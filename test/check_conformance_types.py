#!/usr/bin/env python3
"""The vocabulary header and the conformance macros must declare the same names.

WHY THIS EXISTS, given that check_conformance_macro.py already compares them.

That guard is better than this one: it runs the real extension against the real
macros and so catches RULE divergence, not just name divergence. But it needs a
built extension, which means it cannot run on a bare runner -- and it is one of
the ten guards issue #58 reports as never running in CI. This one needs no build,
no network and no DuckDB: it is two text files and a set comparison, so there is
no reason for it not to run on every push.

WHAT IT WOULD HAVE CAUGHT. `TYPE_DOCUMENT = "document"` was added to the header
in the 1.4 amendment of 2026-09-16 (v3.4.0) and never added to
`duck_block_declared_types()`. Three releases shipped with the two artifacts
disagreeing: a producer emitting the document root the spec had just legalised was
reported by `duck_blocks_undeclared_types()` as using a name the vocabulary does
not declare. A consumer -- duckdb_markdown -- found it while re-vendoring, and the
remedy its guard printed ("re-vendor from upstream") could not fix it, because
upstream's copy was the stale one.

WHAT IT CANNOT SEE, stated so a clean run is not read as more than it is:

  - RULE divergence. The same amendment legalised `level = 0` for the document
    root, src/validation.cpp implemented it, and the macros still refused it with
    an unconditional `level >= 1`. That is a disagreement about *meaning* with no
    name missing on either side, and only behavioural comparison finds it.
    check_conformance_macro.py is that comparison; wire it.
  - Anything about predicates. The header's PREDICATE_REVISION exists precisely
    because a predicate's body cannot be compared by name and value.
  - Whether a declared name is CORRECT. Both files agreeing on a typo is a clean
    run here.

So: this guard proves the two lists are the same list. Nothing more.
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HEADER = REPO / "src" / "include" / "duck_block_vocabulary.hpp"
MACROS = REPO / "vendor" / "duck_block_conformance.sql"

# Element-type constants live under three prefixes, and all three land in the one
# `duck_block_declared_types()` list: block types, inline types, and the value-tree
# types a kind='value' row uses. Splitting them here and unioning is deliberate --
# it documents which families feed that list, and a new family would show up as a
# divergence rather than being silently folded in.
ELEMENT_PREFIXES = ("TYPE_", "INLINE_", "VALUE_")

# (header prefix(es), SQL macro name) for the three lists that can be compared by name.
COMPARISONS = (
    (ELEMENT_PREFIXES, "duck_block_declared_types"),
    (("KIND_",), "duck_block_declared_kinds"),
    (("ENCODING_",), "duck_block_declared_encodings"),
)


def header_values(code: str, prefixes) -> set:
    """Constant VALUES declared under any of `prefixes`, from comment-stripped code.

    STRIPPING COMMENTS IS LOAD-BEARING. The header's changelog contains lines like

        //     TYPE_PAGE = "page_break"   ->   TYPE_PAGE = "pagebreak"

    illustrating a hypothetical rename. A scan that reads comments picks up
    `pagebreak`, which is declared nowhere, and reports a divergence that does not
    exist -- measured: it did exactly that on the first attempt at this guard.
    """
    out = set()
    for prefix in prefixes:
        pattern = r"static constexpr const char \*" + prefix + r'[A-Z0-9_]+\s*=\s*"([a-z0-9_]+)"'
        out |= set(re.findall(pattern, code))
    return out


def strip_cpp_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def macro_list(sql: str, name: str) -> set:
    """The quoted names inside `CREATE OR REPLACE MACRO <name>() AS ( [...] )`.

    Anchored to the macro name and stopped at the first `]`, which is how the other
    checkers read this file too. Note the shared hazard: a `'` or `]` inside a
    COMMENT in that list would truncate the scrape, and truncation is silent and
    reassuring -- a shorter list has less to disagree about. duckdb_markdown hit
    this: a draft comment cut a 44-name list to 9. Hence the sanity floor below.
    """
    match = re.search(re.escape(name) + r"\(\)\s*AS\s*\(\s*\[(.*?)\]", sql, re.DOTALL)
    if match is None:
        raise LookupError(f"macro {name}() not found, or its list is not a `[...]` literal")
    return set(re.findall(r"'([a-z0-9_]+)'", match.group(1)))


def compare(code: str, sql: str) -> list:
    problems = []
    for prefixes, macro in COMPARISONS:
        declared = header_values(code, prefixes)
        listed = macro_list(sql, macro)

        # SANITY FLOOR. A scrape that silently truncated would otherwise report a
        # large "missing from SQL" set, which reads as drift rather than as a broken
        # parse. Fewer than three names on either side means the parse failed.
        if len(declared) < 3 or len(listed) < 3:
            problems.append(
                f"{macro}: parse looks broken -- header {len(declared)} name(s), "
                f"SQL {len(listed)}. Expected both well above 3; a truncated scrape "
                f"is silent, so this is reported as a failure rather than as drift."
            )
            continue

        for name in sorted(declared - listed):
            problems.append(
                f"{macro}: header declares {name!r} and the macro does not list it. "
                f"A producer using it is reported as undeclared."
            )
        for name in sorted(listed - declared):
            problems.append(
                f"{macro}: lists {name!r} with no constant of that value in the header "
                f"under {'/'.join(prefixes)}. Either the constant was removed or the "
                f"list has an invention."
            )
    return problems


def self_test() -> int:
    """Prove the comparison can FAIL, not merely that it passes today.

    A detector exercised only on the side expected to pass is half-checked, which is
    this repo's standing rule for guards. Both answers are pinned here in memory --
    no file is written.
    """
    code = strip_cpp_comments(HEADER.read_text(encoding="utf-8"))
    sql = MACROS.read_text(encoding="utf-8")

    clean = compare(code, sql)
    if clean:
        print("self-test: the UNMODIFIED pair already disagrees, so the mutations below")
        print("           prove nothing. Fix the real divergence first:")
        for problem in clean:
            print(f"  {problem}")
        return 1

    # Mutation 1: drop a name from the SQL list. This is the v3.4.0 defect's shape.
    dropped = sql.replace("'heading',", "", 1)
    if not any("'heading'" in p for p in compare(code, dropped)):
        print("self-test FAILED: removing 'heading' from the macro list was not detected")
        return 1

    # Mutation 2: add a name the header does not declare, i.e. the other direction.
    invented = sql.replace("'heading',", "'heading', 'nonesuch',", 1)
    if not any("nonesuch" in p for p in compare(code, invented)):
        print("self-test FAILED: an invented macro name was not detected")
        return 1

    # Mutation 3: truncate the scrape, which must read as a broken parse.
    truncated = re.sub(
        r"(duck_block_declared_types\(\)\s*AS\s*\(\s*\[)",
        r"\1 'blockquote' ]  -- [",
        sql,
        count=1,
    )
    if not any("parse looks broken" in p for p in compare(code, truncated)):
        print("self-test FAILED: a truncated list did not trip the sanity floor")
        return 1

    print("self-test: 3 mutations detected (missing name, invented name, truncated scrape)")
    return 0


def main() -> int:
    if "--self-test" in sys.argv:
        return self_test()

    for path in (HEADER, MACROS):
        if not path.exists():
            print(f"FAIL: {path.relative_to(REPO)} is missing")
            return 1

    code = strip_cpp_comments(HEADER.read_text(encoding="utf-8"))
    sql = MACROS.read_text(encoding="utf-8")
    problems = compare(code, sql)

    if problems:
        print("The vocabulary header and the conformance macros disagree:\n")
        for problem in problems:
            print(f"  {problem}")
        print(
            "\nBoth are published artifacts consumers copy, so a disagreement here " "ships to every consumer at once."
        )
        return 1

    total = sum(len(macro_list(sql, macro)) for _, macro in COMPARISONS)
    print(f"header and conformance macros agree on all {total} declared names")
    print("  (names only -- rule divergence needs check_conformance_macro.py, which needs a build)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
