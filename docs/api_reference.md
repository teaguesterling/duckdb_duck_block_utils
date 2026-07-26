# API Reference

Complete reference for all functions in the `duck_block_utils` extension.

## Type Definitions

### duck_block (Unified Type)

The core document element type used throughout this extension. Both block-level and inline elements use the same type, distinguished by the `kind` field:

```sql
STRUCT(
    kind VARCHAR,                       -- 'block' or 'inline'
    element_type VARCHAR,               -- Element type identifier
    content VARCHAR,                    -- Primary content
    level INTEGER,                      -- Hierarchy level (NULL if not applicable)
    encoding VARCHAR,                   -- Content encoding: 'text', 'json', 'yaml', 'html', 'xml'
    attributes MAP(VARCHAR, VARCHAR),   -- Type-specific metadata
    element_order INTEGER               -- Position in document (0-indexed)
)
```

### Kind Values

| Kind | Description |
|------|-------------|
| `block` | Block-level elements (heading, paragraph, code, list, etc.) |
| `inline` | Inline elements (text, bold, italic, link, etc.) |

### duck_block_ext

Extended element type with provenance tracking.

```sql
STRUCT(
    kind VARCHAR,
    element_type VARCHAR,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR,
    attributes MAP(VARCHAR, VARCHAR),
    element_order INTEGER,
    source_format VARCHAR,              -- Origin format: 'markdown', 'html', etc.
    file_path VARCHAR                   -- Source file path
)
```

---

## Block Builder Functions

Functions that create `duck_block` structs with `kind='block'`.

### db_heading

Creates a heading block.

```sql
db_heading(content VARCHAR, level INTEGER) → duck_block
```

**Parameters:**
- `content`: The heading text
- `level`: Heading level (1-6, where 1 is the largest)

**Example:**
```sql
SELECT db_heading('Introduction', 1);
-- Returns: {kind: 'block', element_type: 'heading', content: 'Introduction', level: 1, ...}
```

---

### db_paragraph

Creates a paragraph block.

```sql
db_paragraph(content VARCHAR) → duck_block
```

**Parameters:**
- `content`: The paragraph text

**Example:**
```sql
SELECT db_paragraph('This is body text.');
```

---

### db_code

Creates a code block.

```sql
db_code(content VARCHAR) → duck_block
db_code(content VARCHAR, language VARCHAR) → duck_block
```

**Parameters:**
- `content`: The code content
- `language`: (Optional) Programming language for syntax highlighting

**Example:**
```sql
SELECT db_code('print("Hello")', 'python');
-- Returns block with attributes: {language: 'python'}
```

---

### db_blockquote

Creates a blockquote block.

```sql
db_blockquote(content VARCHAR) → duck_block
db_blockquote(content VARCHAR, level INTEGER) → duck_block
```

**Parameters:**
- `content`: The quoted text
- `level`: (Optional) Nesting level, defaults to 1

**Example:**
```sql
SELECT db_blockquote('To be or not to be.');
```

---

### db_list_block

Creates a list block with JSON-encoded items.

```sql
db_list_block(items VARCHAR[]) → duck_block
db_list_block(items VARCHAR[], ordered BOOLEAN) → duck_block
```

**Parameters:**
- `items`: Array of list item strings
- `ordered`: (Optional) True for numbered list, false for bullets (default)

**Example:**
```sql
SELECT db_list_block(['First', 'Second', 'Third'], true);
-- Returns block with content: '["First","Second","Third"]'
-- and attributes: {ordered: 'true'}
```

---

### db_hr

Creates a horizontal rule block.

```sql
db_hr() → duck_block
```

**Example:**
```sql
SELECT db_hr();
```

---

### db_metadata

Creates a metadata block (YAML frontmatter).

```sql
db_metadata(yaml_content VARCHAR) → duck_block
```

**Parameters:**
- `yaml_content`: YAML-formatted metadata

**Example:**
```sql
SELECT db_metadata('title: My Document
author: Jane Doe
date: 2024-01-15');
```

---

### db_image

Creates an image block.

```sql
db_image(src VARCHAR) → duck_block
db_image(src VARCHAR, alt VARCHAR) → duck_block
db_image(src VARCHAR, alt VARCHAR, title VARCHAR) → duck_block
```

**Parameters:**
- `src`: Image URL or path
- `alt`: (Optional) Alt text for accessibility
- `title`: (Optional) Image title/tooltip

**Example:**
```sql
SELECT db_image('/images/logo.png', 'Company Logo', 'Our Logo');
-- Returns block with attributes: {src: '/images/logo.png', alt: 'Company Logo', title: 'Our Logo'}
```

---

### db_raw

Creates a raw HTML/XML block.

