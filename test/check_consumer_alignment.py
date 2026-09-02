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
}
# Sibling checkouts live beside this repo. Overridable so the check can be VERIFIED
# against a synthetic consumer -- perturbing a real peer's working tree to test my own
# instrument would be editing someone else's session out from under them, and a guard
# that needs that to prove itself is not one I would run.
SEARCH_ROOTS = (
    [Path(os.environ["DUCK_BLOCK_CONSUMER_ROOT"])] if os.environ.get("DUCK_BLOCK_CONSUMER_ROOT") else [REPO.parent]
)

CONST = r'static constexpr const char \*([A-Z_]+) = "([^"]*)";'
VERSION = r'SPEC_VERSION = "([^"]*)"'


def constants(text):
    return dict(re.findall(CONST, text))


def spec_version(text):
    m = re.search(VERSION, text)
    return m.group(1) if m else None


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
        br = subprocess.run(["git", "-C", str(repo_root), "branch", "--show-current"],
                            capture_output=True, text=True)
        sha = subprocess.run(["git", "-C", str(repo_root), "rev-parse", "--short", "HEAD"],
                             capture_output=True, text=True)
        if br.returncode or sha.returncode:
            return None
        return f"{br.stdout.strip() or 'DETACHED'}@{sha.stdout.strip()}"
    except OSError:
        return None


def branches_with(repo_root, rel):
    """Local branches whose tree contains `rel`, so an absence can name where it lives."""
    try:
        out = subprocess.run(["git", "-C", str(repo_root), "for-each-ref",
                              "--format=%(refname:short)", "refs/heads"],
                             capture_output=True, text=True)
        if out.returncode:
            return []
        found = []
        for br in out.stdout.split():
            ls = subprocess.run(["git", "-C", str(repo_root), "ls-tree", "-r", "--name-only", br],
                                capture_output=True, text=True)
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
    checked = 0
    for name, rel in sorted(CONSUMERS.items()):
        path = find(name, rel)
        root = REPO.parent / name
        ref = repo_ref(root) if root.exists() else None
        if path is None:
            where = branches_with(root, pathlib.Path(rel).name) if root.exists() else []
            if where:
                print(f"  SKIP {name} [{ref}] -- vendored header not on this branch;"
                      f" it is on {', '.join(where)}")
                print("       Checked out elsewhere is not drift. Re-run with that branch checked out.")
            else:
                print(f"  SKIP {name} -- no checkout here (absence is not alignment)")
            continue
        checked += 1
        text = path.read_text()
        got = constants(text)
        v = spec_version(text)

        missing = sorted(set(canon) - set(got))
        extra = sorted(set(got) - set(canon))
        changed = sorted(k for k in set(canon) & set(got) if canon[k] != got[k])

        if not (missing or extra or changed) and v == canon_v:
            print(f"  OK   {name} [{ref}] -- {len(got)} constants, SPEC_VERSION {v}")
            continue

        drifted.append(name)
        print(f"\n  DRIFT {name} -- SPEC_VERSION {v}, {len(got)} constants")
        if v != canon_v:
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
            local = dict(re.findall(CONST, hdr.read_text(errors="ignore")))
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
    print(f"OK: all {checked} consumer checkouts agree by name and value.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
