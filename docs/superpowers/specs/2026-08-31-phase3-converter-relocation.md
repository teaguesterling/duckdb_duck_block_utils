# Phase 3: relocating the Pandoc converters — analysis and prerequisite

**Date:** 2026-08-31
**Status:** analysis; **not ready to plan as tasks** — one decision blocks it
**Follows:** `2026-08-31-pandoc-gaps-and-reader-dispatch-design.md`

## What Phase 3 was going to be

Move `pandoc_block_convert.cpp` + `pandoc_inline_convert.cpp` (2,120 lines, 24% of this
extension) to panduck, completing the reframing: duck_block_utils becomes vocabulary +
validation + construction/query/render utilities, and panduck owns the Pandoc model it is
named for.

The public surface that would move:

```
pandoc_ast_to_blocks        duck_blocks_to_pandoc_ast     read_pandoc_ast
pandoc_inlines_to_db_inlines duck_blocks_to_pandoc_blocks  write_pandoc_ast
pandoc_inlines_to_text       pandoc_ast
```

## The good news: the converters are a clean leaf

A first look suggested deep coupling — `extraction.cpp`, `assembly.cpp` and
`validation.cpp` all appeared to reference the converters. They do not. They include
`pandoc_convert_util.hpp`, which despite its name holds **generic** helpers: the recursion
depth cap and a safe integer parse. Only `duck_block_utils_extension.cpp` touches the
converters, and only to `Register()` them.

So nothing inside this extension depends on Pandoc conversion. Mechanically, the move is a
file relocation plus a registration change.

## The blocker: there is no shared vocabulary header

The converters are built almost entirely out of `BlockTypes::` constants. panduck cannot
host them without the vocabulary — and panduck's `src/include/duck_block_types.hpp` is a
**copy**, already **missing 19 of 44** names:

```
blocks  bool  caption  cite  deflist  div  figure  generic  inlines  lineblock
list_item  map  math  note  quoted  section  string  value  version
```

Note what is in that list: `cite`, `math`, `note`, `quoted`, `div`, `list_item` all predate
today's work. **The copy was already stale before any of this started.** webbed has a third
copy; that is where panduck's came from.

**This is a live latent bug independent of Phase 3.** panduck's roundtrip canonicalizer
cannot recognise `figure`, `caption` or `generic`, so it will mis-handle documents
containing them without any test noticing — the copied header does not fail, it silently
disagrees.

Moving 2,120 lines of vocabulary-dependent code into a repo whose vocabulary header is 43%
stale would bake that drift in permanently. **Fixing the header sharing is a prerequisite,
not a follow-up.**

`db_block_kinds()` / `db_block_types()` do not solve this. They are runtime introspection,
usable for *assertions*; C++ code needs compile-time constants.

## Four ways forward

### A. panduck takes a build dependency on duck_block_utils

Submodule or vcpkg, consuming `block_types.hpp` directly. Honest — panduck already depends
on the vocabulary semantically — and removes the copy at a stroke. Costs a real build-time
coupling between two extensions that today share only a type, and makes duck_block_utils'
header a published interface with the compatibility obligations that implies.

### B. Extract the vocabulary into a header-only package

Both consume it; neither owns it. Cleanest end state, most work: a new artifact with its own
versioning and release cadence, for one header.

### C. Generate the header from one source

A small codegen step producing the constants for each consumer. Avoids a runtime or
build-time dependency, adds a generator to maintain.

### D. **Do not move the converters.** *(recommended)*

The reframing said format-specific code leaves. The question this forces is whether Pandoc
AST is *a format* to duck_blocks, or its *interlingua*.

It is closer to the interlingua. `block_types.hpp` is substantially pandoc-types 1.23
transliterated — `Str`/`Emph`/`Strong`/`Quoted`/`SmallCaps`/`Cite`/`Note`/`Span` are pandoc's
constructor set, and the `caption`, `figure` and `deflist` types added today came directly
from pandoc constructors. The vocabulary is *defined against* the Pandoc model in a way it
is not defined against HTML, RTF or DOCX. On that reading the converters are spec
machinery, the same category as validation, rather than a reader.

The practical case is stronger still:

- The drift that motivated all of today's work was a **mapping maintained in two places**.
  The converters are not that. They are one implementation, in one place, now with complete
  pandoc-types 1.23 coverage and a real-pandoc harness guarding it.
- Moving them creates a hard build dependency between two extensions **in order to
  relocate code that is not causing a problem**.
- It costs another `LOAD panduck`: duckeye routes thirteen formats through
  `pandoc_ast_to_blocks`, and that would join reading by path in requiring panduck.
- The alignment harness lives here and tests the converters against a real pandoc. It would
  have to move too, or test across a repo boundary.

**Recommendation: D, and fix the header drift anyway.** The stale copies are a real bug
today. Phase 3 is a tidiness argument whose prerequisite is that bug's fix — so do the fix,
and then judge whether the move still looks worth a build dependency. It probably will not.

## What to do regardless of the decision

1. **Tell panduck and webbed their vocabulary copies are stale**, with the missing list.
   panduck's roundtrip canonicalizer is mis-handling `figure`/`caption`/`generic` now.
2. **Add a copy-drift assertion** in each consumer: compare the local header's constants
   against `db_block_types()` and fail when they disagree. This is the same
   self-describe-and-assert shape used for the reader registry, and it makes a stale copy
   loud instead of silent — which is worth doing even under option A or B, since it catches
   a mis-synced vendored header too.
3. **Decide A/B/C/D deliberately**, rather than letting the copies persist by default.

## Out of scope

- `page` markers, still proposed and unstarted.
- Anything requiring a new shared artifact, until A/B/C/D is settled.