```sql
db_raw(content VARCHAR) → duck_block
db_raw(content VARCHAR, format VARCHAR) → duck_block
```

**Parameters:**
- `content`: Raw markup content
- `format`: (Optional) Format type ('html' or 'xml'), defaults to 'html'

**Example:**
```sql
SELECT db_raw('<div class="custom">Content</div>', 'html');
```

---

## Inline Builder Functions

Functions that create `duck_block` structs with `kind='inline'`.

### db_text

Creates plain text content.

```sql
db_text(content VARCHAR) → duck_block
```

**Example:**
```sql
SELECT db_text('Hello world');
-- Returns: {kind: 'inline', element_type: 'text', content: 'Hello world', level: 1, ...}
```

---

### db_link

Creates a hyperlink.

```sql
db_link(text VARCHAR, href VARCHAR) → duck_block
db_link(text VARCHAR, href VARCHAR, title VARCHAR) → duck_block
```

**Example:**
```sql
SELECT db_link('Click here', 'https://example.com');
-- Returns: {kind: 'inline', element_type: 'link', content: 'Click here',
--           attributes: {href: 'https://example.com'}, ...}
```

---

### db_bold

Creates bold/strong text.

```sql
db_bold(content VARCHAR) → duck_block
```

---

### db_italic

Creates italic/emphasis text.

```sql
db_italic(content VARCHAR) → duck_block
```

---

### db_inline_code

Creates inline code.

```sql
db_inline_code(content VARCHAR) → duck_block
```

---

### db_inline_image

Creates an inline image.

```sql
db_inline_image(src VARCHAR) → duck_block
db_inline_image(src VARCHAR, alt VARCHAR) → duck_block
db_inline_image(src VARCHAR, alt VARCHAR, title VARCHAR) → duck_block
```

---

### Whitespace Elements

```sql
db_space() → duck_block      -- Word separator
db_softbreak() → duck_block  -- Soft line break
db_linebreak() → duck_block  -- Hard line break
```

---

## Assembly Functions

Functions that combine elements into documents.

### db_assemble

Assigns sequential `element_order` values to a list of elements.

```sql
db_assemble(blocks LIST(duck_block)) → LIST(duck_block)
```

**Parameters:**
- `blocks`: List of elements to assemble

**Returns:** List with `element_order` values set to 0, 1, 2, ...

**Example:**
```sql
SELECT db_assemble([
    db_heading('Title', 1),
    db_paragraph('Content')
]);
-- First block has element_order=0, second has element_order=1
```

---

### db_document

Alias for `db_assemble`. Use for semantic clarity when creating complete documents.

```sql
db_document(blocks LIST(duck_block)) → LIST(duck_block)
```

---

### db_section

Creates a section with a heading followed by content blocks.

```sql
db_section(title VARCHAR, level INTEGER) → LIST(duck_block)
db_section(title VARCHAR, level INTEGER, children LIST(duck_block)) → LIST(duck_block)
```

**Parameters:**
- `title`: Section heading text
- `level`: Heading level (1-6)
- `children`: (Optional) Content blocks for the section

**Example:**
```sql
SELECT db_section('Getting Started', 2, [
    db_paragraph('First, install the extension.'),
    db_code('LOAD duck_block_utils;', 'sql')
]);
-- Returns: [heading, paragraph, code] with sequential element_order
```

---

### db_rebase_levels

Adjusts heading levels by a fixed offset.

```sql
db_rebase_levels(blocks LIST(duck_block), offset INTEGER) → LIST(duck_block)
```

**Parameters:**
- `blocks`: List of elements
- `offset`: Amount to add to heading levels (can be negative)

**Notes:**
- Only affects heading blocks
- Clamps results to valid range (1-6)
- Useful when embedding subdocuments

**Example:**
```sql
-- Demote all headings by 1 level (h1→h2, h2→h3, etc.)
SELECT db_rebase_levels([db_heading('Title', 1)], 1);
-- Returns heading with level=2
```

---

### db_concat

Concatenates two element lists without modifying `element_order`.

```sql
db_concat(blocks1 LIST(duck_block), blocks2 LIST(duck_block)) → LIST(duck_block)
```

**Parameters:**
- `blocks1`: First list of elements
- `blocks2`: Second list of elements

**Notes:**
- Unlike `db_blocks_merge`, does not adjust `element_order`
- Use with `db_assemble` to renumber after concatenation

**Example:**
```sql
SELECT db_assemble(db_concat(
    db_section('Chapter 1', 1, [db_paragraph('...')]),
    db_section('Chapter 2', 1, [db_paragraph('...')])
));
```

---

## Manipulation Functions

Functions that filter, transform, and combine element collections.

### db_blocks_filter

Filters elements to include only specified types.

```sql
db_blocks_filter(blocks LIST(duck_block), types VARCHAR[]) → LIST(duck_block)
```

