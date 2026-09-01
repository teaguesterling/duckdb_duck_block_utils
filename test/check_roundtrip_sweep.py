#!/usr/bin/env python3
"""Sweep EVERY block type through the paths that enumerate types.

Two failure classes, both found repeatedly in one week, both invisible to code
review because the code was CORRECT for every type that existed when it was
written -- the defect is created by a later addition, elsewhere:

  WRITE-ONLY   a type exports fine and cannot be read back, so a round trip
               silently downgrades it. `section` became `div`; `page_break`
               became `div`. Neither was findable by looking at section or
               page_break.

  DROPPED      a container's child walk enumerates type names, so a type it does
               not know vanishes. `table`, `deflist` and `lineblock` were lost
               inside every div, blockquote, figure and caption -- for weeks.

A sweep produces CANDIDATES, not findings. Three candidates here were investigated
and are NOT defects; they are listed in INHERENT with their reasoning so the sweep
cannot re-raise them and nobody re-investigates. Recording the negatives is the
part that keeps a sweep usable -- an unexplained exclusion and a forgotten defect
look identical.

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

# element_type -> (content SQL, encoding, attributes SQL, needs a child block?)
PROBES = {
    "heading": ("'H'", "text", "MAP{'heading_level':'2'}", False),
    "paragraph": ("'x'", "text", "MAP{}", False),
    "plain": ("'x'", "text", "MAP{}", False),
    "code": ("'x'", "text", "MAP{}", False),
    "hr": ("'x'", "text", "MAP{}", False),
    "lineblock": ("'x'", "text", "MAP{}", False),
    "raw": ("'<b>x</b>'", "html", "MAP{'format':'html'}", False),
    "image": ("'alt'", "text", "MAP{'src':'i.png'}", False),
    "section": ("NULL", "text", "MAP{'role':'article'}", True),
    "page_break": ("''", "text", "MAP{'page_number':'3'}", False),
    "generic": (r"""'{"t":"Marquee","c":[]}'""", "json", "MAP{'source_type':'Marquee'}", False),
    "blockquote": ("NULL", "text", "MAP{}", True),
    "div": ("NULL", "text", "MAP{}", True),
    "figure": ("NULL", "text", "MAP{}", True),
    "caption": ("NULL", "text", "MAP{}", True),
}

# Round trips that do NOT preserve the type, investigated and found inherent.
# Each entry is (what it becomes, why it cannot be otherwise).
INHERENT = {
    "image": (
        "paragraph",
        "Pandoc has no block Image constructor. Para[Image] is its only encoding, so a "
        "block image and a paragraph containing one image are the SAME document to it. "
        "Unlike section and page_break -- where the exporter writes a recoverable marker "
        "-- there is nothing to write, so promoting invents a distinction the source "
        "cannot carry. Was 'fixed' once; the existing tests caught that it destroyed the "
        "alt text.",
    ),
    "caption": (
        "paragraph",
        "Only a STANDALONE caption, which is malformed anyway -- a caption belongs to "
        "the container before it. Inside a figure it round-trips: figure > plain > "
        "caption > plain. The sweep's synthetic probe is unrepresentative here.",
    ),
}

# Constructors that must survive inside a container, with a probe string to find.
NESTED = [
    ("CodeBlock", r'{"t":"CodeBlock","c":[["",[],[]],"NESTPROBE"]}', "NESTPROBE"),
    ("HorizontalRule", r'{"t":"HorizontalRule"}', "HorizontalRule"),
    ("LineBlock", r'{"t":"LineBlock","c":[[{"t":"Str","c":"NESTPROBE"}]]}', "NESTPROBE"),
    ("BulletList", r'{"t":"BulletList","c":[[{"t":"Plain","c":[{"t":"Str","c":"NESTPROBE"}]}]]}', "NESTPROBE"),
    (
        "DefinitionList",
        r'{"t":"DefinitionList","c":[[[{"t":"Str","c":"NESTPROBE"}],[[{"t":"Plain","c":[{"t":"Str","c":"d"}]}]]]]}',
        "NESTPROBE",
    ),
    ("Plain", r'{"t":"Plain","c":[{"t":"Str","c":"NESTPROBE"}]}', "NESTPROBE"),
    (
        "Table",
        r'{"t":"Table","c":[["",[],[]],[null,[]],[],[["",[],[]],[[["",[],[]],[[["",[],[]],'
        r'{"t":"AlignDefault"},1,1,[{"t":"Plain","c":[{"t":"Str","c":"NESTPROBE"}]}]]]]]],[],[["",[],[]],[]]]}',
        "NESTPROBE",
    ),
]


# THIRD ARM. A container carrying its OWN text must not lose it on export.
#
# Neither arm above can see this class. The write-only arm compares element_type, so a
# type that survives with its text gone passes. The containment arm builds its probes
# from Pandoc AST, so it only ever exercises the shapes THIS repo's reader produces --
# and a reader and an exporter written together share their misunderstandings. The
# defect that prompted this arm was exactly that: the reader wrote an image's alt text
# into BOTH `content` and `attributes['alt']`, the exporter read only the attribute, and
# every round trip through this repo looked clean while any other producer -- one
# following the vocabulary's content rule, which says the text goes in `content` -- lost
# the alt silently.
#
# So these probes are HAND-BUILT rather than read from AST. That is the whole point:
# they are the only thing here that does not go through the reader first.
CONTENT_PROBE = "SWEEPTEXTZ"

# Types needing an attribute before the probe means anything.
CONTENT_ATTRS = {
    "image": "MAP{'src':'i.png'}",
    "raw": "MAP{'format':'html'}",
    "heading": "MAP{'heading_level':'2'}",
    "code": "MAP{'language':'sql'}",
}

# Types whose text legitimately has nowhere to go, with the reason. As in INHERENT, the
# negatives are the part worth writing down: an unexplained exemption and a forgotten
# defect look identical six months later.
CONTENT_EXEMPT = {
    "hr": "HorizontalRule has no text position at all -- Pandoc's constructor takes no arguments.",
    "page_break": "A marker. It exports as an empty classed Div by design; it owns no blocks.",
    "list": "A list's text lives in its ITEMS. A list carrying content directly is malformed, "
    "and list_item is probed with a real parent below, which is the shape that matters.",
    "table": "content is the native {headers,rows} JSON, so a bare word is not a table. Real "
    "tables round-trip through the preserved pandoc_ast tuple and are tested in "
    "pandoc_blocks_v2.test.",
    "metadata": "kind='value', not a block -- it lands in the document's `meta`, not in `blocks`.",
}


# FOURTH ARM. The same text, through the RENDER path rather than the export path.
#
# Added on a tip from duckdb_markdown, who ran the content arm above against their own
# code and found FOUR instances -- div, section, figure, caption -- of a rule they had
# already fixed for list_item that same evening. Given a rule and one symptom they
# repaired the symptom and left the class intact in four more places, which is what a
# sweep converts into a list of sites and re-reading the rule does not.
#
# Their `caption` is the case that argues for this arm specifically: a structural branch
# consumed a childless caption and emitted nothing, which SHADOWED the leaf renderer --
# so their first fix was live, correct, and unreachable. A fix that cannot be reached and
# a fix that does not work produce identical output.
#
# The export arm above cannot see any of that: a type can export its text perfectly and
# still render as nothing, and this repo shipped exactly that combination earlier the same
# day (figure, caption and list carrying content rendered as nothing while to_text
# returned it). Two paths, two arms.
RENDER_EXEMPT = {
    "hr": "A rule. No text position in either path -- same reason as the export arm.",
    "page_break": "A marker. It renders as a break; text on it has no meaning.",
    "table": "content is the native {headers,rows} JSON, so a bare word is not a table "
    "and the renderer has nothing to project. Real tables are covered in "
    "render_ansi.test.",
    "metadata": "kind='value'. Document metadata is not body content, so the renderer "
    "correctly declines to draw it; the probe builds it as a block.",
    "raw": "DELIBERATE, and only in to_text: raw markup is omitted so that searching for "
    "`script` does not match `<script>`. Investigated when the agreement guard "
    "first flagged it; it renders fine, which is the half that matters on screen.",
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
    direction the truncation happens to fall -- here a false positive; duckdb_markdown
    hit the same class as a false NEGATIVE the same evening, when their lint bridge
    swallowed stderr and a failing query came back as "no findings".

    Same lesson either way: before trusting a new bridge, run something through it whose
    answer you already know.
    """
    proc = subprocess.run([str(duckdb), "-noheader", "-list", "-c", sql], capture_output=True, text=True)
    if proc.returncode != 0:
        return "<ERROR> " + proc.stderr.strip()
    return proc.stdout


