#!/usr/bin/env python3
"""Sweep EVERY declared block type through the two RENDER paths: render_ansi and to_text.

This is the render arm of the former test/check_roundtrip_sweep.py. The other arms of
that sweep exported through the Pandoc converter and read back through it, and the
converter moved to panduck (which keeps those arms as test/converter/check_roundtrip_sweep.py).
This arm never touched the converter: it asks whether this repo's own renderers show a
type's text, so it stays here.

Set DUCK_BLOCK_CHECKS_STRICT=1 to make a skip a failure (see the other checks).
"""

import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent

STRUCT = (
    'STRUCT(kind VARCHAR, element_type VARCHAR, "content" VARCHAR, "level" INTEGER, '
    '"encoding" VARCHAR, attributes MAP(VARCHAR,VARCHAR), element_order INTEGER)'
)

CONTENT_PROBE = "SWEEPTEXTZ"

# Types needing an attribute before the probe means anything.
CONTENT_ATTRS = {
    "image": "MAP{'src':'i.png'}",
    "raw": "MAP{'format':'html'}",
    "heading": "MAP{'heading_level':'2'}",
    "code": "MAP{'language':'sql'}",
}


# The same text, through the RENDER path rather than the export path.
#
# Added on a tip from duckdb_markdown, who ran the content arm of the round-trip sweep
# against their own code and found FOUR instances -- div, section, figure, caption -- of a
# rule they had already fixed for list_item that same evening. Given a rule and one
# symptom they repaired the symptom and left the class intact in four more places, which
# is what a sweep converts into a list of sites and re-reading the rule does not.
#
# Their `caption` is the case that argues for this arm specifically: a structural branch
# consumed a childless caption and emitted nothing, which SHADOWED the leaf renderer --
# so their first fix was live, correct, and unreachable. A fix that cannot be reached and
# a fix that does not work produce identical output.
#
# An export check cannot see any of that: a type can export its text perfectly and
# still render as nothing, and this repo shipped exactly that combination (figure,
# caption and list carrying content rendered as nothing while to_text returned it).
RENDER_EXEMPT = {
    "hr": "A rule. No text position in either path.",
    "page_break": "A marker. It renders as a break; text on it has no meaning.",
    "table": "content is the native {headers,rows} JSON, so a bare word is not a table "
    "and the renderer has nothing to project. Real tables are covered in "
    "render_ansi.test.",
    "metadata": "kind='value'. Document metadata is not body content, so the renderer "
    "correctly declines to draw it; the probe builds it as a block.",
    "raw": "DELIBERATE, and only in to_text: raw markup is omitted so that searching for "
    "`script` does not match `<script>`. Investigated when the agreement guard "
    "first flagged it; it renders fine, which is the half that matters on screen.",
    "document": "A root carrying content is invalid per spec 1.4, so there is no text for "
    "the renderer to project. Exempting it here is not a gap -- a root with content "
    "never reaches a reader.",
}


def skip(reason: str) -> int:
    if os.environ.get("DUCK_BLOCK_CHECKS_STRICT") == "1":
        print(f"FAIL: {reason}")
        print("      DUCK_BLOCK_CHECKS_STRICT=1 is set, so a skipped check is a failed check.")
        return 1
    print(f"SKIP: {reason}")
    return 0


def duckdb_bin():
    for candidate in ("build/release/duckdb", "build/debug/duckdb"):
        path = REPO / candidate
        if path.exists():
            return path
    return None


def run(duckdb, sql: str) -> str:
    proc = subprocess.run([str(duckdb), "-noheader", "-list", "-c", sql], capture_output=True, text=True)
    if proc.returncode != 0:
        return "<ERROR> " + proc.stderr.strip().splitlines()[0] if proc.stderr.strip() else "<ERROR>"
    return proc.stdout.strip().splitlines()[0] if proc.stdout.strip() else ""


