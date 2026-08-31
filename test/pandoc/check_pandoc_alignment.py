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
LEDGER: dict[str, str] = {}

# Constructors no pandoc reader emits, so the fixture cannot exercise them and
# their absence is not a coverage gap.
UNREACHABLE = {"Null": "no pandoc reader emits it; intentionally yields no element"}

# pandoc-types 1.23. Sub-enums (Alignment, ColWidth, QuoteType, ListNumberStyle,
# ListNumberDelim) share the `t` key but are not Block/Inline constructors.
BLOCKS = {
    "Plain", "Para", "LineBlock", "CodeBlock", "RawBlock", "BlockQuote",
    "OrderedList", "BulletList", "DefinitionList", "Header", "HorizontalRule",
    "Table", "Figure", "Div", "Null",
}
INLINES = {
    "Str", "Emph", "Underline", "Strong", "Strikeout", "Superscript",
    "Subscript", "SmallCaps", "Quoted", "Cite", "Code", "Space", "SoftBreak",
    "LineBreak", "Math", "RawInline", "Link", "Image", "Note", "Span",
}
CONSTRUCTORS = BLOCKS | INLINES


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
        print("SKIP: pandoc is not installed")
        return 0
    duckdb = duckdb_bin()
    if duckdb is None:
        print("SKIP: no built duckdb binary (run `make` first)")
        return 0

    version = subprocess.run(
        ["pandoc", "--version"], capture_output=True, text=True
    ).stdout.splitlines()[0]
    print(f"Checking duck_block_utils' AST mapping against {version}")

    ast_json = subprocess.run(
        ["pandoc", "-f", "markdown+citations", "-t", "json", str(FIXTURE)],
        capture_output=True, text=True, check=True,
    ).stdout
    emitted: set = set()
    collect_constructors(json.loads(ast_json)["blocks"], emitted)
    reachable = CONSTRUCTORS - UNREACHABLE.keys()
    missing = sorted(reachable - emitted)
    print(f"  fixture exercises {len(emitted)} of {len(reachable)} reachable "
          f"constructors ({len(UNREACHABLE)} unreachable by any reader)")
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
        [str(duckdb), "-noheader", "-list", "-c",
         sql.replace("?", "(SELECT content FROM read_text('/dev/stdin'))")],
        input=ast_json, capture_output=True, text=True,
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

    if failed:
        return 1
    print(f"OK: all {len(emitted)} emitted constructors are mapped; "
          f"ledger is empty and accurate.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
