# API Reference

Complete reference for all functions in the `duck_block_utils` extension.

## Type Definitions

### doc_element (Unified Type)

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

### doc_element_ext

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

Functions that create `doc_element` structs with `kind='block'`.

### doc_heading

Creates a heading block.

```sql
doc_heading(content VARCHAR, level INTEGER) → doc_element
```

**Parameters:**
- `content`: The heading text
- `level`: Heading level (1-6, where 1 is the largest)

**Example:**
```sql
SELECT doc_heading('Introduction', 1);
-- Returns: {kind: 'block', element_type: 'heading', content: 'Introduction', level: 1, ...}
```

---

### doc_paragraph

Creates a paragraph block.

```sql
doc_paragraph(content VARCHAR) → doc_element
```

**Parameters:**
- `content`: The paragraph text

**Example:**
```sql
SELECT doc_paragraph('This is body text.');
```

---

### doc_code

Creates a code block.

```sql
doc_code(content VARCHAR) → doc_element
doc_code(content VARCHAR, language VARCHAR) → doc_element
```

**Parameters:**
- `content`: The code content
- `language`: (Optional) Programming language for syntax highlighting

**Example:**
```sql
SELECT doc_code('print("Hello")', 'python');
-- Returns block with attributes: {language: 'python'}
```

---

### doc_blockquote

Creates a blockquote block.

```sql
doc_blockquote(content VARCHAR) → doc_element
doc_blockquote(content VARCHAR, level INTEGER) → doc_element
```

**Parameters:**
- `content`: The quoted text
- `level`: (Optional) Nesting level, defaults to 1

**Example:**
```sql
SELECT doc_blockquote('To be or not to be.');
```

---

### doc_list_block

Creates a list block with JSON-encoded items.

```sql
doc_list_block(items VARCHAR[]) → doc_element
doc_list_block(items VARCHAR[], ordered BOOLEAN) → doc_element
```

**Parameters:**
- `items`: Array of list item strings
- `ordered`: (Optional) True for numbered list, false for bullets (default)

**Example:**
```sql
SELECT doc_list_block(['First', 'Second', 'Third'], true);
-- Returns block with content: '["First","Second","Third"]'
-- and attributes: {ordered: 'true'}
```

---

### doc_hr

Creates a horizontal rule block.

```sql
doc_hr() → doc_element
```

**Example:**
```sql
SELECT doc_hr();
```

---

### doc_metadata

Creates a metadata block (YAML frontmatter).

```sql
doc_metadata(yaml_content VARCHAR) → doc_element
```

**Parameters:**
- `yaml_content`: YAML-formatted metadata

**Example:**
```sql
SELECT doc_metadata('title: My Document
author: Jane Doe
date: 2024-01-15');
```

---

### doc_image

Creates an image block.

```sql
doc_image(src VARCHAR) → doc_element
doc_image(src VARCHAR, alt VARCHAR) → doc_element
doc_image(src VARCHAR, alt VARCHAR, title VARCHAR) → doc_element
```

**Parameters:**
- `src`: Image URL or path
- `alt`: (Optional) Alt text for accessibility
- `title`: (Optional) Image title/tooltip

**Example:**
```sql
SELECT doc_image('/images/logo.png', 'Company Logo', 'Our Logo');
-- Returns block with attributes: {src: '/images/logo.png', alt: 'Company Logo', title: 'Our Logo'}
```

---

### doc_raw

Creates a raw HTML/XML block.

```sql
doc_raw(content VARCHAR) → doc_element
doc_raw(content VARCHAR, format VARCHAR) → doc_element
```

**Parameters:**
- `content`: Raw markup content
- `format`: (Optional) Format type ('html' or 'xml'), defaults to 'html'

**Example:**
```sql
SELECT doc_raw('<div class="custom">Content</div>', 'html');
```

---

## Inline Builder Functions

Functions that create `doc_element` structs with `kind='inline'`.

### doc_text

Creates plain text content.

```sql
doc_text(content VARCHAR) → doc_element
```

**Example:**
```sql
SELECT doc_text('Hello world');
-- Returns: {kind: 'inline', element_type: 'text', content: 'Hello world', level: 1, ...}
```

---

### doc_link

Creates a hyperlink.

```sql
doc_link(text VARCHAR, href VARCHAR) → doc_element
doc_link(text VARCHAR, href VARCHAR, title VARCHAR) → doc_element
```

**Example:**
```sql
SELECT doc_link('Click here', 'https://example.com');
-- Returns: {kind: 'inline', element_type: 'link', content: 'Click here',
--           attributes: {href: 'https://example.com'}, ...}
```

---

### doc_bold

Creates bold/strong text.

```sql
doc_bold(content VARCHAR) → doc_element
```

---

### doc_italic

Creates italic/emphasis text.

```sql
doc_italic(content VARCHAR) → doc_element
```