**Parameters:**
- `blocks`: List of elements to filter
- `types`: Array of element types to include

**Example:**
```sql
SELECT db_blocks_filter(my_blocks, ['heading', 'paragraph']);
-- Returns only heading and paragraph blocks
```

---

### db_blocks_exclude

Filters elements to exclude specified types.

```sql
db_blocks_exclude(blocks LIST(duck_block), types VARCHAR[]) → LIST(duck_block)
```

**Parameters:**
- `blocks`: List of elements to filter
- `types`: Array of element types to exclude

**Example:**
```sql
SELECT db_blocks_exclude(my_blocks, ['hr', 'raw']);
-- Returns all blocks except hr and raw
```

---

### db_blocks_merge

Merges two element lists with automatic `element_order` adjustment.

```sql
db_blocks_merge(blocks1 LIST(duck_block), blocks2 LIST(duck_block)) → LIST(duck_block)
```

**Parameters:**
- `blocks1`: First list of elements
- `blocks2`: Second list of elements (orders will be offset)

**Notes:**
- Second list's `element_order` values are offset by `max(blocks1.element_order) + 1`
- Preserves relative ordering within each list

**Example:**
```sql
SELECT db_blocks_merge(
    [db_heading('Doc 1', 1)],
    [db_heading('Doc 2', 1)]
);
-- First heading has element_order=0, second has element_order=1
```

---

### db_blocks_reorder

Renumbers elements sequentially starting from 0.

```sql
db_blocks_reorder(blocks LIST(duck_block)) → LIST(duck_block)
```

**Parameters:**
- `blocks`: List of elements to renumber

**Notes:**
- Sorts by current `element_order` first
- Assigns new values: 0, 1, 2, ...
- Useful after filtering to eliminate gaps

**Example:**
```sql
SELECT db_blocks_reorder(
    db_blocks_filter(my_blocks, ['heading'])
);
```

---

### db_blocks_slice

Extracts elements within an `element_order` range.

```sql
db_blocks_slice(blocks LIST(duck_block), start INTEGER, end INTEGER) → LIST(duck_block)
```

**Parameters:**
- `blocks`: List of elements
- `start`: Minimum `element_order` (inclusive)
- `end`: Maximum `element_order` (inclusive)

**Example:**
```sql
SELECT db_blocks_slice(my_blocks, 5, 10);
-- Returns blocks with element_order between 5 and 10
```

---

### db_blocks_transform

Apply transformations to element types and content.

```sql
db_blocks_transform(
    blocks LIST(duck_block),
    type_mapping MAP(VARCHAR, VARCHAR),
    content_fn VARCHAR DEFAULT NULL
) → LIST(duck_block)
```

**Parameters:**
- `blocks`: List of elements
- `type_mapping`: Map of old_type → new_type
- `content_fn`: Optional: SQL expression for content transformation

**Example:**
```sql
-- Convert all blockquotes to paragraphs
SELECT db_blocks_transform(
    blocks,
    MAP{'blockquote': 'paragraph'}
);
```

---

## Content Extraction Functions

### db_blocks_to_text

Extract plain text content from elements.

```sql
db_blocks_to_text(blocks LIST(duck_block)) → VARCHAR
db_blocks_to_text(blocks LIST(duck_block), separator VARCHAR) → VARCHAR
```

**Parameters:**
- `blocks`: List of elements
- `separator`: Text between blocks (default: double newline)

**Example:**
```sql
SELECT db_blocks_to_text([
    db_heading('Title', 1),
    db_paragraph('Content')
], '\n');
-- Returns: "Title\nContent"
```

---

### db_blocks_headings

Extract heading information.

```sql
db_blocks_headings(blocks LIST(duck_block)) → LIST(STRUCT(level, title, id, element_order))
```

**Returns:** List of structs containing:
- `level`: Heading level (1-6)
- `title`: Heading text
- `id`: ID attribute if present
- `element_order`: Position in document

**Example:**
```sql
SELECT db_blocks_headings(my_blocks);
-- Returns: [{level: 1, title: 'Introduction', id: '', element_order: 0}, ...]
```

---

### db_blocks_code_blocks

Extract code block information.

```sql
db_blocks_code_blocks(blocks LIST(duck_block)) → LIST(STRUCT(language, content, element_order))
```

**Returns:** List of structs containing:
- `language`: Programming language
- `content`: Code content
- `element_order`: Position in document

---

### db_blocks_stats

Compute statistics by element type.

```sql
db_blocks_stats(blocks LIST(duck_block)) → LIST(STRUCT(element_type, count, total_content_length, avg_content_length))
```

**Returns:** List of structs containing:
- `element_type`: The element type
- `count`: Number of elements of this type
- `total_content_length`: Sum of content lengths
- `avg_content_length`: Average content length

