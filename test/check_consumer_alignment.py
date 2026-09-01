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
import re
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
SEARCH_ROOTS = [Path(os.environ["DUCK_BLOCK_CONSUMER_ROOT"])] if os.environ.get(
    "DUCK_BLOCK_CONSUMER_ROOT") else [REPO.parent]

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
        if path is None:
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
            print(f"  OK   {name} -- {len(got)} constants, SPEC_VERSION {v}")
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