---

### doc_inline_code

Creates inline code.

```sql
doc_inline_code(content VARCHAR) → doc_element
```

---

### doc_inline_image

Creates an inline image.

```sql
doc_inline_image(src VARCHAR) → doc_element
doc_inline_image(src VARCHAR, alt VARCHAR) → doc_element
doc_inline_image(src VARCHAR, alt VARCHAR, title VARCHAR) → doc_element
```

---

### Whitespace Elements

```sql
doc_space() → doc_element      -- Word separator
doc_softbreak() → doc_element  -- Soft line break
doc_linebreak() → doc_element  -- Hard line break
```

---

## Assembly Functions

Functions that combine elements into documents.

### doc_assemble

Assigns sequential `element_order` values to a list of elements.

```sql
doc_assemble(blocks LIST(doc_element)) → LIST(doc_element)
```

**Parameters:**
- `blocks`: List of elements to assemble

**Returns:** List with `element_order` values set to 0, 1, 2, ...

**Example:**
```sql
SELECT doc_assemble([
    doc_heading('Title', 1),
    doc_paragraph('Content')
]);
-- First block has element_order=0, second has element_order=1
```

---

### doc_document

Alias for `doc_assemble`. Use for semantic clarity when creating complete documents.

```sql
doc_document(blocks LIST(doc_element)) → LIST(doc_element)
```

---

### doc_section

Creates a section with a heading followed by content blocks.

```sql
doc_section(title VARCHAR, level INTEGER) → LIST(doc_element)
doc_section(title VARCHAR, level INTEGER, children LIST(doc_element)) → LIST(doc_element)
```

**Parameters:**
- `title`: Section heading text
- `level`: Heading level (1-6)
- `children`: (Optional) Content blocks for the section

**Example:**
```sql
SELECT doc_section('Getting Started', 2, [
    doc_paragraph('First, install the extension.'),
    doc_code('LOAD duck_block_utils;', 'sql')
]);
-- Returns: [heading, paragraph, code] with sequential element_order
```

---

### doc_rebase_levels

Adjusts heading levels by a fixed offset.

```sql
doc_rebase_levels(blocks LIST(doc_element), offset INTEGER) → LIST(doc_element)
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
SELECT doc_rebase_levels([doc_heading('Title', 1)], 1);
-- Returns heading with level=2
```

---

### doc_concat

Concatenates two element lists without modifying `element_order`.

```sql
doc_concat(blocks1 LIST(doc_element), blocks2 LIST(doc_element)) → LIST(doc_element)
```

**Parameters:**
- `blocks1`: First list of elements
- `blocks2`: Second list of elements

**Notes:**
- Unlike `doc_blocks_merge`, does not adjust `element_order`
- Use with `doc_assemble` to renumber after concatenation

**Example:**
```sql
SELECT doc_assemble(doc_concat(
    doc_section('Chapter 1', 1, [doc_paragraph('...')]),
    doc_section('Chapter 2', 1, [doc_paragraph('...')])
));
```

---

## Manipulation Functions

Functions that filter, transform, and combine element collections.

### doc_blocks_filter

Filters elements to include only specified types.

```sql
doc_blocks_filter(blocks LIST(doc_element), types VARCHAR[]) → LIST(doc_element)
```

**Parameters:**
- `blocks`: List of elements to filter
- `types`: Array of element types to include

**Example:**
```sql
SELECT doc_blocks_filter(my_blocks, ['heading', 'paragraph']);
-- Returns only heading and paragraph blocks
```

---

### doc_blocks_exclude

Filters elements to exclude specified types.

```sql
doc_blocks_exclude(blocks LIST(doc_element), types VARCHAR[]) → LIST(doc_element)
```

**Parameters:**
- `blocks`: List of elements to filter
- `types`: Array of element types to exclude

**Example:**
```sql
SELECT doc_blocks_exclude(my_blocks, ['hr', 'raw']);
-- Returns all blocks except hr and raw
```

---

### doc_blocks_merge

Merges two element lists with automatic `element_order` adjustment.

```sql
doc_blocks_merge(blocks1 LIST(doc_element), blocks2 LIST(doc_element)) → LIST(doc_element)
```

**Parameters:**
- `blocks1`: First list of elements
- `blocks2`: Second list of elements (orders will be offset)

**Notes:**
- Second list's `element_order` values are offset by `max(blocks1.element_order) + 1`
- Preserves relative ordering within each list

**Example:**
```sql
SELECT doc_blocks_merge(
    [doc_heading('Doc 1', 1)],
    [doc_heading('Doc 2', 1)]
);
-- First heading has element_order=0, second has element_order=1
```

---

### doc_blocks_reorder

Renumbers elements sequentially starting from 0.

```sql
doc_blocks_reorder(blocks LIST(doc_element)) → LIST(doc_element)
```

