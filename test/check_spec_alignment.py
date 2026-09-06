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
    in_type_table = False
    in_block_table = False
    null_levels = []
    for line in SPEC.read_text().splitlines():
        # The TYPE tables have five columns (Type, Description, level, encoding,
        # attributes). Other tables in this document also lead with a backticked
        # lowercase name -- the `metadata` role table, for one -- and matching on the
        # first column alone read those as element types, reporting `frontmatter` as a
        # type the build was missing.
        #
        # Narrowing the pattern rather than adding `frontmatter` to an exclusion list,
        # deliberately: an exclusion would have suppressed a real signal (a genuinely
        # undocumented type named the same as a role) and would have needed its own
        # expiry audit. The heuristic was wrong; the fix is to make it right.
        #
        # A column count was the first attempt and was also wrong -- the block table has
        # five columns, the inline and value tables four -- so it silently dropped 23 of
        # the 43 types and reported them as undocumented. Tracking the HEADER is exact:
        # every type table opens `| Type | ...` and no other table in the document does.
        if line.startswith("| Type |"):
            in_type_table = True
            # Only the block table has a `level` column; it is the third cell.
            in_block_table = "level" in line
            continue
        if not line.startswith("|"):
            in_type_table = False
            in_block_table = False
        if not in_type_table:
            continue
        m = re.match(r"\|\s*`([a-z_]+)`\s*\|", line)
        if m:
            documented.add(m.group(1))
            # The table and the validator are two encodings of the level rule, and
            # nothing compared them: eight rows said `NULL` for months after
            # duck_blocks_validate started rejecting a NULL level, so a producer
            # following the table emitted blocks the validator refused. Found by
            # panduck on 2026-09-04, whose page_break reader met the validator by
            # accident and the table not at all.
            cells = [c.strip() for c in line.split("|")]
            if in_block_table and len(cells) > 4 and cells[3].upper() == "NULL":
                null_levels.append(m.group(1))
    # KINDS and ENCODINGS are exclusions too, and they expire the same two ways as any
    # other -- pointed out by duckdb_markdown finding both modes in their own allowlist.
    #
    #   stale       a name that is no longer a kind or an encoding subtracts nothing
    #   superseded  a name that BECOMES an element_type is stripped here before the
    #               comparison, so a genuinely undocumented type would be silently
    #               excused. That is not hypothetical: `text` is both an encoding and
    #               a real inline element_type, and the first version of this file
    #               excluded it and reported a false failure.
    #
    # So check them against what the build actually declares rather than trusting the
    # literals to have aged well.
    kind_names = {
        line.strip()
        for line in subprocess.run(
            [str(duckdb), "-noheader", "-list", "-c", "SELECT unnest(duck_block_kind_names());"],
            capture_output=True,
            text=True,
        ).stdout.splitlines()
        if line.strip()
    }
    if kind_names and KINDS != kind_names:
        print(f"\nFAIL: KINDS is {sorted(KINDS)}; the build declares {sorted(kind_names)}.")
        print("      Update the literal. An exclusion naming something the build no longer")
        print("      has excuses nothing and looks healthy.")
        return 1
    shadowed = sorted((KINDS | ENCODINGS) & actual)
    if shadowed:
        print(f"\nFAIL: {shadowed} are excluded here but ARE real element types in the build.")
        print("      This strips them before the comparison, so their documentation is never")
        print("      checked -- an exclusion suppressing the evidence that would retire it.")
        print("      `text` is the known case: both an encoding and an inline element_type.")
        return 1

    if null_levels:
        print(f"\nFAIL: the block table gives {null_levels} a NULL level.")
        print("      Every element carries an explicit level and duck_blocks_validate rejects")
        print("      NULL, so the table is telling producers to emit what the validator refuses.")
        print("      Write the depth rule in the cell (`depth (top level 1)`).")
        return 1

    documented -= KINDS | ENCODINGS

    undocumented = sorted(actual - documented)
    stale = sorted(documented - actual)

    print(f"Checking {SPEC.relative_to(REPO)} against duck_block_type_names()")
    print(f"  build reports {len(actual)} element types; spec documents {len(documented)}")

    failed = False

    # THE DOCUMENT MUST NOT NAME A SPEC VERSION. Teague's instruction, and it replaces
    # the check that stood here until now -- which asserted the doc's `**Version:**`
    # header EQUALS duck_block_spec_version().
    #
    # That check was right for as long as the doc carried a version. It does not any
    # more, so its condition is gone and the entry is REPLACED rather than reworded --
    # the rule this repo applies to every stale exclusion, applied to a check of its own
    # from four hours ago.
    #
    # The inverse is the useful assertion now. A number written in prose cannot be
    # regenerated and goes stale silently: this document's header read `0.4.0` for eight
    # months while the shipped value moved five majors, so anyone reading it to decide
    # what to implement was reading a number no code had produced. `SPEC_VERSION` is the
    # one place a version belongs, because the build produces it.
    #
    # Prose only -- `duck_block_spec_version()` is a function name, and
    # `"pandoc-api-version":[1,23,1]` is Pandoc's, not ours.
    shipped = (
        subprocess.run(
            [str(duckdb), "-noheader", "-list", "-c", "SELECT duck_block_spec_version();"],
            capture_output=True,
            text=True,
        ).stdout.strip()
        or "<unreadable>"
    )
    text = SPEC.read_text()
    named = []
    for pat, what in (
        (r"^\*\*Version:\*\*", "a `**Version:**` header"),
        (r"\b[Ss]pec(?:ification)? \d+\.\d+", "prose naming a spec version"),
        (r"\bas of \d+\.\d+", "prose naming a spec version"),
        (r"\bsince (?:spec )?\d+\.\d+", "prose naming a spec version"),
        (r"\b\d+\.\d+ ->", "a version-to-version change note"),
    ):
        for m in re.finditer(pat, text, re.M):
            line = text[: m.start()].count("\n") + 1
            named.append((line, what, text.splitlines()[line - 1].strip()[:78]))
    if named:
        print("\nFAIL: the spec document names a spec version. It must not.")
        for line, what, ctx in named:
            print(f"      line {line}: {what}")
            print(f"        {ctx}")
        print("      A number in prose cannot be regenerated and goes stale silently --")
        print("      this header read 0.4.0 while the build shipped five majors later.")
        print("      The version belongs beside SPEC_VERSION in the vocabulary header,")
        print("      where the build produces it. Describe the rule, not when it landed.")
        return 1
    print(f"  the document names no spec version (the build says {shipped})")

    # THE ENCODING TABLE, same comparison as the type tables. Added because the spec
    # says of that table "make check fails if the two disagree" -- and until this
    # existed, it did not. A document asserting a guard that is not there is worse than
    # one that says nothing: a reader stops checking, on the strength of the claim.
    enc_build = {
        line.strip()
        for line in subprocess.run(
            [str(duckdb), "-noheader", "-list", "-c", "SELECT unnest(duck_block_encoding_names());"],
            capture_output=True,
            text=True,
        ).stdout.splitlines()
        if line.strip()
    }
    enc_doc = set()
    in_enc = False
    for line in text.splitlines():
        if line.startswith("| Encoding |"):
            in_enc = True
            continue
        if not line.startswith("|"):
            in_enc = False
        if not in_enc:
            continue
        m = re.match(r"\|\s*`([a-z_]+)`\s*\|", line)
        if m:
            enc_doc.add(m.group(1))
    if enc_build and enc_doc != enc_build:
        print("\nFAIL: the spec's encoding table disagrees with duck_block_encoding_names().")
        if enc_build - enc_doc:
            print(f"      declared by the build, undocumented: {sorted(enc_build - enc_doc)}")
        if enc_doc - enc_build:
            print(f"      documented, not declared by the build: {sorted(enc_doc - enc_build)}")
        return 1
    print(f"  the encoding table matches the build ({len(enc_build)} values)")
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
