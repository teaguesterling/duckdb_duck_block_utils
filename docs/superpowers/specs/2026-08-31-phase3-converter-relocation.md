# Phase 3: relocating the Pandoc converters — analysis and prerequisite

**Date:** 2026-08-31
**Status:** analysis complete; **recommendation is not to proceed**. panduck concurs; Teague's call.
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
host them without the vocabulary — and panduck has no usable one. Its
`src/include/duck_block_types.hpp` is a struct-shape helper (field indices, `CreateBlock`
/`CreateInline`) that also carries some vocabulary constants, of which **18 of the
canonical 41 are absent**:

```
blocks  bool  caption  cite  deflist  div  figure  generic  inlines  lineblock
list_item  map  math  note  quoted  section  string  version
```

`cite`, `math`, `note`, `quoted`, `div` and `list_item` predate today's work, so the copy
was drifting before any of it. webbed has a third copy of the same shape.

**CORRECTION, 2026-08-31.** An earlier version of this document said the drift caused a
live bug — that panduck's roundtrip canonicalizer "cannot recognise `figure`, `caption` or
`generic`". **That is false, and panduck disproved it by running the canonicalizer**: it
passes block `element_type` through verbatim (`CBlock(etype, ...)`), so it has no fixed
block vocabulary to be stale against and handles all three today.

Two errors are worth recording, because they were different:

- The consequence was **inferred from the header's contents rather than tested**. The
  header being stale does not imply anything is broken; that required reading the consumer,
  which I did not do.
- The *number* was produced by an **invalid comparison** — canonical `TYPE_`/`INLINE_`
  constants against *every quoted lowercase string* in panduck's file, which is a different
  set. It happened to land near the true figure, which is worse than being plainly wrong:
  an approximately-right number arrived at by a broken method reads as confirmed.

The staleness is real but **inert**. It is worth fixing so it cannot become live, not
because it is live.

The genuine gaps sit elsewhere, and panduck found both while checking:

1. The canonicalizer's **inline** wrapper is a fixed list of nine (bold, italic,
   strikethrough, underline, superscript, subscript, smallcaps, code, math). An inline
   outside it loses its marker. panduck's readers emit five, all covered — latent, not live,
   until a reader emits `generic` or `note`.
2. On the Pandoc side, panduck emits `figure` then recurses caption blocks, flattening a
   caption to `paragraph`, where a duck_block `caption` stays `caption`. The two sides
   disagree the moment a reader emits one — which DOCX table captions will.

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
  have to move too, or test across a repo boundary. **panduck weighs this heaviest**: it is
  the only thing standing between those converters and silent drift, and it works because it
  sits next to them.
- panduck's own argument, which is stronger than mine: **panduck has no vocabulary header at
  all** — only a struct-shape helper. 2,120 lines of vocabulary-dependent code would arrive
  with nothing local to define the vocabulary against. That is an argument against the move,
  not for fixing the header first.

**Recommendation: D.** panduck agrees. Fix the header drift too, but on its own merits — it
is inert today, not a bug. Phase 3 is a tidiness argument whose prerequisite is a build
dependency between two extensions, to relocate code that is not causing a problem.

## What to do regardless of the decision

1. **Told panduck and webbed their vocabulary copies are stale**, with the missing list.
   Inert today — no consumer indexes off those constants — but worth closing so it cannot
   become live.
2. **Add a copy-drift assertion** in each consumer: compare the local header's constants
   against `db_block_types()` and fail when they disagree. This is the same
   self-describe-and-assert shape used for the reader registry, and it makes a stale copy
   loud instead of silent — which is worth doing even under option A or B, since it catches
   a mis-synced vendored header too.
3. **Decide A/B/C/D deliberately**, rather than letting the copies persist by default.

## Out of scope

- `page` markers, still proposed and unstarted.
- Anything requiring a new shared artifact, until A/B/C/D is settled.