**Parameters:**
- `blocks`: List of elements to renumber

**Notes:**
- Sorts by current `element_order` first
- Assigns new values: 0, 1, 2, ...
- Useful after filtering to eliminate gaps

**Example:**
```sql
SELECT doc_blocks_reorder(
    doc_blocks_filter(my_blocks, ['heading'])
);
```

---

### doc_blocks_slice

Extracts elements within an `element_order` range.

```sql
doc_blocks_slice(blocks LIST(doc_element), start INTEGER, end INTEGER) → LIST(doc_element)
```

**Parameters:**
- `blocks`: List of elements
- `start`: Minimum `element_order` (inclusive)
- `end`: Maximum `element_order` (inclusive)

**Example:**
```sql
SELECT doc_blocks_slice(my_blocks, 5, 10);
-- Returns blocks with element_order between 5 and 10
```

---

### doc_blocks_transform

Apply transformations to element types and content.

```sql
doc_blocks_transform(
    blocks LIST(doc_element),
    type_mapping MAP(VARCHAR, VARCHAR),
    content_fn VARCHAR DEFAULT NULL
) → LIST(doc_element)
```

**Parameters:**
- `blocks`: List of elements
- `type_mapping`: Map of old_type → new_type
- `content_fn`: Optional: SQL expression for content transformation

**Example:**
```sql
-- Convert all blockquotes to paragraphs
SELECT doc_blocks_transform(
    blocks,
    MAP{'blockquote': 'paragraph'}
);
```

---

## Content Extraction Functions

### doc_blocks_to_text

Extract plain text content from elements.

```sql
doc_blocks_to_text(blocks LIST(doc_element)) → VARCHAR
doc_blocks_to_text(blocks LIST(doc_element), separator VARCHAR) → VARCHAR
```

**Parameters:**
- `blocks`: List of elements
- `separator`: Text between blocks (default: double newline)

**Example:**
```sql
SELECT doc_blocks_to_text([
    doc_heading('Title', 1),
    doc_paragraph('Content')
], '\n');
-- Returns: "Title\nContent"
```

---

### doc_blocks_headings

Extract heading information.

```sql
doc_blocks_headings(blocks LIST(doc_element)) → LIST(STRUCT(level, title, id, element_order))
```

**Returns:** List of structs containing:
- `level`: Heading level (1-6)
- `title`: Heading text
- `id`: ID attribute if present
- `element_order`: Position in document

**Example:**
```sql
SELECT doc_blocks_headings(my_blocks);
-- Returns: [{level: 1, title: 'Introduction', id: '', element_order: 0}, ...]
```

---

### doc_blocks_code_blocks

Extract code block information.

```sql
doc_blocks_code_blocks(blocks LIST(doc_element)) → LIST(STRUCT(language, content, element_order))
```

**Returns:** List of structs containing:
- `language`: Programming language
- `content`: Code content
- `element_order`: Position in document

---

### doc_blocks_stats

Compute statistics by element type.

```sql
doc_blocks_stats(blocks LIST(doc_element)) → LIST(STRUCT(element_type, count, total_content_length, avg_content_length))
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
| `doc_element_valid(elem)` | Validate element structure |
| `doc_element_kind(elem)` | Get kind field |
| `doc_element_type(elem)` | Get element_type field |
| `doc_element_content(elem)` | Get content field |
| `doc_element_level(elem)` | Get level field |
| `doc_element_encoding(elem)` | Get encoding field |
| `doc_element_order(elem)` | Get element_order field |
| `doc_element_attr(elem, key)` | Get attribute value |
| `doc_element_set_order(elem, val)` | Set element_order |
| `doc_element_set_content(elem, val)` | Set content |
| `doc_element_set_level(elem, val)` | Set level |

---

## Pandoc Conversion Functions

### pandoc_inlines_to_doc_inlines

Convert Pandoc inline JSON to doc_element inlines.

```sql
pandoc_inlines_to_doc_inlines(inlines VARCHAR) → LIST(doc_element)
```

**Example:**
```sql
SELECT pandoc_inlines_to_doc_inlines('[{"t":"Str","c":"Hello"},{"t":"Space"},{"t":"Strong","c":[{"t":"Str","c":"world"}]}]');
-- Returns list of doc_element inlines
```

---

### doc_inlines_to_pandoc

Convert doc_element inlines to Pandoc JSON.

```sql
doc_inlines_to_pandoc(inlines LIST(doc_element)) → VARCHAR
```

**Example:**
```sql
SELECT doc_inlines_to_pandoc([doc_text('Hello')]);
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

## See Also

- [Design Document](design.md) - Architecture and implementation details
- [Duck Blocks Specification](duck_blocks_spec.md) - Unified doc_element type schema
- [DuckDB Markdown Extension](https://github.com/teaguesterling/duckdb_markdown) - Reference implementation
