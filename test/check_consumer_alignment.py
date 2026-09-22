#!/usr/bin/env python3
"""Are the consuming extensions actually aligned with the vocabulary they vendor?

This repo owns the format. Every other check here verifies THIS build against ITSELF
-- the spec against the constants, the SQL against the validator, the docs against the
functions. None of them can see whether the four extensions that implement the format
agree with it, and that is the only property the format exists to provide.

So this reads each consumer's vendored `duck_block_vocabulary.hpp` and compares it
against canonical BY NAME AND VALUE. Not by text diff: a cosmetic rewrite (this repo
once changed every `idx_t` to `uint64_t`) would fire a diff and change nothing, and a
check that cries wolf on cosmetics is muted before it catches anything real.

THE THREE DRIFTS, in ascending order of how badly they fail:

  MISSING     the consumer vendored before a constant existed. Compiles; they simply
              cannot reference what they do not have. Loud at their build if they try.
  EXTRA       a constant this repo has removed or renamed. Their code still compiles
              against a name the format no longer has.
  VALUE       the same name, a different string. THE DANGEROUS ONE. Compiles clean
              everywhere, every test on both sides passes, and the consumer's writer
              silently stops matching. Nothing in C++ catches it -- not vendoring, not
              a submodule pin. Only this comparison does.

Skips when a consumer's checkout is not present, which is the normal case in CI. A
skip says SKIP, never OK -- the absence of a repo is not evidence of alignment.
"""

import os
import pathlib
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
CANON = REPO / "src" / "include" / "duck_block_vocabulary.hpp"

# Consumers that vendor the header. Paths are this machine's layout; a consumer
# checked out elsewhere simply skips.
CONSUMERS = {
    "duckdb_markdown": "src/include/duck_block_vocabulary.hpp",
    "duckdb_panduck": "src/include/duck_block_vocabulary.hpp",
    "duckdb_webbed": "src/include/duck_block_vocabulary.hpp",
    "sitting_duck": "third_party/duck_block_utils/duck_block_vocabulary.hpp",
}
# Sibling checkouts live beside this repo. Overridable so the check can be VERIFIED
# against a synthetic consumer -- perturbing a real peer's working tree to test my own
# instrument would be editing someone else's session out from under them, and a guard
# that needs that to prove itself is not one I would run.
SEARCH_ROOTS = (
    [Path(os.environ["DUCK_BLOCK_CONSUMER_ROOT"])] if os.environ.get("DUCK_BLOCK_CONSUMER_ROOT") else [REPO.parent]
)

# String AND integer constants. The integers are the struct field offsets (KIND_IDX ..
# FILENAME_IDX): a consumer whose copy has a stale offset reads the wrong struct field while
# every string constant matches, which is the silent value change this check exists for.
# markdown noticed its parser counted 96 on the same header where this one counted 88
# (2026-09-15); the 8 missing names were exactly the offsets.
CONST = r'static constexpr (?:const char \*|u?int(?:8|16|32|64)_t |idx_t )([A-Z_]+) = (?:"([^"]*)"|([0-9]+));'
VERSION = r'SPEC_VERSION = "([^"]*)"'
# The provenance stamp the header's own re-vendoring guidance asks for, in the two
# forms the fleet has used: "Vendored at upstream commit: <sha> (SPEC_VERSION x.y)"
# (markdown, panduck, webbed; trailing text allowed) and the older
# "VENDORED from duckdb_duck_block_utils@<sha>" (sitting_duck before #122).
PROVENANCE = (
    r"(?i)vendored (?:at upstream commit:?|from duckdb_duck_block_utils@)\s*([0-9a-f]{7,40})"
    r"(?:[^\n(]*\([^)\n]*?SPEC_VERSION\s+([0-9.]+)\))?"
)
STAMP_WORDS = re.compile(r"(?i)vendored (?:at upstream commit|from duckdb_duck_block_utils)")


def constants(text):
    return {name: (sval if ival == "" else ival) for name, sval, ival in re.findall(CONST, text)}


def spec_version(text):
    m = re.search(VERSION, text)
    return m.group(1) if m else None


def provenance(text):
    """(sha, claimed_version) from the vendored copy's stamp, or (None, None)."""
    m = re.search(PROVENANCE, text)
    return (m.group(1), m.group(2)) if m else (None, None)


def stamp_line(text):
    """The whole line the stamp sits on, or None. The stamp is a SINGLE LINE by
    construction, so the line is the unit to judge -- not the regex match, which stops at
    the version and cannot see that the rest of the line was carried away."""
    m = re.search(PROVENANCE, text)
    if m is None:
        return None
    start = text.rfind("\n", 0, m.start()) + 1
    end = text.find("\n", m.start())
    return text[start:] if end == -1 else text[start:end]


