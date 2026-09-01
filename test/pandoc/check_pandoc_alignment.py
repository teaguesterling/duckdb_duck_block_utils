#!/usr/bin/env python3
"""Assert duck_block_utils' Pandoc AST mapping matches what a real pandoc emits.

Scope is CONSTRUCTOR COVERAGE -- "does pandoc emit a `t` we do not map" -- and
deliberately not output equality. Pandoc has real bugs (its RTF reader drops the
space after certain characters; on LibreOffice RTF it reads headings as
Para[Strong[Span]] and detects no heading at all), so asserting agreement with it
invites failures that are pandoc's fault rather than ours. If a later change does
compare output, it needs a three-way triage -- we-are-wrong / reference-is-wrong /
not-implemented -- rather than a plain diff.

Detection leans on the `generic` backstop: any constructor the converter does not
recognise arrives as element_type='generic' carrying attributes['source_type'].
So "unhandled" is directly observable rather than inferred.

The ledger below RATCHETS IN BOTH DIRECTIONS:

  * a constructor pandoc emits that is unhandled and NOT in the ledger -> fail
    (a new gap appeared)
  * a constructor in the ledger that is now handled -> fail, demanding promotion
    (the ledger went stale)

One-directional would rot: a fixed gap keeps claiming to be broken until it
misleads someone.

Exits 0 and skips when pandoc is absent.
"""

import os
import json
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
FIXTURE = HERE / "fixtures" / "constructors.md"

# Constructors known to be unmapped. Empty is the goal; a non-empty entry must
# say why it is acceptable.
# EMPTY, and empty is the state in which you cannot tell an AUDITED registry from an
# unaudited one -- duckdb_markdown's point, about a registry of their own. The staleness
# branch below (`LEDGER.keys() - unhandled`) is unreachable while this dict is empty, so
# it had never run once despite existing.
#
# Control-tested 2026-09-01 rather than reasoned about: planting {"Header": ...} makes it
# report `ledger is stale -- recorded as gaps but now handled: Header`, and removing it
# returns the run to green. So the branch works, and this comment is the only place that
# fact is recorded, because nothing in a green run can show it.
LEDGER: dict[str, str] = {}

# Constructors no pandoc reader emits, so the fixture cannot exercise them and
# their absence is not a coverage gap.
UNREACHABLE = {"Null": "no pandoc reader emits it; intentionally yields no element"}

# pandoc-types 1.23. Sub-enums (Alignment, ColWidth, QuoteType, ListNumberStyle,
# ListNumberDelim) share the `t` key but are not Block/Inline constructors.
BLOCKS = {
    "Plain",
    "Para",
    "LineBlock",
    "CodeBlock",
    "RawBlock",
    "BlockQuote",
    "OrderedList",
    "BulletList",
    "DefinitionList",
    "Header",
    "HorizontalRule",
    "Table",
    "Figure",
    "Div",
    "Null",
}
INLINES = {
    "Str",
    "Emph",
    "Underline",
    "Strong",
    "Strikeout",
    "Superscript",
    "Subscript",
    "SmallCaps",
    "Quoted",
    "Cite",
    "Code",
    "Space",
    "SoftBreak",
    "LineBreak",
    "Math",
    "RawInline",
    "Link",
    "Image",
    "Note",
    "Span",
}
CONSTRUCTORS = BLOCKS | INLINES


# A skipped check reports success, which is how a guard quietly stops guarding.
# The webbed session found their duck_block conformance test had never run
# ANYWHERE -- `require duck_block_utils` skipped it every time, so it had zero
# directions of coverage while looking green. Locally a skip is right: a dev
# without pandoc should not be blocked. In CI it is not, because CI is where the
# guarantee is supposed to hold. Set DUCK_BLOCK_CHECKS_STRICT=1 there.
def skip(reason: str) -> int:
    if os.environ.get("DUCK_BLOCK_CHECKS_STRICT") == "1":
        print(f"FAIL: {reason}")
        print("      DUCK_BLOCK_CHECKS_STRICT=1 is set, so a skipped check is a failed check.")
        return 1
    print(f"SKIP: {reason}")
    return 0


def duckdb_bin() -> Path | None:
    for candidate in ("build/release/duckdb", "build/debug/duckdb"):
        path = REPO / candidate
        if path.exists():
            return path
    return None


def collect_constructors(node, out: set) -> None:
    if isinstance(node, dict):
        t = node.get("t")
        if isinstance(t, str) and t in CONSTRUCTORS:
            out.add(t)
        for value in node.values():
            collect_constructors(value, out)
    elif isinstance(node, list):
        for value in node:
            collect_constructors(value, out)


