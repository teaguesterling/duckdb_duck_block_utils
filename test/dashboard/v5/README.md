# V5 Dashboard: V2 API Implementation

This dashboard tracks the C++ implementation of the v2 API changes documented in `docs/v2_api_design.md`.

## Summary of Changes

The v2 API introduces three major changes:

1. **All builders return `LIST(duck_block)`** - Every `db_*` builder returns a list, enabling uniform composition
2. **Config parameters first, content last** - Reordered parameters for consistency
3. **`duck_block_content` union type** - Accepts `VARCHAR | duck_block | LIST(duck_block)` for flexible input

## Implementation Phases

### Phase 1: Core Infrastructure

Create the `BuildWithContent` utility that handles the `duck_block_content` union type:

```cpp
// New helper function that all builders will use
// Handles: VARCHAR → set content, children → content NULL + children at level+1
static vector<Value> BuildWithContent(
    const Value &parent_block,
    const Value &content_input,  // Could be VARCHAR, duck_block, or LIST(duck_block)
    int32_t base_level = 1
);
```

**Logic:**
1. If `content_input` is NULL → return `[parent]` with empty content
2. If `content_input` is VARCHAR → return `[parent]` with content set
3. If `content_input` is duck_block → return `[parent (content=NULL), child@level+1]`
4. If `content_input` is LIST(duck_block) → return `[parent (content=NULL), children@level+1...]`

### Phase 2: Block Builders

Update each block builder to:
1. Return `LIST(duck_block)` (already partially implemented for some)
2. Use new parameter order
3. Call `BuildWithContent` for content handling

| Builder | Current Signature | V2 Signature | Status |
|---------|------------------|--------------|--------|
| `db_heading` | `(content, level) → duck_block` | `(level, content) → LIST` | Needs update |
| `db_paragraph` | `(content) → duck_block` | `(content) → LIST` | Needs update |
| `db_code` | `(content, lang?) → duck_block` | `(lang?, content) → LIST` | Needs update |
| `db_blockquote` | `(content, level?) → duck_block` | `(level?, content) → LIST` | Needs update |
| `db_list_block` | `(items, ordered?) → duck_block` | `(ordered?, items) → LIST` | Needs update |
| `db_hr` | `() → duck_block` | `() → LIST` | Needs update |
| `db_metadata` | `(yaml) → duck_block` | `(yaml) → LIST` | Needs update |
| `db_image` | `(src, alt?, title?) → duck_block` | `(src, alt?, title?) → LIST` | Needs update |
| `db_raw` | `(content, format?) → duck_block` | `(format?, content) → LIST` | Needs update |
| `db_list_item` | N/A | `(ordered?, content) → LIST` | **New** |

### Phase 3: Inline Builders

Update each inline builder similarly:

| Builder | Current Signature | V2 Signature | Status |
|---------|------------------|--------------|--------|
| `db_text` | `(content) → duck_block` | `(content) → LIST` | Needs update |
| `db_bold` | `(content) → duck_block` | `(content) → LIST` | Needs update |
| `db_italic` | `(content) → duck_block` | `(content) → LIST` | Needs update |
| `db_link` | `(text, href, title?) → duck_block` | `(href, title?, content) → LIST` | Needs update |
| `db_inline_image` | `(src, alt?, title?) → duck_block` | `(src, alt?, title?) → LIST` | Needs update |
| ... | ... | ... | ... |

### Phase 4: Assembly Functions

Update assembly functions for new input types:

| Function | Current Input | V2 Input | Status |
|----------|--------------|----------|--------|
| `db_assemble` | `LIST(duck_block)` | `LIST(LIST(duck_block))` | Needs update |
| `db_document` | `LIST(duck_block)` | `LIST(LIST(duck_block))` | Needs update |
| `db_section` | `(title, level, children?)` | `(level, title, children?)` | Needs update |

### Phase 5: Extraction Functions

Ensure extraction functions handle both old and new formats:

- `db_blocks_headings` - Read from `attributes['heading_level']` first, fall back to `level` field
- `db_rebase_levels` - Write to `attributes['heading_level']`, read with same priority

Already implemented but needs verification.

## Files to Modify

```
src/builders.cpp              # Block builders
src/include/builders.hpp      # Block builder headers
src/inline_builders.cpp       # Inline builders
src/include/inline_builders.hpp # Inline builder headers
src/assembly.cpp              # Assembly functions
src/include/assembly.hpp      # Assembly headers
src/extraction.cpp            # Extraction functions (verify)
```

## Test Coverage

V2 test files have been created:
- `test/sql/builders_v2.test`
- `test/sql/inline_builders_v2.test`
- `test/sql/assembly_v2.test`
- `test/sql/extraction_v2.test`
- `test/sql/validation_v2.test`
- `test/sql/pandoc_blocks_v2.test`

## Implementation Strategy

1. **Add new overloads alongside existing** - Don't break existing tests initially
2. **Implement `BuildWithContent` utility first** - This is the core of the change
3. **Update builders one at a time** - Test each change
4. **Remove old overloads once v2 tests pass** - Clean up deprecated signatures

## Key Design Decisions

### Content Field Rules
- **VARCHAR input** → `parent.content = input_string`
- **Children input** → `parent.content = NULL`, children at `level + 1`

### Level Field
- `level` field indicates structural nesting depth (for rendering)
- `attributes['heading_level']` stores semantic heading level (1-6)
- These are now independent: a level 3 heading can be at structural level 1

### Element Order
- Assigned by `db_assemble`, not by individual builders
- Builders set `element_order = 0` as placeholder

## Backwards Compatibility

The extraction functions should handle both formats:
1. Old format: `heading_level` stored in `level` field
2. New format: `heading_level` stored in `attributes['heading_level']`

Priority: Check `attributes['heading_level']` first, fall back to `level`.

## Example: Building a Document (V2 API)

```sql
SELECT db_assemble([
    db_heading(1, 'Welcome'),
    db_paragraph([
        db_text('Click '),
        db_link('https://example.com', 'here'),
        db_text(' to learn more.')
    ]),
    db_section(2, 'Getting Started', [
        db_paragraph('Installation instructions...'),
        db_code('bash', 'npm install')
    ])
]);
```

This produces a flat list with proper levels and element_order values.