def unclosed_delimiter(line):
    """The first delimiter pair the line opens and does not close, or None.

    THIS IS THE SEVERED-STAMP TEST, and it is deliberately not a canonical-form match.
    Trailing annotation is LEGITIMATE and the fleet uses it: sitting_duck's stamp ends
    ", synced 2026-09-14." and webbed's carries a 20-line provenance block around it. What
    is not legitimate is a line that opens a bracket whose other half is on the next line,
    which is exactly what a formatter reflow produces -- panduck's read
    "... (SPEC_VERSION 1.4)  [duck_block_utils" with "// v3.3.0]" wrapped below (2026-09-21).
    Balance separates the annotated from the truncated; "ends after the version" would
    reject sitting_duck, a conforming copy, to catch panduck, a damaged one."""
    for opener, closer in (("(", ")"), ("[", "]"), ("{", "}")):
        if line.count(opener) != line.count(closer):
            return opener + closer
    return None


def body(text):
    """The header from its own title line onward: what every vendored copy shares once its
    preamble (a stamp line, or webbed's 20-line provenance block, which has a `// ====`
    rule of its own -- anchoring on the first rule read webbed's copy as stale) is dropped."""
    m = re.search(r"^// The duck_block vocabulary -- PUBLISHED INTERFACE\.\s*$", text, re.M)
    return text[m.start() :] if m else text


def header_at(sha):
    """This repo's header at `sha`, or None if the sha is not a commit in this checkout.

    Read from the OWNER's history, which is the point of doing this here as well as in a
    consumer's own check: a consumer without markdown's provenance check (sitting_duck
    dropped its stamp in two consecutive syncs, #122 and #126, with the body byte-identical
    to upstream, so no constant comparison could see it) is still caught by the repo that
    owns the format. Resolved strictly first (`rev-parse --verify <sha>^{commit}`), so an
    ambiguous or unknown short sha fails cleanly and offline; a squash can leave a cited
    sha outside main's history, and that is reported as such, not as drift.
    """
    try:
        rp = subprocess.run(
            ["git", "-C", str(REPO), "rev-parse", "--verify", "--quiet", f"{sha}^{{commit}}"],
            capture_output=True,
            text=True,
        )
        if rp.returncode:
            return None
        # BYTES, not text=True: universal newlines would turn CRLF into LF before the
        # comparison and make "byte-exact below the title line" false on real files
        # (panduck measured a CRLF copy passing, 2026-09-15).
        out = subprocess.run(
            ["git", "-C", str(REPO), "show", f"{rp.stdout.strip()}:src/include/duck_block_vocabulary.hpp"],
            capture_output=True,
        )
        return out.stdout.decode("utf-8", errors="replace") if out.returncode == 0 else None
    except OSError:
        return None


def provenance_problems(text, got, v, superseded):
    """The stamp's refused cases, owner-side: absent; malformed; claimed version differs
    from the file (the renumbering hatch excepted); sha unknown here; header at the sha
    differs from the copy (STALE STAMP -- compared as TEXT from the body onward, because a
    comment-only edit is exactly what a name-and-value comparison cannot see)."""
    sha, claimed = provenance(text)
    line = stamp_line(text)
    if line is not None:
        pair = unclosed_delimiter(line)
        if pair is not None:
            return [
                f"provenance stamp is TRUNCATED: its line opens `{pair[0]}` and never closes it,"
                f" so the stamp was wrapped or cut -- a formatter sweep reflowing a vendored file"
                f" does exactly this (panduck, 2026-09-21). A stamp exists to be compared"
                f" byte-exactly; recovering a sha from a line whose own structure shows it is"
                f" incomplete is a way of not checking it. The line: {line.strip()[:100]}"
            ]
    if sha is None:
        if STAMP_WORDS.search(text):
            return [
                "provenance stamp is present but malformed (a half-edited re-vendor?);"
                " expected '// Vendored at upstream commit: <sha> (SPEC_VERSION <x.y>)'"
            ]
        return [
            "no provenance stamp -- the copy must carry"
            " '// Vendored at upstream commit: <sha> (SPEC_VERSION <x.y>)'"
            " (step 1 of the header's own re-vendoring guidance); a re-vendor that"
            " drops it is how sitting_duck #122 and #126 shipped bare copies"
        ]
    problems = []
    if claimed is None and re.search(r"SPEC_VERSION", text.split("\n")[text[: text.find(sha)].count("\n")]):
        # The stamp line mentions SPEC_VERSION but not in a parenthesised group this
        # parser reads (e.g. "(2026-09-14) (SPEC_VERSION 1.4)"): say so rather than
        # silently skipping the version comparison.
        problems.append(
            f"stamp names {sha} and mentions SPEC_VERSION, but the claimed version is unreadable to this check"
        )
    if claimed and v and claimed != v:
        # A stamp written on the retired internal line (6.x) against a file on the
        # public line is the renumbering, not a mismatch -- SPEC_VERSION_SUPERSEDES.
        renumbered = bool(superseded and claimed.split(".")[0] == superseded.split(".")[0])
        if not renumbered:
            problems.append(
                f"stamp claims SPEC_VERSION {claimed}, the file declares {v}: stamp not updated with the copy"
            )
    at = header_at(sha)
    if at is None:
        problems.append(f"stamp names {sha}, which is not a commit in this checkout (a squash? fetch first)")
        return problems
    if body(at) != body(text):
        theirs = constants(at)
        diff = sorted(k for k in set(theirs) | set(got) if theirs.get(k) != got.get(k))
        what = f"constants differ: {', '.join(diff[:6])}" if diff else "constants agree; comments or prose differ"
        problems.append(
            f"STALE STAMP: the header at {sha} does not match this copy ({what}):"
            " the copy was edited after vendoring, or the stamp names the wrong commit"
        )
    return problems


