# Test fixtures — canonical duck_blocks

Spec-conformant `duck_block` documents for testing consumers (notably the ANSI
renderer) against the [duck_blocks spec](../../docs/duck_blocks_spec.md) rather
than against any one input format's syntax.

## Files

| File | What it is |
|------|------------|
| `sample_document.parquet` | A document as `LIST(duck_block)` rows (8 blocks + 9 inlines) |
| `sample_document.json` | The same document as a JSON array of block objects |
| `generate.sql` | Regenerates both from the builder API |

## Why these exist

Rich text here is the **canonical structured form**: formatting lives in
`kind='inline'` child elements (`bold`, `italic`, `code`, `link`, …) with
**literal `content`** and `encoding='text'` — e.g. a paragraph is `content=NULL`
followed by its inline children at `level=2`. There is deliberately **no
Markdown syntax** (`**`, `*`, `` ` ``, `[](…)`) in any `content` field.

A correct renderer must walk the inline children and style by `element_type`.
A renderer that instead re-parses Markdown out of `content` will render these
fixtures wrong (empty paragraph / literal asterisks) — which is exactly the
regression these fixtures guard against.

Regenerate with `duckdb < test/fixtures/generate.sql` (duck_block_utils loaded).
