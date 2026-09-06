#!/usr/bin/env python3
"""A constant this repo declares must not be shadowed by its own literal in src/.

duck_block_vocabulary.hpp is published so consumers can compare CONSTANTS by name and
value -- that comparison is the only thing that catches a changed value, since a rename
is a compile error and a value change is not.

But a constant only protects the code that USES it. On 2026-09-01 this repo declared
ATTR_ROLE, ROLE_FRONTMATTER and seven more, published them to four extensions, told a
consumer to "use the constant rather than the literal" -- and used NONE of them: 51
bare literals sat in src/ behind nine unused constants. A value change would then have
been caught in every consumer and not here, which is the wrong way round for the repo
that owns the value.

Declared-but-unused is the decorative-names failure one level up: the vocabulary had
43 type names that validated nothing until a lint was added, and then nine attribute
constants that guarded nothing until they were called.

So: for every string constant in the vocabulary header, its VALUE must not appear as a
bare string literal in src/*.cpp. Exemptions carry reasons and are audited -- an
exemption naming a constant that no longer exists is reported, because an exclusion
whose subject has gone excuses nothing.
"""

import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
VOCAB = REPO / "src" / "include" / "duck_block_vocabulary.hpp"
SRC = REPO / "src"

# constant name -> why its literal may still appear in src/
EXEMPT = {
    # Values that are also ordinary English words or JSON keys in unrelated positions.
    "ATTR_KEY": "'key' is also a yyjson object key and a MAP column name in contexts that "
    "have nothing to do with a duck_block attribute; converting those would be "
    "wrong rather than pedantic.",
    "ROLE_SECTION": "'section' is also the element_type TYPE_SECTION's own value, so the "
    "literal legitimately appears where the TYPE is meant.",
    "ROLE_HEADER": "'header' appears as a Pandoc/HTML construct name unrelated to the role.",
    "ROLE_PAGE": "'page' is the class written into exported Pandoc Divs for TYPE_PAGE, which "
    "is the marker's serialisation rather than a role attribute.",
    "ROLE_ARTICLE": "part of the section role set, matched via a list rather than singly.",
    "ROLE_ASIDE": "as ROLE_ARTICLE.",
    "ROLE_NAV": "as ROLE_ARTICLE.",
    "ROLE_FOOTER": "as ROLE_ARTICLE.",
    "ROLE_MAIN": "as ROLE_ARTICLE.",
}


# ROLE_DOCUMENT and ROLE_FRONTMATTER were EXEMPT here and are not any more. Both
# reasons still read as TRUE -- they are declared for producers, and this repo's Pandoc
# reader has no frontmatter concept because Pandoc parses frontmatter into Meta rather
# than leaving a blob. But neither was excusing anything: their literals appear nowhere
# in src/, so the shadowing scan never flagged them and the entries only looked like
# considered decisions. That is a GAP -- published upstream, nothing here branches on
# it -- and this check does not track gaps. Recording it here rather than as a
# suppression that would also swallow the real instance.


def skip(reason: str) -> int:
    if os.environ.get("DUCK_BLOCK_CHECKS_STRICT") == "1":
        print(f"FAIL: {reason}")
        return 1
    print(f"SKIP: {reason}")
    return 0