def find(name, rel):
    for root in SEARCH_ROOTS:
        p = root / name / rel
        if p.exists():
            return p
    return None


def repo_ref(repo_root):
    """branch@sha of a consumer checkout, or None.

    THE SUBJECT OF THIS CHECK IS A WORKING TREE, not a project. webbed reported as
    out of alignment on 2026-09-01 because their checkout was sitting on a docs
    branch where the vendored header does not exist -- the vendoring lives on
    `duck_block_bump`. The measurement was correct about the directory and wrong
    about the thing anyone cared about, and a red that names no ref cannot be told
    apart from a real disagreement.
    """
    try:
        br = subprocess.run(["git", "-C", str(repo_root), "branch", "--show-current"], capture_output=True, text=True)
        sha = subprocess.run(
            ["git", "-C", str(repo_root), "rev-parse", "--short", "HEAD"], capture_output=True, text=True
        )
        if br.returncode or sha.returncode:
            return None
        return f"{br.stdout.strip() or 'DETACHED'}@{sha.stdout.strip()}"
    except OSError:
        return None


def branches_with(repo_root, rel):
    """Local branches whose tree contains `rel`, so an absence can name where it lives."""
    try:
        out = subprocess.run(
            ["git", "-C", str(repo_root), "for-each-ref", "--format=%(refname:short)", "refs/heads"],
            capture_output=True,
            text=True,
        )
        if out.returncode:
            return []
        found = []
        for br in out.stdout.split():
            ls = subprocess.run(
                ["git", "-C", str(repo_root), "ls-tree", "-r", "--name-only", br], capture_output=True, text=True
            )
            if ls.returncode == 0 and any(line.endswith(rel) for line in ls.stdout.splitlines()):
                found.append(br)
        return found
    except OSError:
        return []