def run_all(duckdb, sql: str) -> str:
    """Whole output, not the first line.

    `run` above returns line ONE, which is right for a scalar and wrong for anything
    rendered: a code block puts its language on line 1 and its text on line 2, so the
    render arm reported `code` as dropping text that was plainly on screen. A harness
    that truncates its own evidence produces a confident false result in whichever
    direction the truncation happens to fall.

    Before trusting a new bridge, run something through it whose answer you already know.
    """
    proc = subprocess.run([str(duckdb), "-noheader", "-list", "-c", sql], capture_output=True, text=True)
    if proc.returncode != 0:
        return "<ERROR> " + proc.stderr.strip()
    return proc.stdout


def probe(ty: str) -> str:
    attrs = CONTENT_ATTRS.get(ty, "MAP{}")
    return (
        f"[{{'kind':'block','element_type':'{ty}','content':'{CONTENT_PROBE}','level':1,"
        f"'encoding':'text','attributes':{attrs},'element_order':0}}::{STRUCT}]"
    )


def shows(duckdb, blk: str):
    shown = run_all(
        duckdb, "SELECT regexp_replace(duck_blocks_render_ansi(" + blk + ", 40), '\x1b\\[[0-9;]*m', '', 'g');"
    )
    text = run_all(duckdb, f"SELECT duck_blocks_to_text({blk});")
    return shown, text


def main() -> int:
    duckdb = duckdb_bin()
    if duckdb is None:
        return skip("no built duckdb binary (run `make` first)")

    types = run(duckdb, "SELECT string_agg(t, ' ') FROM (SELECT unnest(duck_block_type_names()) AS t);").split()
    if not types or types[0].startswith("<ERROR>"):
        print("FAIL: could not read duck_block_type_names() -- a sweep over no types is not a pass.")
        return 1

    failed = False
    print("Render sweep: the same text, through render_ansi and to_text")
    rendered = 0
    for ty in sorted(set(types)):
        if ty in RENDER_EXEMPT:
            continue
        rendered += 1
        shown, text = shows(duckdb, probe(ty))
        missing = [n for n, v in (("render_ansi", shown), ("to_text", text)) if CONTENT_PROBE not in v]
        if not missing:
            continue
        failed = True
        print(f"\nFAIL: `{ty}` carries text in `content` and {' and '.join(missing)} shows NOTHING.")
        print("      A type can export its text perfectly and still render as nothing -- this repo")
        print("      shipped exactly that for figure, caption and list. Check for a structural branch")
        print("      that consumes the element and emits nothing, which SHADOWS the leaf renderer and")
        print("      makes a correct fix unreachable. Or add it to RENDER_EXEMPT with the reason.")
    if not failed:
        print(
            f"  {rendered} types show their text in both; {len(RENDER_EXEMPT)} exempt "
            f"({', '.join(sorted(RENDER_EXEMPT))})"
        )

    # EXCLUSIONS EXPIRE, and a stale one is worse than none: it goes on excusing a case
    # that no longer needs excusing, and hides the next regression behind an explanation
    # nobody rechecks. An exempt type that now PASSES is reported as expired, and so is
    # an entry whose KEY names a type that no longer exists.
    print("Exemption audit: is every recorded exclusion still load-bearing?")
    stale = []
    for ty, reason in sorted(RENDER_EXEMPT.items()):
        if ty not in set(types):
            stale.append((ty, "no such element_type any more", reason))
            continue
        shown, text = shows(duckdb, probe(ty))
        if CONTENT_PROBE in shown and CONTENT_PROBE in text:
            stale.append((ty, "it PASSES now", reason))
    if stale:
        failed = True
        for ty, why, reason in stale:
            print(f"\nFAIL: `{ty}` no longer needs its RENDER_EXEMPT entry -- {why}.")
            print("      DELETE the entry rather than rewording it.")
            print(f"      recorded reason: {reason}")
    else:
        print(f"  all {len(RENDER_EXEMPT)} exclusions still hold")

    if failed:
        return 1
    print("OK: every declared type shows its text in render_ansi and to_text, or is exempt with a reason.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
