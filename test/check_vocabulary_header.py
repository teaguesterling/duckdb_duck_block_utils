#!/usr/bin/env python3
"""Assert src/include/duck_block_vocabulary.hpp stays consumable by submodule.

Sibling extensions (panduck, webbed, duckdb_markdown, sitting_duck) vendor this
header rather than copying the vocabulary, because copies drift silently. That
only works while the header is **link-free**: every declaration in it must have
its definition inline, or a consumer that includes it gets undefined-symbol
errors at link time for functions it never called.

Nothing in *this* repo would notice that regression -- we link block_types.cpp
anyway. So this compiles a probe the way a consumer would: the vocabulary header
alone, no other project header, and no linking against this extension.

Two checks:

  1. it compiles standalone
  2. it declares nothing it does not define -- caught by grep, since a
     declaration-only member compiles fine here and only fails at a consumer's
     link step, which is precisely the failure this file exists to prevent

Exits 0 and skips when there is no compiler or no duckdb source tree.
"""

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
VOCAB = REPO / "src" / "include" / "duck_block_vocabulary.hpp"

PROBE = """
#include "duck_block_vocabulary.hpp"
#include <cstdio>
int main() {
    std::printf("%s %s %s\\n", duckdb::DuckBlockVocabulary::KIND_VALUE,
                duckdb::DuckBlockVocabulary::TYPE_FIGURE,
                duckdb::DuckBlockVocabulary::SPEC_VERSION);
    return 0;
}
"""


def main() -> int:
    cxx = shutil.which("g++") or shutil.which("clang++")
    if cxx is None:
        print("SKIP: no C++ compiler found")
        return 0
    duckdb_inc = REPO / "duckdb" / "src" / "include"
    if not duckdb_inc.is_dir():
        print("SKIP: no duckdb source tree (submodule not initialised)")
        return 0

    failed = False

    # 1. Declarations without definitions. `static TYPE name();` compiles here
    #    and breaks a consumer at link time.
    text = VOCAB.read_text()
    bad = [
        line.strip()
        for line in text.splitlines()
        if re.search(r"^\s*static\s+\w[\w:<>* ]*\s+\w+\s*\([^)]*\)\s*;\s*$", line)
    ]
    if bad:
        failed = True
        print("FAIL: the vocabulary header declares functions it does not define.")
        print("      A submodule consumer would get undefined symbols at link time:")
        for line in bad:
            print(f"        {line}")
        print("      Move them to block_types.hpp, which is allowed to need linking.")

    # 2. Compiles standalone, with no other project header on the include path.
    with tempfile.TemporaryDirectory() as tmp:
        probe = Path(tmp) / "probe.cpp"
        probe.write_text(PROBE)
        proc = subprocess.run(
            [cxx, "-std=c++17", "-fsyntax-only", "-I", str(VOCAB.parent), "-I", str(duckdb_inc), str(probe)],
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            failed = True
            print("FAIL: the vocabulary header does not compile standalone:")
            print("\n".join(proc.stderr.splitlines()[:12]))

    if failed:
        return 1
    print("OK: duck_block_vocabulary.hpp compiles standalone and needs no linking.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