def main() -> int:
    duckdb = duckdb_bin()
    if duckdb is None:
        return skip("no built duckdb binary (run `make` first)")

    failed = False

    print("Round-trip sweep: export then read back, every block type")
    for ty, (content, enc, attrs, needs_child) in sorted(PROBES.items()):
        child = ""
        if needs_child:
            child = (
                f", {{'kind':'block','element_type':'paragraph','content':'inner','level':2,"
                f"'encoding':'text','attributes':MAP{{}},'element_order':1}}::{STRUCT}"
            )
        sql = (
            f"SELECT coalesce((SELECT b.element_type FROM (SELECT unnest(pandoc_ast_to_blocks("
            f"duck_blocks_to_pandoc_blocks([{{'kind':'block','element_type':'{ty}','content':{content},"
            f"'level':1,'encoding':'{enc}','attributes':{attrs},'element_order':0}}::{STRUCT}{child}]"
            f")::VARCHAR)) AS b) WHERE b.kind='block' LIMIT 1), '<NOTHING>');"
        )
        got = run(duckdb, sql)
        if got == ty:
            continue
        if ty in INHERENT and got == INHERENT[ty][0]:
            continue
        failed = True
        print(f"\nFAIL: `{ty}` does not survive a round trip -- it reads back as `{got}`.")
        if ty in INHERENT:
            print(f"      Expected `{INHERENT[ty][0]}` by the recorded exception, got `{got}`.")
        else:
            print("      Either the reader cannot read what the exporter writes (add the read side),")
            print("      or the encoding genuinely cannot carry the distinction -- in which case add")
            print("      it to INHERENT with the reasoning rather than 'fixing' it.")
    if not failed:
        n_inherent = len(INHERENT)
        print(
            f"  {len(PROBES) - n_inherent} types round-trip to themselves; "
            f"{n_inherent} recorded as inherent ({', '.join(sorted(INHERENT))})"
        )

    print("Containment sweep: every constructor inside a Div")
    for name, inner, probe in NESTED:
        doc = '{"pandoc-api-version":[1,23,1],"meta":{},"blocks":[{"t":"Div","c":[["",[],[]],[' + inner + ']]}]}'
        got = run(duckdb, f"SELECT duck_blocks_to_pandoc_blocks(pandoc_ast_to_blocks('{doc}'))::VARCHAR;")
        if probe in got:
            continue
        failed = True
        print(f"\nFAIL: `{name}` is DROPPED inside a container -- its content never reaches the output.")
        print("      A container's child walk must not decide whether a child EXISTS by")
        print("      enumerating type names. Add a terminal arm that does not need to know.")
    if not failed:
        print(f"  all {len(NESTED)} constructors survive inside a container")

    print("Content sweep: every block type, hand-built, must not lose its own text")
    types = run(duckdb, "SELECT string_agg(t, ' ') FROM (SELECT unnest(duck_block_type_names()) AS t);").split()
    checked = 0
    for ty in sorted(set(types)):
        if ty in CONTENT_EXEMPT:
            continue
        attrs = CONTENT_ATTRS.get(ty, "MAP{}")
        blk = (
            f"{{'kind':'block','element_type':'{ty}','content':'{CONTENT_PROBE}','level':%d,"
            f"'encoding':'text','attributes':{attrs},'element_order':%d}}::{STRUCT}"
        )
        # list_item gets a real parent: standalone it is malformed, and probing a
        # malformed shape would report a defect the vocabulary does not have.
        if ty == "list_item":
            parent = (
                f"{{'kind':'block','element_type':'list','content':NULL,'level':1,'encoding':'text',"
                f"'attributes':MAP{{'list_type':'bullet'}},'element_order':0}}::{STRUCT}"
            )
            doc_sql = f"[{parent}, {blk % (2, 1)}]"
        else:
            doc_sql = f"[{blk % (1, 0)}]"
        checked += 1
        got = run(duckdb, f"SELECT duck_blocks_to_pandoc_blocks({doc_sql})::VARCHAR;")
        if CONTENT_PROBE in got:
            continue
        failed = True
        print(f"\nFAIL: `{ty}` carries text in `content` and the exporter DROPS it.")
        print(f"      got: {got[:160]}")
        print("      The vocabulary's content rule says a single text child lives in `content`,")
        print("      so an exporter reading only an attribute loses it for every producer but")
        print("      one that happens to write both. Read `content` as the fallback, or add the")
        print("      type to CONTENT_EXEMPT with the reason its text has nowhere to go.")
    if not failed:
        print(
            f"  {checked} types keep their text; {len(CONTENT_EXEMPT)} exempt " f"({', '.join(sorted(CONTENT_EXEMPT))})"
        )

    print("Render sweep: the same text, through render_ansi and to_text")
    rendered = 0
    for ty in sorted(set(types)):
        if ty in RENDER_EXEMPT:
            continue
        attrs = CONTENT_ATTRS.get(ty, "MAP{}")
        blk = (
            f"[{{'kind':'block','element_type':'{ty}','content':'{CONTENT_PROBE}','level':1,"
            f"'encoding':'text','attributes':{attrs},'element_order':0}}::{STRUCT}]"
        )
        rendered += 1
        shown = run_all(
            duckdb, "SELECT regexp_replace(duck_blocks_render_ansi(" + blk + ", 40), '\x1b\\[[0-9;]*m', '', 'g');"
        )
        text = run_all(duckdb, f"SELECT duck_blocks_to_text({blk});")
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

    if failed:
        return 1
    print("OK: no write-only types, nothing dropped in a container, no text lost on export or render.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
