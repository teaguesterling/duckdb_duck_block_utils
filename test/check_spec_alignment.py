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

import os
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


def duckdb_bin():
    for candidate in ("build/release/duckdb", "build/debug/duckdb"):
        path = REPO / candidate
        if path.exists():
            return path
    return None


def main() -> int:
    duckdb = duckdb_bin()
    if duckdb is None:
        return skip("no built duckdb binary (run `make` first)")

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

    # The spec doc's own Version header against the shipped SPEC_VERSION. These are two
    # copies of one fact, and they disagreed from v1.0.0 until 2026-08-31: the header
    # read 0.4.0 while consumers asserted duck_block_spec_version(), which had moved
    # five majors. Nothing compared them, so a peer reading the document to decide what
    # to implement was reading a number no code had produced in months.
    ver = subprocess.run(
        [str(duckdb), "-noheader", "-list", "-c", "SELECT duck_block_spec_version();"],
        capture_output=True,
        text=True,
    )
    shipped = ver.stdout.strip() if ver.returncode == 0 else ""
    m = re.search(r"^\*\*Version:\*\*\s*([0-9]+\.[0-9]+)", SPEC.read_text(), re.M)
    header = m.group(1) if m else None
    if not shipped:
        failed = True
        print("\nFAIL: could not read duck_block_spec_version()")
    elif header is None:
        failed = True
        print("\nFAIL: the spec document has no parseable `**Version:** MAJOR.MINOR` header.")
        print("      A consumer deciding what to implement reads that line.")
    elif header != shipped:
        failed = True
        print(f"\nFAIL: the spec document says version {header}; the build ships {shipped}.")
        print("      Whichever is right, they cannot both be published. Update the header.")
    else:
        print(f"  spec version {shipped} agrees between the document and the build")
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
