#!/usr/bin/env python3
"""Assert everything in vendor/ is actually consumable as a copied file.

`vendor/` claims a property -- copy this and it works, with no dependency on this
extension. An unverified claim of vendorability is worse than none: a consumer finds
out at their build, in their repo, having already committed to the approach.

Most consumers cannot check it for us. DuckDB matches extension ABI by exact version
string, so an extension whose DuckDB submodule is pinned off the release tag is
refused duck_block_utils by every route, which is why these files exist at all.

  duck_block_normalize.hpp   compiles against the vocabulary header + DuckDB and
                             NOTHING else from this repo -- probed by compiling a
                             consumer's translation unit in a directory holding only
                             those two headers, so an accidental `#include` of
                             block_types.hpp or normalize.hpp fails here rather than
                             at a consumer's build.

  duck_block_conformance.sql covered by check_conformance_macro.py, which compares it
                             against the real validator rather than merely running it.

  README.md                  must name every file beside it. A vendorable file nobody
                             is told about is not vendorable in any useful sense.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
VENDOR = REPO / "vendor"
VOCAB = REPO / "src" / "include" / "duck_block_vocabulary.hpp"

PROBE = """#include "duck_block_normalize.hpp"
int main() {
    duckdb::vector<duckdb::Value> v;
    duckdb::duck_block::CollapseLonePlainIntoParent(v, duckdb::LogicalType::VARCHAR);
    return (int)v.size();
}
"""

THIRD_PARTY = [
    "fmt/include",
    "hyperloglob",
    "hyperloglog",
    "fastpforlib",
    "skiplist",
    "fast_float",
    "re2",
    "miniz",
    "utf8proc/include",
    "concurrentqueue",
    "pcg",
    "tdigest",
    "mbedtls/include",
    "jaro_winkler",
    "yyjson/include",
]


def skip(reason: str) -> int:
    if os.environ.get("DUCK_BLOCK_CHECKS_STRICT") == "1":
        print(f"FAIL: {reason}")
        print("      DUCK_BLOCK_CHECKS_STRICT=1 is set, so a skipped check is a failed check.")
        return 1
    print(f"SKIP: {reason}")
    return 0


def main() -> int:
    if not VENDOR.is_dir():
        print("FAIL: vendor/ is missing -- it is what consumers copy from.")
        return 1

    readme = VENDOR / "README.md"
    if not readme.exists():
        print("FAIL: vendor/README.md is missing.")
        print("      A vendorable file nobody is told about is not vendorable in any")
        print("      useful sense -- the whole failure was consumers not knowing what")
        print("      they could take.")
        return 1

    text = readme.read_text()
    undocumented = [f.name for f in sorted(VENDOR.iterdir()) if f.name != "README.md" and f.name not in text]
    if undocumented:
        print("FAIL: vendor/ holds files the README does not name:")
        for n in undocumented:
            print(f"        {n}")
        print("      Name it and say what it needs, or move it out of vendor/.")
        return 1
    print(f"Checking vendor/ ({len(list(VENDOR.iterdir())) - 1} files, all named in README.md)")

    cxx = shutil.which("g++") or shutil.which("clang++")
    if cxx is None:
        return skip("no C++ compiler, cannot probe the header as a consumer would")
    duckdb_inc = REPO / "duckdb" / "src" / "include"
    if not duckdb_inc.is_dir():
        return skip("no duckdb source tree, cannot probe the header as a consumer would")

    hdr = VENDOR / "duck_block_normalize.hpp"
    if not hdr.exists():
        print("FAIL: vendor/duck_block_normalize.hpp is missing.")
        return 1

    with tempfile.TemporaryDirectory() as td:
        inc = Path(td) / "inc"
        inc.mkdir()
        # ONLY these two. No src/include on the path -- that is the whole point.
        shutil.copy(hdr, inc / hdr.name)
        shutil.copy(VOCAB, inc / VOCAB.name)
        probe = Path(td) / "probe.cpp"
        probe.write_text(PROBE)
        args = [cxx, "-std=c++11", "-fsyntax-only", f"-I{inc}", f"-I{duckdb_inc}"]
        tp = REPO / "duckdb" / "third_party"
        args += [f"-I{tp / d}" for d in THIRD_PARTY if (tp / d).is_dir()]
        args.append(str(probe))
        proc = subprocess.run(args, capture_output=True, text=True)
        if proc.returncode != 0:
            print("\nFAIL: duck_block_normalize.hpp does not compile as a vendored file.")
            print("      It was compiled with ONLY itself, duck_block_vocabulary.hpp and DuckDB")
            print("      on the include path -- which is what a consumer has. An include of")
            print("      block_types.hpp or anything else from this repo fails here.")
            print("      " + "\n      ".join(proc.stderr.strip().splitlines()[:6]))
            return 1

    print("  duck_block_normalize.hpp compiles with only the vocabulary header + DuckDB")
    print("OK: vendor/ is consumable as copied files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
