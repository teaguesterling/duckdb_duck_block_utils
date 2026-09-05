#!/usr/bin/env python3
"""Assert src/include/duck_block_vocabulary.hpp stays consumable as a lone file.

Sibling extensions (panduck, webbed, duckdb_markdown, sitting_duck) vendor this
header as a single copied file -- see its "VENDORING THIS FILE" block. That only
works while the header is **link-free**: every declaration in it must have its
definition inline, or a consumer that includes it gets undefined-symbol errors
at link time for functions it never called.

Nothing in *this* repo would notice that regression -- we link block_types.cpp
anyway. So this compiles a probe the way a consumer would: the vocabulary header
alone, no other project header, and no linking against this extension.

Two checks:

  1. it compiles AND LINKS with no duckdb include path at all
  2. it declares nothing it does not define -- caught by grep, since a
     declaration-only member compiles fine here and only fails at a consumer's
     link step, which is precisely the failure this file exists to prevent
  3. it includes nothing but <cstdint> -- any DuckDB include would drag a 290 MB
     nested duckdb clone into every consumer's CI, because the extension CI
     templates check out with submodules: 'recursive' and this repo carries its
     own duckdb submodule

Exits 0 and skips when there is no compiler or no duckdb source tree.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
VOCAB = REPO / "src" / "include" / "duck_block_vocabulary.hpp"


def repo_duckdb():
    """The duckdb binary AND the extension beside it, or (None, None).

    Both come from the SAME build directory. The first version of this pinned the
    extension to build/release while letting the binary fall through to build/debug,
    so a debug-only checkout found no release extension and the whole comparison
    SKIPPED -- silently, and indistinguishably from passing. Found by control-testing
    the second candidate, which this repo never takes because build/release always
    exists. duckdb_markdown found the identical never-taken fallback in their own
    vendored-header lookup the same evening.
    """
    for candidate in ("release", "debug"):
        binary = REPO / "build" / candidate / "duckdb"
        ext = REPO / "build" / candidate / "extension" / "duck_block_utils" / "duck_block_utils.duckdb_extension"
        if binary.exists() and ext.exists():
            return binary, ext
    return None, None


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


def main() -> int:
    cxx = shutil.which("g++") or shutil.which("clang++")
    if cxx is None:
        return skip("no C++ compiler found")
    failed = False

    # 3. Nothing but <cstdint>. A DuckDB include here is not a style question:
    #    it is a 290 MB nested clone in four consumers' CI for a 12 KB header.
    includes = re.findall(r'^\s*#include\s+([<"][^>"]+[>"])', VOCAB.read_text(), re.M)
    stray = [i for i in includes if i != "<cstdint>"]
    if stray:
        failed = True
        print("FAIL: the vocabulary header must include nothing but <cstdint>:")
        for i in stray:
            print(f"        {i}")

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
            # No duckdb include path: a consumer must not need one.
            [cxx, "-std=c++17", "-o", str(Path(tmp) / "probe"), "-I", str(VOCAB.parent), str(probe)],
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            failed = True
            print("FAIL: the vocabulary header does not compile/link standalone:")
            print("\n".join(proc.stderr.splitlines()[:12]))

    # 3. Every type-ish constant the header DECLARES is enumerated by the build, and
    #    vice versa. This is the direction that catches adding a type to the header and
    #    forgetting AllTypeNames() -- which is exactly the change whose author is
    #    thinking about the new type rather than about the lists that enumerate them.
    #
    #    The pattern is ANCHORED at a real declaration and matches whole constant names.
    #    A loose `TYPE_[A-Z_]+` matched line 64's illustrative COMMENT about a value
    #    change and the suffix of every LIST_TYPE_* constant, and reported two defects
    #    that do not exist. A measurement can be wrong the same way a check can.
    duckdb, ext = repo_duckdb()
    if duckdb is None:
        print("  (skipping the build comparison: no duckdb binary)")
    else:
        pat = re.compile(r'^\s*static constexpr const char \*((?:TYPE|INLINE|VALUE)_[A-Z_]+)\s*=\s*"([^"]+)"')
        declared = {m.group(2) for m in (pat.match(l) for l in VOCAB.read_text().splitlines()) if m}
        proc = subprocess.run(
            [
                str(duckdb),
                "-unsigned",
                "-noheader",
                "-list",
                "-c",
                f"LOAD '{ext}'; SELECT unnest(duck_block_type_names());",
            ],
            capture_output=True,
            text=True,
        )
        enumerated = {x.strip() for x in proc.stdout.split() if x.strip()}
        if proc.returncode != 0 or not enumerated:
            failed = True
            print("FAIL: could not read duck_block_type_names() from the build, so this")
            print("      comparison reported nothing rather than agreement.")
        else:
            if declared - enumerated:
                failed = True
                print(f"FAIL: declared in the header, never enumerated: {sorted(declared - enumerated)}")
                print("      A consumer asking the build what types exist will not be told about")
                print("      them, so nothing downstream can branch on a type the header offers.")
            if enumerated - declared:
                failed = True
                print(f"FAIL: enumerated by the build, not declared in the header: {sorted(enumerated - declared)}")
                print("      A vendoring consumer has no constant for it and must use a literal.")
            # MULTIPLICITY, checked before any set comparison that would hide it. Five
            # names live on two constants each (code, generic, image, list, raw), so the
            # list enumerating CONSTANTS returned 47 rows for 43 types until c33a8b4.
            rows = [x.strip() for x in proc.stdout.split() if x.strip()]
            if len(rows) != len(enumerated):
                failed = True
                dupes = sorted({n for n in rows if rows.count(n) > 1})
                print(f"FAIL: duck_block_type_names() returns duplicates: {dupes}")
                print("      len() then reads the wrong vocabulary size and any join double-counts.")
            elif not failed:
                print(f"  {len(declared)} type names declared and enumerated, no duplicates")

    if failed:
        return 1
    print("OK: duck_block_vocabulary.hpp is freestanding -- <cstdint> only, no linking, no duckdb.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
