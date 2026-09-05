# Attribute-key vocabulary, pagination ruling, unknown-key report — proposal record

**Status: DRAFT, pending Teague's ruling.** Proposed by the panduck session on
2026-09-04, stated to be at Teague's direction. Verified here before recording.
Separate from the `filename` field design of the same day. Nothing implemented.

## The defect that prompted it

panduck's `read_pdf_blocks` put `page_number` on every block as an attribute.
`duck_blocks_page_rows` returned zero rows for a two-page PDF, because this repo's
page model is `page_break` blocks carrying `page_number`. Renaming the key changed
nothing; emitting breaks fixed it. Two models of one concept, and no check in any
repo could see it.

## Verified here

- **The spec DOES state the pagination model**, at length, in "`page_break` is
  physical, not semantic": a break marks the START of the page it names, `page_number`
  is optional, content before the first break belongs to no page, a document with no
  break has no pages. panduck's statement that the spec "only says the element type
  exists" is wrong. What the spec does NOT rule on is a per-block `page_number`.
- **Published `ATTR_` constants**: role, key, heading_level, list_type, source_type,
  pandoc_ast, and the legacy `ordered`. Seven.
- **Keys this repo's own builders emit** (`attrs["..."]` in `src/`): id, title,
  quote_type, format, src, href, display, class, alt, language, key, suffix, start,
  short_caption, prefix. Fifteen, one of which has a constant. The vocabulary owner
  has the same gap it is being asked to close for others.
- **Keys the spec documents in prose**: heading_level, format, pandoc_ast, role,
  source_type, key, page_number, ordered, list_type, alt. No attributes table.
- `vendor/duck_block_conformance.sql` has `duck_blocks_undeclared_types` and no
  attribute-key equivalent. The check-vocabulary arms compare against published
  constants only, so an invented key is invisible by construction.
- No vector or embedding surface exists in this repo (the three grep hits for
  "embedding" are the English word). panduck's clean-slate measurement holds.

## Recommendations

### 1. Publish constants for keys already in use — ratify, do not invent

Add `ATTR_` constants and a spec "Attributes" table for keys emitted by two or more
extensions or by this repo's own builders: `id`, `class`, `href`, `src`, `title`,
`alt`, `language`, `format`, `start`, `page_number`. Each row says which element
types carry it and what it means. `page` is not ratified. Keys emitted only by one
builder here (quote_type, display, prefix, suffix, short_caption) get a constant too,
since the point is that every emitted key has a name to check against.

