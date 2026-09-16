# Extraction annotation — design

**Status: DRAFT, pending ratification. Nothing implemented, and no attribute key is
minted in code.** Requested by Teague 2026-09-04: *"we should have a canonical way to
flag and annotate the hit blocks so we can have a single
`duck_blocks_extracted_to_text`/`_to_json`."* Partitioned to this session by
duckdb-duck-block-utils-8d, who owns the vocabulary and will review this for
consistency. Decisions that are the spec owner's are marked RULING.

## Problem

The extractors now all hand back blocks (`f047b3e`, `0eb9e47`):

```
duck_blocks_get_section(blocks, pattern)      -> LIST(duck_block)
duck_blocks_get_pages(blocks, first, last)    -> LIST(duck_block)
duck_blocks_sections_like(blocks, query_term) -> (section, start_order, blocks)
duck_blocks_toc_rows(blocks)                  -> metadata only, no blocks yet
duck_blocks_page_rows(blocks)                 -> metadata only, no blocks yet
```

What comes back is a bare `LIST(duck_block)` that has **lost the fact that it is an
extract**. Nothing on it records that these blocks are §Methods, or pages 3–7, or the
three hits for "canning". So:

- Every caller re-renders extracts its own way, and they disagree.
- Concatenating two extracts produces one undifferentiated list. Which blocks answered
  which query is unrecoverable.
- A renderer cannot write "§ Methods" above the text, because it was never told.

One renderer (`duck_blocks_extracted_to_text` / `_to_json`) can only exist if the
extract carries, in a canonical place, **what pulled it and from where**.

## Why this goes in `attributes`, and why the filename precedent does not forbid it

`2026-09-04-filename-provenance-field-design.md` rejected `attributes['filename']`:

> no shape change, but not a column, and repeated per block as a map entry rather than
> a dictionary-encoded column.

That rejection is correct and does not transfer. **The test is whether the fact has to
travel *inside* a `LIST(duck_block)` value across a function boundary.**

- `filename` does not. It is a property of a reader's *row*; the row is the carrier and
  `GROUP BY filename` is the consumer. It can be a column, so it should be.
- An extraction annotation is produced **by a function whose only output is the list**.
  The list is the only carrier there is, so the fact must be in-band or it does not
  exist.

Cardinality (millions of reader rows vs tens of extracted blocks) is a *consequence* of
that difference, not the reason for it — which matters, because it means the argument
still holds for an extract that happens to be large. This is the same reasoning that
puts `role` in `attributes`, and it is why the partition note says a flag on a block is
structure and belongs in `attributes`, never in a new struct field.

It also inherits the filename spec's rejection of a `kind='value'` marker element:
such a marker "cannot be grouped by, and is lost the moment two files' rows are
interleaved." Interleaving is the *normal* case for extracts — it is exactly what
`sections_like` returns — so a marker block is doubly wrong here.

**Reserved-keys rule (from webbed).** Once ratified, a *source* attribute literally
named `extracted_by` is skipped by readers, so no upstream document can forge an
extract by carrying the key itself.

## RULING 1 — which blocks carry it

Recommendation: **every block in the extract.**

The alternative — stamp only the first block — is cheaper and breaks immediately:
slicing an extract, filtering it, or `UNION`ing two of them loses or misattributes the
annotation, and those are ordinary operations on a `LIST(duck_block)`. Per-block is the
only form that survives the value being treated as a value.

The cost objection that sank `attributes['filename']` does not bite at extract
cardinality (tens of blocks, not millions). If an extract is ever large enough for the
map entries to matter, the caller extracted the whole document and should not have used
an extractor.

## RULING 2 — the keys

Recommendation: **two keys, proposed for ratification, not minted:**

| key | value | example |
|---|---|---|
| `extracted_by` | which extractor produced this — a **closed** set | `section`, `pages`, `search`, `toc` |
| `extracted_ref` | the locator, extractor-specific | `Methods`, `3-7`, `canning` |

A third, `extracted_seq`, is needed only if RULING 4 says multi-hit extracts must be
distinguishable within one list. Recommend adding it **only** if that ruling says yes —
an unused key is a key that drifts.

Deliberately **not** a single JSON-encoded value. `attributes` is
`MAP(VARCHAR, VARCHAR)`, so a JSON blob would fit, and it would be opaque to every
drift and undeclared-key check the vocabulary runs. Two flat keys stay inspectable.

The closed `extracted_by` value set gets **constants the way `ROLE_` values do**
(`EXTRACTED_BY_SECTION`, `EXTRACTED_BY_PAGES`, …), so the drift check can see a *value*
change and not merely a key change.

Both keys must go through the ratification path the attribute draft describes, and
neither should appear in `src/` before the spec names it. Minting first is exactly how
panduck's `page`/`page_number` defect happened today.

## RULING 3 — the renderer's contract

```sql
duck_blocks_extracted_to_text(blocks)   -- one text rendering, grouped by extract
duck_blocks_extracted_to_json(blocks)   -- [{extracted_by, extracted_ref, blocks:[…]}]
```

