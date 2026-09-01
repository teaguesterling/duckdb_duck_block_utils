#!/usr/bin/env python3
"""Assert docs/duck_blocks_spec.md documents exactly the vocabulary the build has.

The original PANDOC_AST_GAPS.md report found the spec advertising three mappings
(`pandoc:lineblock`, `pandoc:deflist`, `pandoc:figure`) that no code produced, and
a fourth (`pandoc:div`) that had drifted from what was emitted. Nothing detected
that, because nothing compared them. This does.

Fails in BOTH directions, for the same reason the Pandoc alignment ledger does:

  * a type the build reports that the spec does not document -> fail (undocumented)
  * a type the spec documents that the build does not have   -> fail (stale)

One-directional would rot. A spec that keeps advertising a removed type misleads
exactly as badly as one that omits a new one -- arguably worse, since a reader
trusting it writes code against something that isn't there.

Exits 0 and skips when there is no built binary.
"""

import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
SPEC = REPO / "docs" / "duck_blocks_spec.md"

# `kind` values, not element types -- they head their own table.
KINDS = {"block", "inline", "value"}

# `encoding` values, which share the table shape but are a different axis.
# NOTE `text` is deliberately absent: it is BOTH an encoding and a real inline
# element_type (INLINE_TEXT), so excluding it here would strip a genuine type and
# report a false failure -- which is exactly what the first version of this file did.
ENCODINGS = {"json", "yaml", "html", "xml", "latex", "markdown"}


def duckdb_bin():
    for candidate in ("build/release/duckdb", "build/debug/duckdb"):
        path = REPO / candidate
        if path.exists():
            return path
    return None


def main() -> int:
    duckdb = duckdb_bin()
    if duckdb is None:
        print("SKIP: no built duckdb binary (run `make` first)")
        return 0

    proc = subprocess.run(
        [str(duckdb), "-noheader", "-list", "-c", "SELECT unnest(duck_block_type_names());"],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        print("FAIL: could not read duck_block_type_names()\n" + proc.stderr.strip())
        return 1
    actual = {line.strip() for line in proc.stdout.splitlines() if line.strip()}

    # element_type names are the first column of the spec's type tables.
    documented = set()
    for line in SPEC.read_text().splitlines():
        m = re.match(r"\|\s*`([a-z_]+)`\s*\|", line)
        if m:
            documented.add(m.group(1))
    documented -= KINDS | ENCODINGS

    undocumented = sorted(actual - documented)
    stale = sorted(documented - actual)

    print(f"Checking {SPEC.relative_to(REPO)} against duck_block_type_names()")
    print(f"  build reports {len(actual)} element types; spec documents {len(documented)}")

    failed = False
    if undocumented:
        failed = True
        print("\nFAIL: the build has element types the spec does not document:")
        for name in undocumented:
            print(f"        {name}")
    if stale:
        failed = True
        print("\nFAIL: the spec documents element types the build does not have:")
        for name in stale:
            print(f"        {name}")
        print("      Remove them, or add them to ENCODINGS/KINDS if misclassified.")

    if failed:
        return 1
    print("OK: spec and vocabulary agree in both directions.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