def main() -> int:
    if not VOCAB.exists():
        print("FAIL: the vocabulary header is missing.")
        return 1

    # SCOPED TO THE NAMESPACE THAT IS EXCLUSIVELY OURS: attribute names, role values
    # and list_type values. Not element types, kinds or encodings -- their VALUES
    # collide with other namespaces by design, and a check that reported those would be
    # mostly false positives:
    #
    #   "text"    is both INLINE_TEXT and ENCODING_TEXT, so using either shadows the other
    #   "html", "latex", "xml", "markdown", "json"  are Pandoc FORMAT names as well as
    #                                               our encodings, in the same files
    #   "code", "image", "raw"                      are both block and inline types
    #
    # First version of this check reported 17 constants, most of them legitimately
    # something else. A check with a 1-in-3 false positive rate is one people stop
    # reading, which is worse than not having it -- so it asks the narrower question it
    # can actually answer.
    all_consts = dict(re.findall(r'static constexpr const char \*([A-Z_]+) = "([^"]*)";', VOCAB.read_text()))
    consts = {n: v for n, v in all_consts.items() if n.startswith(("ATTR_", "ROLE_", "LIST_TYPE_"))}
    if not consts:
        return skip("no string constants found in the vocabulary header")

    # COMMENTS STRIPPED BEFORE THE SCAN. This file's comments quote vocabulary values
    # constantly -- explaining what `attributes['role']` means, or why "ordered" is a
    # legacy alias -- and a literal inside one of them would read as a bare literal in
    # code and report a shadowing that does not exist.
    #
    # duckdb_markdown found the same loose-pattern hazard in their usage scanner and
    # sharpened the framing: for a ONE-OFF measurement the manufacturing direction is
    # the recoverable one, because a phantom finding is loud and gets chased down. For
    # a STANDING CHECK it is not, because the way a phantom gets silenced is an EXEMPT
    # entry with a plausible reason -- and that entry then excuses the real instance
    # when it arrives. Loud is only self-correcting until someone quiets it.
    def strip_comments(text):
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        return re.sub(r"//[^\n]*", "", text)

    sources = {f: strip_comments(f.read_text()) for f in sorted(SRC.glob("*.cpp"))}

    shadowed = []
    for name, value in sorted(consts.items()):
        if name in EXEMPT or not value:
            continue
        hits = [f.name for f, t in sources.items() if f'"{value}"' in t]
        if hits:
            shadowed.append((name, value, hits))

    stale = sorted(n for n in EXEMPT if n not in all_consts)

    # AN EXEMPTION EXPIRES TWO WAYS and this check only had one. `stale` above catches
    # a key naming a constant that no longer exists. It never caught the other mode: an
    # entry that would PASS without it, and so excuses nothing while still reading as a
    # considered decision. check_roundtrip_sweep.py has audited its three registries
    # this way since yesterday -- an audit lands on whichever list you were thinking
    # about, and I was not thinking about this one.
    excuses_nothing = sorted(
        n
        for n in EXEMPT
        if n in all_consts and all_consts[n] and not any(f'"{all_consts[n]}"' in t for t in sources.values())
    )

    print(f"Checking {len(consts)} attribute/role/list_type constants are used, not shadowed")
    failed = False
    if shadowed:
        failed = True
        print("\nFAIL: these constants are shadowed by their own bare literal in src/:")
        for name, value, hits in shadowed:
            print(f'        {name} = "{value}"  in {", ".join(sorted(set(hits)))}')
        print("      A constant only protects the code that USES it. A consumer comparing")
        print("      constants by value would catch a change here that this repo would not,")
        print("      which is the wrong way round for the repo that owns the value.")
        print("      Use the constant, or add it to EXEMPT with the reason the literal is")
        print("      legitimately something else.")
    if excuses_nothing:
        failed = True
        print("\nFAIL: these EXEMPT entries excuse nothing -- the check passes without them:")
        for n in excuses_nothing:
            print(f'        {n} = "{all_consts[n]}" -- recorded reason: {EXEMPT[n]}')
        print("      DELETE them. An exemption that no longer applies goes on excusing a case")
        print("      that does not need excusing, and hides the real instance when it arrives.")
    if stale:
        failed = True
        print("\nFAIL: EXEMPT names constants that no longer exist:")
        for n in stale:
            print(f"        {n} -- recorded reason: {EXEMPT[n]}")
        print("      DELETE the entry rather than rewording it.")
    if failed:
        return 1

    print(f"  no constant shadowed; {len(EXEMPT)} exempt with reasons")
    print("OK: the vocabulary's own constants are used.")
    # WHAT THIS CANNOT SEE, stated so a green run is not read as more than it is.
    #
    # It checks that a constant is USED, never that the RIGHT one is used. Two
    # constants can share a value -- ROLE_DEFINITION and LIST_TYPE_DEFINITION are both
    # "definition"; ATTR_ORDERED_LEGACY and LIST_TYPE_ORDERED are both "ordered" -- and
    # then using either satisfies this check while one of them says the wrong thing.
    #
    # That is not hypothetical. A bulk conversion in this repo replaced the attribute
    # KEY "ordered" with LIST_TYPE_ORDERED at 18 sites: `attrs[LIST_TYPE_ORDERED]`
    # compiles, produces byte-identical output, passes every test and this check, and
    # means the wrong thing. panduck named the hazard an hour before it was committed
    # here -- "right by coincidence and wrong in meaning... it compiles, matches, and
    # reads as deliberate."
    #
    # This check cannot fix it: the values genuinely are the same string, position is
    # the only signal, and it does not parse.
    #
    # BUT "a bulk conversion cannot be verified" was too broad, and panduck corrected
    # it by auditing their own -- which was clean, and not because they were careful:
    #
    #     they matched   attributes["role"]  ->  attributes[ATTR_ROLE]
    #     I matched      "ordered"           ->  LIST_TYPE_ORDERED
    #
    # An INDEXING EXPRESSION carries its position -- `attributes[X]` can only be a key,
    # so the wrong constant there is a mistake you have to work at. A BARE LITERAL is
    # position-blind by construction, so one edit is correct in one place and wrong in
    # another with nothing distinguishing them.
    #
    # So the rule is mechanical rather than a matter of attention: WHEN CONVERTING
    # LITERALS TO CONSTANTS, MATCH THE SURROUNDING EXPRESSION, NOT THE LITERAL. It
    # narrows the edit to sites where position is already pinned, and the sites it
    # refuses to match are exactly the ambiguous ones that need eyes.
    print("    (cannot tell WHICH constant is right when two share a value -- see the note)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