Two additions relayed by panduck from webbed (2026-09-04, webbed issue #143):

- `ATTR_NAME` for the pre-HTML5 `<a name="...">` anchor, which shares the fragment
  namespace with `id` and is dropped entirely today. Kept distinct from `id`.
- **Published keys are reserved.** A source attribute whose name collides with a
  published `ATTR_` constant is skipped, not captured, so `<section role="banner">`
  cannot forge the vocabulary's own `role`. Recommendation: adopt as normative; a
  per-extension convention cannot enforce it.

Additive; SPEC_VERSION bumps once with the `filename` change if they land together.

### Found while verifying: the spec table and the validator disagreed on `level`

panduck found the block-type table giving `page_break` a NULL level while
`duck_blocks_validate` rejects NULL. Checked here: spec 3.0 ruled explicit levels
(commit 51d8177), the Level Field Semantics section says "there are no NULLs", the
paragraph above the table says the NULL convention was removed, and the validator
enforces it. Eight table cells and one preamble sentence were leftovers. Fixed in
the spec on 2026-09-04, with a guard added to `check_spec_alignment.py` so a NULL in
the level column fails the check. The validator is normative; panduck's readers
(level 1 or depth) were already conformant.

### 2. Pagination: rule on the per-block attribute

Recommendation: **the page model is breaks only; a per-block `page_number` attribute
is not part of the spec and consumers MUST NOT depend on it.** panduck drops it.

Reason, and it is the same principle as the `filename` field: **a row's position or
provenance belongs in reader columns; structure belongs in blocks.** A caller who
wants to filter rows by page has `duck_blocks_get_pages` over blocks it holds, reader
pushdown (`first_page`/`last_page`) for reading less, and, if a per-row page column is
wanted, a reader column beside `filename`, not an attribute on the struct. Two
representations of one fact is exactly what produced the zero-row defect.

**Relayed ruling, 2026-09-04 (panduck relaying Teague, to be confirmed first-hand):
per-block `page_number` is excessive; breaks only.** panduck drops the attribute once
the spec text is settled.

### 2b. Teague's follow-on proposal: boundary attributes and a page utility

**WITHDRAWN by Teague, 2026-09-05 (relayed by panduck, to be confirmed first-hand):**
*"I think my page start attribute idea is wrong. you can have it always on and
selectively propagate, however!"* Reading, shared with panduck: markers are the
"always on" part and stay the single source of truth; "selectively propagate" is a
QUERY-TIME utility that fills page numbers onto the blocks a caller asks about, on
demand, rather than a reader stamping boundary attributes into the vocabulary. Under
that reading: no `page_start`/`page_end` keys, no ATTR_ constants for pages, no
one-producer rule and no pre-stamp assertion, because there is only one derivation.
What survives from the analysis below is the limit (propagation reaches a slice only
when its markers are inside the slice; the extractor annotation is the general answer)
and the query surface: `duck_blocks_page_span(blocks) -> STRUCT(first_page, last_page)`
from markers, NULL when there are none, plus a propagating form such as
`duck_blocks_with_page_numbers(blocks)` that returns the same blocks with
`attributes['page_number']` filled from the preceding marker, computed, never emitted
by a reader. The analysis is kept for the record of why the attribute form was
rejected.


In his words, relayed: *"beginning/end element of a page should have page number or
maybe page_position = start/end on the first/last. then there should be a utility for
that."* Two spellings offered, form left open. Analysed here; RULING is his.

**What it is for.** Markers are outside most slices. `doc_section` cuts a range, the
enclosing `page_break` is before the range, and the slice arrives with no page
information; `duck_blocks_page_rows` on it returns zero rows, correctly, because the
slice carries none. An attribute on a boundary element survives slicing when the
boundary is inside the slice, and no consumer has to filter a phantom element.

**The limit, stated first because it bounds the design.** A boundary attribute helps a
slice only when the slice CONTAINS a page turn. A section that starts and ends inside
page 4 has no boundary element and learns nothing from either spelling. So this is not
"page information for slices"; it is "page turns inside a slice are visible without the
marker". The general answer for a slice's page span belongs to the EXTRACTOR, which
holds the whole document when it cuts: Tiiny's extraction-annotation spec already has
`extracted_ref` for exactly this, and `pages` as an `extracted_by` value. The two
proposals compose; neither replaces the other.

**Recommendations:**

1. **Complement, never replace.** `page_break` stays canonical: it alone can say "this
   break ends page N", carry an empty page, or mark content before the first page.
   Boundary attributes are DERIVED from markers.
2. **Spelling: two keys valued with the page number, `page_start` and `page_end`.**
   Not `page_position = start|end`: a one-element page is both, and one VARCHAR cannot
   say both (panduck's edge). Not bare `page_number` on the boundary elements: the
   ruling above just rejected per-block `page_number`, and a consumer seeing it on some
   blocks cannot tell a boundary from the rejected model. `page_start = '4'` on the
   first element and `page_end = '4'` on the last says which boundary AND which page,
   and a one-element page carries both keys.
3. **One producer: a utility, idempotent, from markers.** `duck_blocks_mark_pages(blocks)`
   stamps the two keys from `page_break` elements. Readers MAY pre-stamp only if the
   result is identical to what the utility yields (panduck's PDF reader can, from its
   per-element page column; the EPUB reader need not). One fact derived one way, so
   two producers cannot disagree. **A pre-stamping reader SHOULD assert its output
   against the utility in its own suite** — `read_pdf_blocks(f)` pre-stamped equals
   `duck_blocks_mark_pages` over the same blocks — because "identical" is a rule that
   rots silently, and unusually this instrument is LOCAL: the utility is a consumer the
   reader can load, so no junction bridge is needed. (panduck, 2026-09-05, who will hold
   it as an ordinary assertion in its PDF reader test.)
4. **The query side: `duck_blocks_page_span(blocks) -> STRUCT(first_page, last_page)`.**
   Reads markers first, boundary attributes second, NULL when neither is present.
   "NULL" is the honest answer for an interior slice; the extractor annotation is where
   that case is answered.
5. **Ratify `ATTR_PAGE_START`, `ATTR_PAGE_END` through the list in item 1**, and state
   that their values are the same integers `page_break` carries. Additive.

**Not recommended:** changing `duck_blocks_get_pages` or `page_rows` to read the
attributes. They key on markers, markers stay canonical, and a slice with no marker has
no pages — the spec already says so and it is the correct fact.

### 3. An unknown-key report

Add `duck_block_attribute_names()` (introspection, like the kind/type/encoding lists)
and `duck_blocks_undeclared_attribute_keys(blocks)` to the vendorable conformance
SQL, returning `{key, count, element_types}`. Advisory, never a hard failure:
experimentation stays possible, and "you emit 7 keys nobody has published" is the
report that would have caught the defect. A `duck_blocks_lint` warning arm can reuse
it.

### 4. Embeddings: reserve the principle, not a slot

`attributes` is `MAP(VARCHAR, VARCHAR)` and cannot physically hold a vector, so the
question is only where one lives beside blocks. Recommendation: a one-paragraph spec
statement that an embedding is **derived per-block data keyed by
`(filename, element_order)`**, held in a reader or index column or a sibling table,
never in the struct or its attributes. That is the same principle as items 2 and the
`filename` field, and it is why `filename` has to exist before any vector surface
does. No implementation, no reserved index.

## Sequencing

Agree with panduck: attribute constants (1) and the pagination ruling (2) before any
reader-registry hook that lets a reader supply its own toc/search. Without them each
registered search keys on whatever its author picked, and the page/page_number
failure recurs once per reader.

## Follow-up recorded 2026-09-04: a junction fixture

panduck measured, through the parquet bridge, that both shipped provenance emitters put
`filename` FIRST and neither binds against 6.4 acceptance, while each repo's own suite
(webbed: 3940 assertions) was green. What broke is the junction between an emitter in one
extension and a consumer in another, and no repo's suite covers a junction. Proposal, the
same shape as the undeclared-key report: **one canonical conformance fixture**, a parquet
of 8-field blocks in the exact accepted type, that every producer must be able to generate
and every consumer must accept, so the fleet tests against one artifact instead of four
independent good intentions. The bridge works across mismatched DuckDB pins, which is
exactly when a junction cannot be tested by loading both extensions. Not scheduled.

