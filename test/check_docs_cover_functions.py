#!/usr/bin/env python3
"""Every registered function must be mentioned somewhere in README.md or docs/.

A function nobody is told about is not a feature. This repo shipped
`duck_blocks_normalize` and `duck_block_plain` -- both central to the current content
rule -- with neither named in the README, and found out by counting rather than by
anyone noticing.

The bar is deliberately LOW: the name appears somewhere. That catches the failure that
actually happens -- a function added and never written up -- without pretending to
judge whether the prose is any good, which a check cannot do and which a stricter rule
would only invite people to game.

EXEMPTIONS carry their reason and are audited: an exempt name that is no longer
registered is reported, because an exclusion whose subject has gone excuses nothing and
hides the next one behind an explanation nobody rechecks.
"""

import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent

# name -> why it needs no user-facing documentation
EXEMPT = {
    "duck_block_ensure_extension": "Internal plumbing for the doc macros -- loads a sibling "
    "extension if present. Not part of the public surface; a user "
    "who calls it directly has misread something.",
    "duck_block_doc_macros": "A PRAGMA that registers the document-query macros. Users invoke the "
    "MACROS it creates, which are documented; the pragma itself is the "
    "registration mechanism.",
}


def skip(reason: str) -> int:
    if os.environ.get("DUCK_BLOCK_CHECKS_STRICT") == "1":
        print(f"FAIL: {reason}")
        print("      DUCK_BLOCK_CHECKS_STRICT=1 is set, so a skipped check is a failed check.")
        return 1
    print(f"SKIP: {reason}")
    return 0


def duckdb_bin():
    for c in ("build/release/duckdb", "build/debug/duckdb"):
        if (REPO / c).exists():
            return REPO / c
    return None


def main() -> int:
    duckdb = duckdb_bin()
    if duckdb is None:
        return skip("no built duckdb binary (run `make` first)")

    proc = subprocess.run(
        [
            str(duckdb),
            "-noheader",
            "-list",
            "-c",
            "SELECT DISTINCT function_name FROM duckdb_functions() "
            "WHERE function_name LIKE 'duck_block%' OR function_name LIKE 'pandoc%' "
            "OR function_name LIKE '%duck_block%' ORDER BY 1;",
        ],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        print("FAIL: could not list registered functions\n" + proc.stderr.strip()[:400])
        return 1
    registered = {n.strip() for n in proc.stdout.splitlines() if n.strip()}

    corpus = ""
    for path in [REPO / "README.md"] + sorted((REPO / "docs").rglob("*.md")):
        corpus += path.read_text()

    missing = sorted(n for n in registered if n not in EXEMPT and n not in corpus)
    stale = sorted(n for n in EXEMPT if n not in registered)

    print(f"Checking {len(registered)} registered functions are documented")
    failed = False
    if missing:
        failed = True
        print("\nFAIL: registered but documented nowhere in README.md or docs/:")
        for n in missing:
            print(f"        {n}")
        print("      A function nobody is told about is not a feature. Name it, or add it")
        print("      to EXEMPT with the reason it is not part of the public surface.")
    if stale:
        failed = True
        print("\nFAIL: EXEMPT names functions that are no longer registered:")
        for n in stale:
            print(f"        {n} -- recorded reason: {EXEMPT[n]}")
        print("      DELETE the entry rather than rewording it. An exclusion whose subject")
        print("      has gone excuses nothing and hides the next one behind an explanation.")
    if failed:
        return 1

    print(f"  all documented; {len(EXEMPT)} exempt ({', '.join(sorted(EXEMPT))})")
    print("OK: nothing registered is undocumented.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
