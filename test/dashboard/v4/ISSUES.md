# V4 Implementation Issues

## Issue 1: Block builders need overloads that accept inline children and return flattened lists

**Problem:** Current block builders like `db_paragraph(content VARCHAR)` return a single `duck_block`. To include inline content (links, images, formatted text), we need overloads that:
1. Accept `duck_block[]` (inline children)
2. Return `duck_block[]` (flattened list with proper levels)

**Current behavior:**
```sql
db_paragraph('Hello world')
-- Returns: single duck_block
```

**Needed behavior:**
```sql
db_paragraph([db_link('GitHub', url), db_text(' '), db_inline_image(badge, 'CI')])
-- Returns: duck_block[] (flattened list)
-- [
--   {kind: 'block', element_type: 'paragraph', content: '', level: 1, element_order: 0},
--   {kind: 'inline', element_type: 'link', content: 'GitHub', level: 2, element_order: 0, ...},
--   {kind: 'inline', element_type: 'text', content: ' ', level: 2, element_order: 1},
--   {kind: 'inline', element_type: 'image', content: 'CI', level: 2, element_order: 2, ...},
-- ]
```

**Design principle:** Hierarchy is expressed through `level` field and depth-first ordering, NOT through nested types. This maintains type safety with flat lists.

**Builders needing this overload:**

Block builders that can contain inline content:
- `db_paragraph(inlines duck_block[]) -> duck_block[]`
- `db_heading(inlines duck_block[], level) -> duck_block[]`
- `doc_blockquote(inlines duck_block[]) -> duck_block[]`
- `db_list_item(inlines duck_block[]) -> duck_block[]` (new)

Block builders that can contain block children:
- `db_list_block(items duck_block[][]) -> duck_block[]` - each item is flattened
- `db_section(heading duck_block[], content duck_block[]) -> duck_block[]`
- `doc_blockquote(blocks duck_block[]) -> duck_block[]` - nested blocks

Inline builders that can contain inline children:
- `db_link(inlines duck_block[], href) -> duck_block[]` - link with formatted text
- `db_bold(inlines duck_block[]) -> duck_block[]`
- `db_italic(inlines duck_block[]) -> duck_block[]`
- `doc_span(inlines duck_block[]) -> duck_block[]`

**Pattern:** Any builder that can have children should accept `duck_block[]` and return `duck_block[]` with children flattened at level+1.

---

## Issue 2: Need `db_list_item` builder

**Problem:** There's `db_list_block(items VARCHAR[], ordered)` but no way to create individual list items with inline content.

**Needed:**
```sql
db_list_item([db_link('GitHub', url), db_text(' '), db_inline_image(badge, 'CI')], ordered := false)
-- Returns: duck_block[] with list_item block + inline children at level+1
```

Or alternatively, `db_list_block` should accept `duck_block[][]` (list of inline arrays) and flatten everything.

---

## Issue 3: Markdown renderer needs to handle flat list with levels

**Problem:** Once blocks with inline children are flattened, `db_elements_to_md()` needs to:
1. Recognize parent-child relationships via level
2. Render inline children within their parent block context

**Current:** Renders each element independently.

**Needed:** Group consecutive level n+1 elements as children of the preceding level n element, and render appropriately (e.g., list item content, paragraph content).

---

## Issue 4: Step 02 should produce a single list, Step 03 should be one render call

**Goal:**
```sql
-- Step 02: Single list of duck_blocks
CREATE TABLE page_structure AS
SELECT list_flatten([
    [db_heading('Title', 1)],
    db_paragraph([db_link('GitHub', url), db_text(' '), db_inline_image(badge, 'CI')]),
    [db_hr()]
]) AS blocks;

-- Step 03: Single render call
COPY (SELECT db_elements_to_md(blocks) FROM page_structure)
TO 'dashboard.md' (FORMAT CSV, HEADER FALSE, QUOTE '');
```

**Blocked by:** Issues 1, 2, 3 above.

---

## Summary

The flat list with level-based hierarchy is the right design. We need:
1. Builder overloads: `doc_X(duck_block[]) -> duck_block[]` that flatten children with level+1
2. New builder: `db_list_item` for individual list items
3. Renderer update: `db_elements_to_md()` must handle level-based parent-child grouping