Recommendation: **blocks with no annotation render exactly as `duck_blocks_to_text`
does.** Stated as an identity in the spec, not as a fallback — there is no claim being
violated, so there is nothing to be loud about. `duck_blocks_extracted_to_text` is then
safe to call on any block list, which is what makes it a single renderer rather than a
third thing to choose between.

What it adds over `duck_blocks_to_text` is a canonical heading per extract group
(`§ Methods`, `pp. 3–7`, `match: "canning"`), so two concatenated extracts read as two
extracts. The heading format per `extracted_by` value is part of the spec text, so
consumers do not each invent one.

## RULING 4 — do multi-hit extracts need to be distinguishable in one list?

`duck_blocks_sections_like` returns one row per hit, so within a row the annotation is
unambiguous and no sequence is needed. It becomes ambiguous only if a caller flattens
several rows into one list and two hits share a `extracted_ref` (two sections with the
same title — which `duck_blocks_quality` already reports as `duplicate_heading`).

Recommendation: **do not add `extracted_seq` yet.** Add it when a real caller flattens,
rather than reserving a slot ahead of adoption — reserving ahead of adoption is what
produced the `SOURCE_FORMAT_IDX` / `FILE_PATH_IDX` conflict the filename spec had to
untangle.

## RULING 5 — do `toc_rows` and `page_rows` stamp?

**`page_rows`: yes**, once it gains a `blocks` column — `extracted_by = 'pages'`. Pages
do not nest, so there is no boundary ambiguity.

**`toc_rows`: no. It should not gain a `blocks` column at all.** A table of contents is
an **index, not an extract**. Both plausible boundary semantics are already spoken for
— `get_section` contains subsections, `sections_like` reports the innermost — and a
third semantics for `toc_rows` would be a vocabulary cost with no new capability,
because the composition already exists: a caller who wants the blocks of an entry calls
`duck_blocks_get_section` on that entry and gets containment semantics, once, on
demand. Emitting them eagerly turns a 200-block document into 500+ blocks spread across
rows, most of them duplicated into their ancestors.

**RULED by Teague 2026-09-04: metadata only.** `toc_rows` does not gain a `blocks`
column. "All extractors return blocks" therefore reads as *all extractors*, and a table
of contents is not one — it is an index into the document, and the blocks of an entry
are obtained by composing `duck_blocks_get_section` on it.

## RULING 6 — interaction with `filename` (6.4)

They are orthogonal and both should be present on an extracted block:

- `filename` — struct field, index 7 — *which file this block was read from*.
- `extracted_by` / `extracted_ref` — attributes — *what pulled this block out*.

Nothing here changes the 7-vs-8-field shape question, and the annotation must not be
used to smuggle provenance. Concretely: every extractor here is a macro over C++
functions that return the 7-field shape, so **an extract never carries `filename` at
all**. The way to keep provenance on extracts is to extract per document — `GROUP BY
filename` on the reader rows, then extract within each group — which is the shape a
retrieval loop wants regardless. An extractor that receives 8-field blocks and returns
7-field blocks (which RULING 3 of the filename spec permits for every transforming
function) loses `filename` and keeps the annotation. That asymmetry should be stated in
the spec text so nobody reads the annotation as a provenance substitute.

`SPEC_VERSION`: additive — new attribute keys, no field renamed or removed. **MINOR
bump.** Whether it rides 6.4 or lands as 6.5 is the owner's call and depends on whether
6.4 has shipped when this ratifies.

## Verification

- Each extractor stamps `extracted_by` with its own value and an `extracted_ref` that
  round-trips (extract §Methods, read the ref back, get `Methods`).
- **Every** block of an extract carries the annotation, not just the first — assert on
  the last block too, since a first-block-only implementation passes any test that
  only looks at `[1]`.
- Concatenating two extracts keeps them distinguishable: group by
  `(extracted_by, extracted_ref)` and get two groups with the right counts.
- Slicing an extract preserves the annotation on the surviving blocks.
- `duck_blocks_extracted_to_text` on **un-annotated** blocks equals
  `duck_blocks_to_text` on the same blocks — the identity of RULING 3, asserted rather
  than assumed.
- Two extracts concatenated render with two headings, in extract order.
- **Discriminating negative:** an unratified/unknown `extracted_by` value is reported by
  the undeclared-key check rather than rendered with a made-up heading.
- `duck_blocks_extracted_to_json` output parses and its `blocks` arrays round-trip
  through the block vocabulary.

## Resolved: where stamping happens, and what to call it

Settled with the vocabulary owner 2026-09-04:

- **`duck_blocks_slice` stays ignorant of extraction.** It is a general utility. Each
  extractor applies the helper itself; a second pass over tens of blocks costs nothing.
- **The helper is `duck_blocks_annotate(blocks, by, ref)`** — *not*
  `duck_blocks_stamp_extract`. `duck_blocks_stamp` already means the version marker, and
  reusing the verb would make two unrelated things share a name.

---
*Written by the Tiiny/SurvivorLibrary session. Downstream motivation: tiibrarian grounds
answers by checking a quoted span against the page it cites, so it needs an extract that
still knows what it is — an unannotated `LIST(duck_block)` cannot tell a grounding layer
which query or section produced it.*