---

## Type Functions

Standard functions for type construction, validation, and field access.

See [Type Functions](type_functions.md) for complete documentation.

### Quick Reference

| Function | Description |
|----------|-------------|
| `duck_block(type, content, ...)` | Full block constructor |
| `duck_block(type, content)` | Simple block constructor |
| `duck_inline(type, content, ...)` | Full inline constructor |
| `duck_block_valid(elem)` | Validate element structure |
| `duck_block_kind(elem)` | Get kind field |
| `duck_block_type(elem)` | Get element_type field |
| `duck_block_content(elem)` | Get content field |
| `duck_block_level(elem)` | Get level field |
| `duck_block_encoding(elem)` | Get encoding field |
| `duck_block_order(elem)` | Get element_order field |
| `duck_block_attr(elem, key)` | Get attribute value |
| `duck_block_set_order(elem, val)` | Set element_order |
| `duck_block_set_content(elem, val)` | Set content |
| `duck_block_set_level(elem, val)` | Set level |

---

## Pandoc Conversion Functions

### pandoc_inlines_to_db_inlines

Convert Pandoc inline JSON to duck_block inlines.

```sql
pandoc_inlines_to_db_inlines(inlines VARCHAR) → LIST(duck_block)
```

**Example:**
```sql
SELECT pandoc_inlines_to_db_inlines('[{"t":"Str","c":"Hello"},{"t":"Space"},{"t":"Strong","c":[{"t":"Str","c":"world"}]}]');
-- Returns list of duck_block inlines
```

---

### db_inlines_to_pandoc

Convert duck_block inlines to Pandoc JSON.

```sql
db_inlines_to_pandoc(inlines LIST(duck_block)) → VARCHAR
```

**Example:**
```sql
SELECT db_inlines_to_pandoc([db_text('Hello')]);
-- Returns: '[{"t":"Str","c":"Hello"}]'
```

---

### pandoc_inlines_to_text

Convert Pandoc inline JSON to text.

```sql
pandoc_inlines_to_text(inlines VARCHAR, mode VARCHAR) → VARCHAR
```

**Parameters:**
- `inlines`: Pandoc inline JSON
- `mode`: 'text' for plain text, 'markdown' for formatted

**Example:**
```sql
SELECT pandoc_inlines_to_text('[{"t":"Str","c":"Hello"},{"t":"Space"},{"t":"Strong","c":[{"t":"Str","c":"world"}]}]', 'markdown');
-- Returns: 'Hello **world**'
```

---

## Terminal Rendering Functions

Render duck_blocks as styled ANSI terminal output. See [Terminal Rendering](rendering.md) for usage, examples, and limitations.

### db_blocks_render_ansi

```sql
db_blocks_render_ansi(blocks LIST(duck_block)) → VARCHAR
db_blocks_render_ansi(blocks LIST(duck_block), width INTEGER) → VARCHAR
```

Native renderer producing UTF-8 text with ANSI SGR escapes, word-wrapped to a
display width. Escape sequences count as zero columns and CJK/emoji as two
(utf8proc); active styles are re-opened across line breaks; tables wrap cells
to fit the width budget. Omit `width` (or pass `<= 0`) to auto-detect the
terminal size (`/dev/tty`, then `$COLUMNS`, then 80). A block's structured
`kind='inline'` children are styled by `element_type`; a loose inline with no
parent block is skipped; `metadata` blocks are suppressed.

```sql
SELECT db_blocks_render_ansi(
    db_heading(1, 'Report')
    || db_paragraph([db_text('All systems '), db_bold('nominal'), db_text('.')]),
    72
);
```

### db_terminal_width

```sql
db_terminal_width() → INTEGER
```

The width `db_blocks_render_ansi` would auto-detect. Volatile.

### PRAGMA duck_block_render

Registers convenience macros (requires the `json` extension, autoloaded):

| Macro | Description |
|-------|-------------|
| `db_render_blocks(blocks)` | Render to ANSI at auto-detected width (delegates to `db_blocks_render_ansi`) |
| `db_render_query(sql)` | Table macro: run `sql` and render the result as an ANSI table (column `rendered`) |
| `db_json_to_table_block(json)` | JSON array of objects → `table` duck_block |
| `db_render_block(type, content, attrs)` | Render a single block element |
| `db_ansi(code, s)` / `db_ansi_inline(s)` | SGR wrapper / inline markdown styler |

## See Also

- [Design Document](design.md) - Architecture and implementation details
- [Duck Blocks Specification](duck_blocks_spec.md) - Unified duck_block type schema
- [DuckDB Markdown Extension](https://github.com/teaguesterling/duckdb_markdown) - Reference implementation
