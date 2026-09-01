#!/usr/bin/env python3
"""The published conformance macros must agree with the real validator.

conformance/duck_block_conformance.sql is pure SQL that consumers copy, because an
extension that vendors its own DuckDB submodule CANNOT LOAD duck_block_utils at all --
DuckDB matches extension ABI by exact version string, so a pin off the release tag
(`v1.5.5-dev154`) is refused by every route. Raised by the webbed session, whose
metadata blocks carried a NULL level for three major spec versions with nothing in a
position to object: the check that would have caught it was one they could not run.

So there are now TWO implementations of one rule, which is the shape that hid an
image's alt text in this very repo -- the reader wrote a fact twice, the exporter read
one copy, and a round trip through the pair looked perfect because the pair shares its
assumptions. Maintaining both carefully is not a defence. Comparing them is.

This runs both over one corpus and fails on ANY disagreement, in either direction.
"""

import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
MACROS = REPO / "conformance" / "duck_block_conformance.sql"

B = (
    "{{'kind':'{k}','element_type':{t},'content':{c},'level':{l},'encoding':'{e}',"
    "'attributes':MAP{{}},'element_order':{o}}}::duck_block"
)


def blk(k="block", t="'paragraph'", c="'x'", l="1", e="text", o="0"):
    return B.format(k=k, t=t, c=c, l=l, e=e, o=o)


# (name, SQL list expression). Each is run through BOTH implementations.
CASES = [
    ("plain paragraph", f"[{blk()}]"),
    ("nested one level", f"[{blk(t=chr(39)+'div'+chr(39), c='NULL')}, {blk(l='2', o='1')}]"),
    ("value kind", f"[{blk(k='value', t=chr(39)+'metadata'+chr(39), e='yaml')}]"),
    ("level 0", f"[{blk(l='0')}]"),
    ("NULL level", f"[{blk(l='NULL')}]"),
    ("level jump 1->3", f"[{blk(t=chr(39)+'div'+chr(39), c='NULL')}, {blk(l='3', o='1')}]"),
    ("duplicate element_order", f"[{blk()}, {blk(o='0')}]"),
    ("negative element_order", f"[{blk(o='-1')}]"),
    ("bad kind", f"[{blk(k='thing')}]"),
    ("bad encoding", f"[{blk(e='klingon')}]"),
    ("NULL element_type", f"[{blk(t='NULL')}]"),
    # Real reader output, not hand-built: the pair must agree on what this repo emits.
    (
        "reader: rich doc + metadata",
        "pandoc_ast_to_blocks('{\"meta\":{\"title\":{\"t\":\"MetaString\",\"c\":\"T\"}},"
        "\"blocks\":[{\"t\":\"Para\",\"c\":[{\"t\":\"Str\",\"c\":\"a \"},"
        "{\"t\":\"Strong\",\"c\":[{\"t\":\"Str\",\"c\":\"b\"}]}]}]}')",
    ),
    (
        "reader: nested list",
        "pandoc_ast_to_blocks('[{\"t\":\"BulletList\",\"c\":[[{\"t\":\"Plain\",\"c\":"
        "[{\"t\":\"Str\",\"c\":\"o\"}]},{\"t\":\"BulletList\",\"c\":[[{\"t\":\"Plain\",\"c\":"
        "[{\"t\":\"Str\",\"c\":\"i\"}]}]]}]]}]')",
    ),
]


def skip(reason: str) -> int:
    if os.environ.get("DUCK_BLOCK_CHECKS_STRICT") == "1":
        print(f"FAIL: {reason}")
        print("      DUCK_BLOCK_CHECKS_STRICT=1 is set, so a skipped check is a failed check.")
        return 1
    print(f"SKIP: {reason}")
    return 0


def duckdb_bin():
    for candidate in ("build/release/duckdb", "build/debug/duckdb"):
        if (REPO / candidate).exists():
            return REPO / candidate
    return None


def main() -> int:
    duckdb = duckdb_bin()
    if duckdb is None:
        return skip("no built duckdb binary (run `make` first)")
    if not MACROS.exists():
        print(f"FAIL: {MACROS.relative_to(REPO)} is missing -- it is what consumers copy.")
        return 1

    script = [MACROS.read_text()]
    for name, expr in CASES:
        script.append(
            f"SELECT '{name}' AS case_name, "
            f"duck_blocks_validate({expr}).valid AS extension_says, "
            f"duck_blocks_are_valid({expr}) AS macro_says;"
        )
    proc = subprocess.run(
        [str(duckdb), "-noheader", "-list"],
        input="\n".join(script),
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        print("FAIL: could not run the comparison\n" + proc.stderr.strip()[:800])
        return 1

    rows = [ln for ln in proc.stdout.splitlines() if "|" in ln]
    if len(rows) != len(CASES):
        print(f"FAIL: expected {len(CASES)} results, got {len(rows)}.")
        print("      A case that errors instead of returning a verdict is not a pass.")
        print("\n".join(rows[:5]))
        return 1

    print(f"Checking conformance/{MACROS.name} against duck_blocks_validate()")
    failed = False
    for row in rows:
        name, ext, mac = (p.strip() for p in row.split("|"))
        if ext == mac:
            continue
        failed = True
        print(f"\nFAIL: `{name}` -- the extension says {ext}, the published macro says {mac}.")
        print("      Consumers that vendor their own DuckDB CANNOT load the extension, so the")
        print("      macro is the only conformance they can run. A disagreement means one class")
        print("      of consumer is being told the opposite of the other. Fix whichever is")
        print("      wrong; do not leave them to drift.")
    if not failed:
        print(
            f"  {len(CASES)} cases agree, including {sum(1 for n, _ in CASES if n.startswith('reader:'))} "
            f"taken from real reader output rather than hand-built"
        )
        print("OK: the macro consumers copy says what the validator says.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