def main() -> int:
    if shutil.which("pandoc") is None:
        return skip("pandoc is not installed")
    duckdb = duckdb_bin()
    if duckdb is None:
        return skip("no built duckdb binary (run `make` first)")

    version = subprocess.run(["pandoc", "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    print(f"Checking duck_block_utils' AST mapping against {version}")

    ast_json = subprocess.run(
        ["pandoc", "-f", "markdown+citations", "-t", "json", str(FIXTURE)],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    emitted: set = set()
    collect_constructors(json.loads(ast_json)["blocks"], emitted)
    # UNREACHABLE EXPIRES TWO WAYS, and neither was audited -- the same pair
    # duckdb_markdown found in their fallthrough allowlist, in the check of mine I had
    # not thought to apply my own finding to. Fixing the instance and not the class.
    #
    #   stale key   the constructor is renamed or removed in a later pandoc-types, so
    #               the entry subtracts NOTHING from a set it is not in. It excuses
    #               nothing and looks healthy, forever.
    #   superseded  a reader starts emitting it. The entry removes it from `reachable`
    #               BEFORE coverage is computed, so the fixture is never asked to
    #               exercise it -- the exclusion silently outlives its own premise and
    #               cannot be reported by the coverage check it is disabling.
    #
    # The second is the dangerous one: it is an exclusion that suppresses the very
    # evidence that would retire it.
    unreachable_stale = sorted(UNREACHABLE.keys() - CONSTRUCTORS)
    unreachable_emitted = sorted(UNREACHABLE.keys() & emitted)
    if unreachable_stale or unreachable_emitted:
        for name in unreachable_stale:
            print(f"\nFAIL: UNREACHABLE names `{name}`, which is not a constructor any more.")
            print(f"      recorded reason: {UNREACHABLE[name]}")
        for name in unreachable_emitted:
            print(f"\nFAIL: UNREACHABLE says `{name}` is never emitted -- the fixture just emitted it.")
            print(f"      recorded reason: {UNREACHABLE[name]}")
            print("      While the entry stands, coverage for this constructor is DISABLED.")
        print("      DELETE the entry rather than rewording it. If the reason still reads")
        print("      as true, the property it describes has moved, and that is the thing")
        print("      worth understanding before the entry goes.")
        return 1

    reachable = CONSTRUCTORS - UNREACHABLE.keys()
    missing = sorted(reachable - emitted)
    print(
        f"  fixture exercises {len(emitted)} of {len(reachable)} reachable "
        f"constructors ({len(UNREACHABLE)} unreachable by any reader)"
    )
    if missing:
        print("  NOT exercised, so not checked: " + ", ".join(missing))

    # Feed the whole AST through the converter and read back what came out as
    # `generic` -- those are precisely the constructors it could not map.
    sql = (
        "SELECT DISTINCT b.attributes['source_type'] AS t "
        "FROM (SELECT unnest(pandoc_ast_to_blocks(?)) AS b) "
        "WHERE b.element_type = 'generic' AND b.attributes['source_type'] IS NOT NULL;"
    )
    proc = subprocess.run(
        [str(duckdb), "-noheader", "-list", "-c", sql.replace("?", "(SELECT content FROM read_text('/dev/stdin'))")],
        input=ast_json,
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        print("FAIL: conversion errored\n" + proc.stderr.strip())
        return 1
    unhandled = {line.strip() for line in proc.stdout.splitlines() if line.strip()}

    new_gaps = sorted(unhandled - LEDGER.keys())
    stale = sorted(LEDGER.keys() - unhandled)

    for name in sorted(unhandled & LEDGER.keys()):
        print(f"  known gap unchanged: {name} ({LEDGER[name]})")

    failed = False
    if new_gaps:
        failed = True
        print("\nFAIL: constructors pandoc emits that the converter does not map,")
        print("      and which are not recorded in the ledger:")
        for name in new_gaps:
            print(f"        {name}")
        print("      Either map it, or add it to LEDGER with a reason.")
    if stale:
        failed = True
        print("\nFAIL: ledger is stale -- these are recorded as gaps but are now")
        print("      handled. Remove them from LEDGER:")
        for name in stale:
            print(f"        {name}")

    # CONSTRUCTOR COVERAGE IS NOT ATTRIBUTE COVERAGE. A fixture containing an
    # ordered list satisfies the ledger above while every list in it starts at 1
    # in Decimal/Period -- so a reader that ignored start/number_style/
    # number_delim and hardcoded the defaults would pass. panduck found the same
    # hole from the other end: three EPUB fixtures contained <ol> and not one
    # reached the reader, because every <ol> a real writer emits lands in
    # navigation. Containing a construct is not exercising it, and exercising it
    # at its defaults does not discriminate.
    attr_sql = (
        "SELECT DISTINCT coalesce(b.attributes['start'],'-') || '/' || "
        "coalesce(b.attributes['number_style'],'-') || '/' || coalesce(b.attributes['number_delim'],'-') "
        "FROM (SELECT unnest(pandoc_ast_to_blocks(?)) AS b) "
        "WHERE b.element_type = 'list' AND b.attributes['ordered'] = 'true';"
    )
    proc = subprocess.run(
        [
            str(duckdb),
            "-noheader",
            "-list",
            "-c",
            attr_sql.replace("?", "(SELECT content FROM read_text('/dev/stdin'))"),
        ],
        input=ast_json,
        capture_output=True,
        text=True,
    )
    combos = {line.strip() for line in proc.stdout.splitlines() if line.strip()}
    non_default = {c for c in combos if c not in ("-/-/-", "1/Decimal/Period")}
    if not non_default:
        failed = True
        print("\nFAIL: no ordered list in the fixture uses a NON-DEFAULT start, style or")
        print("      delimiter, so a reader hardcoding 1/Decimal/Period would pass this")
        print("      check. Add one (e.g. a list starting at 3, or lower-roman).")
        print(f"      seen: {sorted(combos)}")
    else:
        print(f"  ordered-list attributes exercised beyond defaults: {sorted(non_default)}")

    if failed:
        return 1
    print(f"OK: all {len(emitted)} emitted constructors are mapped; " f"ledger is empty and accurate.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
