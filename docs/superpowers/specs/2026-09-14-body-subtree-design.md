# Spec 1.4: body is a subtree property

**Status:** approved by Teague ("Roll that 1.4 out", 2026-09-14) after the three readings were
put to him; implemented on `feat/spec-1.4-body-subtree`.
**Reported by:** panduck (#54 fixture, measured with this repo's 1.3 build), 2026-09-14.

## The gap in 1.3

Pandoc-derived readers emit document metadata as a `kind='value'` container whose text lives in
`kind='inline'` children (MetaInlines). The per-row `duck_block_is_body` says the container is
not body and then says "Test Author" under it IS. `duck_blocks_to_text` was clean because it
walks the tree; every row filter leaked: an embedder, and panduck's `doc_section` /
`doc_search_sections`, on docx, odt, org, epub, rtf and tex.

## Readings considered

1. **Subtree rule (chosen).** Body iff block/inline, not metadata, and no ancestor by level is
   a value or metadata row. What `to_text` already does.
2. Change producers to emit value leaves as something other than inline: breaking, fights the
   MetaValue model every reader relies on.
3. Document the per-row predicate as an approximation: leaves the function misleading for the
   one case it was introduced to settle.

## What 1.4 adds (additive)

- The definition above, in the spec's two-homes section and kind-filter table.
- `IsBody()` unchanged in answers, documented as necessary-not-sufficient in the header.
- `duck_blocks_body(blocks) -> LIST(duck_block)`: a scalar Value walk (not a table macro:
  panduck measured that re-scanning a table function through several CTE references returned
  zero rows on DuckDB 2.0). A value or metadata row roots a subtree; it and every following row
  at a greater level are dropped; the subtree ends at the first row at or above the root's level.
  Childless value rows and lone frontmatter blobs are dropped whole. No renumbering.
- The value-tree level contract (descendants strictly deeper than their root) stated and
  validated as L6: an inline immediately following a value row at the same level.
- `PREDICATE_REVISION = "1.3"`: the deferred drift-check ruling. Consumer checks compare
  constants by value and cannot see predicate bodies; this constant changes when one does.

## Tests

`test/sql/body_v14.test`: the per-row predicate unchanged (and shown saying true for the leaf);
the panduck-shaped fixture projected to exactly the body rows with `element_order` kept;
`to_text` equality; nested value trees dropped whole; NULL/empty; L6 red and green.

## Consumers

panduck's walk (panduck#69) becomes `duck_blocks_body(list(b))`. Siblings re-vendor 1.4 and
set the IsBody caveat comment to a pointer at `duck_blocks_body`.