def main() -> int:
    if not CANON.exists():
        print("FAIL: canonical vocabulary header missing.")
        return 1
    canon_text = CANON.read_text()
    canon = constants(canon_text)
    canon_v = spec_version(canon_text)
    print(f"Checking consumer alignment against {len(canon)} constants, SPEC_VERSION {canon_v}")

    drifted = []
    behind_names = []
    checked = 0
    for name, rel in sorted(CONSUMERS.items()):
        path = find(name, rel)
        # The checkout the header was FOUND in, not REPO.parent: with DUCK_BLOCK_CONSUMER_ROOT
        # set, REPO.parent / name is a different working tree, so the label named the sibling
        # checkout while the constants came from the override (a generated fixture labelled
        # main@12547c8; a fresh clone labelled None). Same lookup as the shadow scan below.
        root = next((r / name for r in SEARCH_ROOTS if (r / name).exists()), None)
        ref = repo_ref(root) if root else None
        if path is None:
            where = branches_with(root, pathlib.Path(rel).name) if root else []
            if where:
                print(f"  SKIP {name} [{ref}] -- vendored header not on this branch;" f" it is on {', '.join(where)}")
                print("       Checked out elsewhere is not drift. Re-run with that branch checked out.")
            else:
                print(f"  SKIP {name} -- no checkout here (absence is not alignment)")
            continue
        checked += 1
        text = path.read_bytes().decode("utf-8", errors="replace")  # bytes: keep CR, see header_at
        got = constants(text)
        v = spec_version(text)

        missing = sorted(set(canon) - set(got))
        extra = sorted(set(got) - set(canon))
        changed = sorted(k for k in set(canon) & set(got) if canon[k] != got[k])

        # A RENUMBERING is not a value change. When the canonical header names a
        # superseded line (SPEC_VERSION_SUPERSEDES), a consumer whose SPEC_VERSION is
        # on that line is behind the renumbering, not carrying a different value:
        # the shape is identical on both sides. Report it in those words, because a
        # major-equality check reads 6 -> 1 as breaking, which is the opposite of
        # what happened.
        superseded = canon.get("SPEC_VERSION_SUPERSEDES")
        on_old_line = bool(superseded and v and v.split(".")[0] == superseded.split(".")[0])
        if on_old_line:
            changed = [k for k in changed if k != "SPEC_VERSION"]
            missing = [k for k in missing if k != "SPEC_VERSION_SUPERSEDES"]

        prov = provenance_problems(text, got, v, canon.get("SPEC_VERSION_SUPERSEDES"))

        # MAJOR EQUALITY AND A MINOR FLOOR, the contract the header states. A consumer on
        # the same major and an OLDER minor that differs only by constants added upstream
        # is BEHIND, not drifted: it compiles, it reads every struct field correctly, and it
        # re-vendors when it needs something new. Exact equality turned every additive
        # minor into a fleet-wide red that sessions read as an instruction to re-vendor
        # (Teague's ruling, 2026-09-15: "reduce the churn going forward"). SPEC_VERSION and
        # PREDICATE_REVISION legitimately differ on a behind copy; any OTHER changed value,
        # an extra name, a minor AHEAD, or a provenance problem still fails.
        def ver(x):
            try:
                a, b = (x or "").split(".")[:2]
                return int(a), int(b)
            except ValueError:
                return None

        cv, gv = ver(canon_v), ver(v)
        # "Behind" includes the SAME minor when the only difference is constants canonical has
        # and the copy lacks. Spec releases are batched (Teague, 2026-09-15): constants land on
        # main before SPEC_VERSION moves, so an up-to-date 1.4 copy compared against main that
        # already carries an unreleased constant must not go red. A copy that CLAIMS a minor
        # but lacks a constant that minor released is still caught: its provenance stamp names
        # a sha whose header does not match it (STALE STAMP).
        behind = bool(
            not on_old_line and cv and gv and gv[0] == cv[0] and gv[1] <= cv[1] and (missing or gv[1] < cv[1])
        )
        if behind:
            changed = [k for k in changed if k not in ("SPEC_VERSION", "PREDICATE_REVISION")]
            if not (extra or changed or prov):
                sha, _ = provenance(text)
                print(
                    f"  BEHIND {name} [{ref}] -- SPEC_VERSION {v} vs {canon_v}, {len(got)} constants,"
                    f" vendored at {sha}; missing {len(missing)} additive constant(s)"
                    f"{': ' + ', '.join(missing) if missing else ''}. Passes: same major, minor floor."
                )
                behind_names.append(name)
                continue

        if not (missing or extra or changed or prov) and v == canon_v:
            sha, _ = provenance(text)
            print(f"  OK   {name} [{ref}] -- {len(got)} constants, SPEC_VERSION {v}, vendored at {sha}")
            continue

        drifted.append(name)
        print(f"\n  DRIFT {name} -- SPEC_VERSION {v}, {len(got)} constants")
        for pr in prov:
            print(f"        PROVENANCE: {pr}")
        if on_old_line:
            print(f"        version {v} is on the retired internal line; canonical is {canon_v}, the SAME shape")
            print(f"        renumbered (header: 6.6 -> 1.2, no change). Re-vendor and set your")
            print(f"        major-equality constant to {canon_v.split('.')[0]} once; nothing else moves.")
        elif v != canon_v:
            if cv and gv and gv[0] == cv[0] and gv[1] > cv[1]:
                print(f"        version {v} is AHEAD of canonical {canon_v}: a copy newer than its owner")
            elif cv and gv and gv[0] != cv[0]:
                print(f"        MAJOR {gv[0]} against canonical {cv[0]}: a breaking change this copy has not taken")
            else:
                print(f"        version {v} against canonical {canon_v}")
        if changed:
            print(f"        VALUE CHANGED ({len(changed)}) -- compiles clean on both sides,")
            print("        every test passes, and their writer silently stops matching:")
            for k in changed:
                print(f"          {k}: theirs {got[k]!r}, canonical {canon[k]!r}")
        if missing:
            print(f"        missing ({len(missing)}): {', '.join(missing)}")
            print("        -- vendored before these existed; re-pull the header")
        if extra:
            print(f"        extra ({len(extra)}): {', '.join(extra)}")
            print("        -- names this format no longer has")

    # SHADOWED CONSTANTS -- a second declaration where the check does not look.
    #
    # The comparison above reads the VENDORED header, and a consumer's vendored header
    # can be perfectly correct while their code never uses it: a subclass that inherits
    # the vocabulary and REDECLARES a name silently wins. Legal C++ name-hiding, clean
    # build, no warning, and every `Theirs::ENCODING_JSON` reads as a use of the shared
    # vocabulary while resolving to the local copy.
    #
    # webbed raised it after inheriting ATTR_ROLE alongside their own and watching it
    # build clean. panduck then found SIX in their own tree -- ATTR_HEADING_LEVEL and
    # five ENCODING_* -- all byte-identical, so nothing had broken yet and nothing
    # would have said so if one had.
    #
    # panduck's own summary is the shape worth naming: A CHECK THAT EXAMINES THE RIGHT
    # FILE CAN BE DEFEATED BY A SECOND DECLARATION SOMEWHERE IT DOES NOT LOOK. Their
    # suggested signal needs no C++ parsing -- a name in a consumer header that is not
    # the vendored copy, which also exists in canonical -- so that is what this does.
    for name, rel in sorted(CONSUMERS.items()):
        root = None
        for r in SEARCH_ROOTS:
            if (r / name).is_dir():
                root = r / name
                break
        if root is None:
            continue
        vendored = (root / rel).resolve()
        # A SHADOW IS ONLY A SHADOW IF THE VENDORED HEADER IS THERE TO BE SHADOWED.
        # With it absent, every constant in the consumer's own headers looks like a
        # redeclaration of it -- which is how webbed's PRE-vendoring duck_block_types.hpp
        # reported 34 shadows while sitting on a docs branch. Unmeasured, not clean, and
        # not a failure: reported so the difference is visible.
        if not vendored.exists():
            ref = repo_ref(root)
            print(f"\n  UNMEASURED {name} [{ref}] -- shadow scan needs the vendored header,")
            print("        and this branch does not have it. A consumer's own headers are not")
            print("        shadows of something that is not there.")
            continue
        shadows = []
        for hdr in sorted(root.rglob("*.hpp")):
            if hdr.resolve() == vendored or "/duckdb/" in str(hdr):
                continue
            # A COPY of the vendored header is not a shadow, and neither is anything inside a
            # nested git worktree or submodule: sitting_duck keeps worktrees under trees/, and
            # the scan walked into one and counted a whole second vendored header as 95
            # "redeclarations" (2026-09-15). A shadow is a declaration in the consumer's OWN
            # code that competes with the vendored copy.
            if hdr.name == vendored.name:
                continue
            if any((d / ".git").exists() for d in hdr.relative_to(root).parents if str(d) != "." for d in [root / d]):
                continue
            local = constants(hdr.read_text(errors="ignore"))
            for k in sorted(set(local) & set(canon)):
                shadows.append((hdr.relative_to(root), k, local[k], canon[k]))
        if shadows:
            drifted.append(f"{name} (shadowed)")
            print(f"\n  SHADOWED {name} -- {len(shadows)} constant(s) redeclared outside the vendored header")
            for rel_p, k, theirs, mine in shadows:
                verdict = "same value" if theirs == mine else f"VALUE DIFFERS: {theirs!r} vs {mine!r}"
                print(f"        {rel_p}: {k}  ({verdict})")
            print("        A local declaration WINS silently. Verify each value is byte-identical")
            print("        BEFORE deleting -- deleting first turns a value difference into an")
            print("        unexplained behaviour change several commits later, with the deletion")
            print("        no longer an obvious suspect.")

    if not checked:
        print("  no consumer checkouts found -- nothing verified")
        return 0
    if drifted:
        print(f"\nFAIL: {len(drifted)} consumer(s) out of alignment: {', '.join(drifted)}")
        print("      This repo owns the format; a consumer disagreeing with it is the")
        print("      failure the vocabulary exists to prevent, not their local problem.")
        return 1
    if behind_names:
        print(
            f"OK: {checked} consumer checkout(s) pass; {len(behind_names)} BEHIND on an older minor"
            f" ({', '.join(behind_names)}), which is allowed: re-vendor when a newer constant is needed."
        )
        return 0
    print(f"OK: all {checked} consumer checkouts agree by name and value.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
