# V4 Dashboard Pipeline - DEFERRED

**Status:** Deferred pending resolution of Issue #2

## Goal

Create a dashboard pipeline that uses duck_block_utils builders completely, without embedding markdown syntax in string templates.

## Blocking Issue

**Issue #2:** [Define canonical inline representation: content vs nested children](https://github.com/teaguesterling/duckdb_duck_block_utils/issues/2)

The `doc_inlines_to_pandoc()` converter expects nested inline children at level+1, but builders like `doc_link('text', 'url')` store text in the content field. This causes links to render as empty `[]()`.

## Current State

The v4 files exist but produce incorrect output:
- `01_yaml_load_data.sql` - Works (loads YAML data)
- `02_duck_block_utils_create_page_structure.sql` - Creates blocks but inline rendering broken
- `03_markdown_render.sql` - Works but receives broken inlines

## Resolution Path

1. Finalize canonical representation spec (`docs/duck_blocks_spec.md`)
2. Update builders and converters to follow spec
3. Resume v4 implementation

## Workaround

For now, use v3 which uses string templates with embedded markdown syntax.
