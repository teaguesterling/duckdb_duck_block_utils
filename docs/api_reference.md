# API Reference

Complete reference for all functions in the `duck_block_utils` extension.

## Builder Functions

Functions that create individual `doc_block` structs.

### doc_heading

Creates a heading block.

```sql
doc_heading(content VARCHAR, level INTEGER) → doc_block
```

**Parameters:**
- `content`: The heading text
- `level`: Heading level (1-6, where 1 is the largest)

**Example:**
```sql
SELECT doc_heading('Introduction', 1);
-- Returns: {block_type: heading, content: Introduction, level: 1, ...}
```

---

### doc_paragraph

Creates a paragraph block.

```sql
doc_paragraph(content VARCHAR) → doc_block
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
doc_code(content VARCHAR) → doc_block
doc_code(content VARCHAR, language VARCHAR) → doc_block
```

**Parameters:**
- `content`: The code content
- `language`: (Optional) Programming language for syntax highlighting

**Example:**
```sql
SELECT doc_code('print("Hello")', 'python');
-- Returns block with attributes: {language: python}
```

---

### doc_blockquote

Creates a blockquote block.

```sql
doc_blockquote(content VARCHAR) → doc_block
doc_blockquote(content VARCHAR, level INTEGER) → doc_block
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
doc_list_block(items VARCHAR[]) → doc_block
doc_list_block(items VARCHAR[], ordered BOOLEAN) → doc_block
```

**Parameters:**
- `items`: Array of list item strings
- `ordered`: (Optional) True for numbered list, false for bullets (default)

**Example:**
```sql
SELECT doc_list_block(['First', 'Second', 'Third'], true);
-- Returns block with content: ["First","Second","Third"]
-- and attributes: {ordered: true}
```

---

### doc_hr

Creates a horizontal rule block.

```sql
doc_hr() → doc_block
```

**Example:**
```sql
SELECT doc_hr();
```

---

### doc_metadata

Creates a metadata block (YAML frontmatter).

```sql
doc_metadata(yaml_content VARCHAR) → doc_block
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
doc_image(src VARCHAR) → doc_block
doc_image(src VARCHAR, alt VARCHAR) → doc_block
doc_image(src VARCHAR, alt VARCHAR, title VARCHAR) → doc_block
```

**Parameters:**
- `src`: Image URL or path
- `alt`: (Optional) Alt text for accessibility
- `title`: (Optional) Image title/tooltip

**Example:**
```sql
SELECT doc_image('/images/logo.png', 'Company Logo', 'Our Logo');
-- Returns block with attributes: {src: /images/logo.png, alt: Company Logo, title: Our Logo}
```

---

### doc_raw

Creates a raw HTML/XML block.

```sql
doc_raw(content VARCHAR) → doc_block
doc_raw(content VARCHAR, format VARCHAR) → doc_block
```

**Parameters:**
- `content`: Raw markup content
- `format`: (Optional) Format type ('html' or 'xml'), defaults to 'html'

**Example:**
```sql
SELECT doc_raw('<div class="custom">Content</div>', 'html');
```

---

## Assembly Functions

Functions that combine blocks into documents.

### doc_assemble

Assigns sequential `block_order` values to a list of blocks.

```sql
doc_assemble(blocks LIST(doc_block)) → LIST(doc_block)
```

**Parameters:**
- `blocks`: List of blocks to assemble

**Returns:** List with `block_order` values set to 0, 1, 2, ...

**Example:**
```sql
SELECT doc_assemble([
    doc_heading('Title', 1),
    doc_paragraph('Content')
]);
-- First block has block_order=0, second has block_order=1
```

---

### doc_document

Alias for `doc_assemble`. Use for semantic clarity when creating complete documents.

```sql
doc_document(blocks LIST(doc_block)) → LIST(doc_block)
```

---

### doc_section

Creates a section with a heading followed by content blocks.

```sql
doc_section(title VARCHAR, level INTEGER) → LIST(doc_block)
doc_section(title VARCHAR, level INTEGER, children LIST(doc_block)) → LIST(doc_block)
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
-- Returns: [heading, paragraph, code] with sequential block_order
```

---

### doc_rebase_levels

Adjusts heading levels by a fixed offset.

```sql
doc_rebase_levels(blocks LIST(doc_block), offset INTEGER) → LIST(doc_block)
```

**Parameters:**
- `blocks`: List of blocks
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

Concatenates two block lists without modifying `block_order`.

```sql
doc_concat(blocks1 LIST(doc_block), blocks2 LIST(doc_block)) → LIST(doc_block)
```

**Parameters:**
- `blocks1`: First list of blocks
- `blocks2`: Second list of blocks

**Notes:**
- Unlike `doc_blocks_merge`, does not adjust `block_order`
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

Functions that filter, transform, and combine block collections.

### doc_blocks_filter

Filters blocks to include only specified types.

```sql
doc_blocks_filter(blocks LIST(doc_block), types VARCHAR[]) → LIST(doc_block)
```

**Parameters:**
- `blocks`: List of blocks to filter
- `types`: Array of block types to include

**Example:**
```sql
SELECT doc_blocks_filter(my_blocks, ['heading', 'paragraph']);
-- Returns only heading and paragraph blocks
```

---

### doc_blocks_exclude

Filters blocks to exclude specified types.

```sql
doc_blocks_exclude(blocks LIST(doc_block), types VARCHAR[]) → LIST(doc_block)
```

**Parameters:**
- `blocks`: List of blocks to filter
- `types`: Array of block types to exclude

**Example:**
```sql
SELECT doc_blocks_exclude(my_blocks, ['hr', 'raw']);
-- Returns all blocks except hr and raw
```

---

### doc_blocks_merge

Merges two block lists with automatic `block_order` adjustment.

```sql
doc_blocks_merge(blocks1 LIST(doc_block), blocks2 LIST(doc_block)) → LIST(doc_block)
```

**Parameters:**
- `blocks1`: First list of blocks
- `blocks2`: Second list of blocks (orders will be offset)

**Notes:**
- Second list's `block_order` values are offset by `max(blocks1.block_order) + 1`
- Preserves relative ordering within each list

**Example:**
```sql
SELECT doc_blocks_merge(
    [doc_heading('Doc 1', 1)],
    [doc_heading('Doc 2', 1)]
);
-- First heading has block_order=0, second has block_order=1
```

---

### doc_blocks_reorder

Renumbers blocks sequentially starting from 0.

```sql
doc_blocks_reorder(blocks LIST(doc_block)) → LIST(doc_block)
```

**Parameters:**
- `blocks`: List of blocks to renumber

**Notes:**
- Sorts by current `block_order` first
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

Extracts blocks within a `block_order` range.

```sql
doc_blocks_slice(blocks LIST(doc_block), start INTEGER, end INTEGER) → LIST(doc_block)
```

**Parameters:**
- `blocks`: List of blocks
- `start`: Minimum `block_order` (inclusive)
- `end`: Maximum `block_order` (inclusive)

**Example:**
```sql
SELECT doc_blocks_slice(my_blocks, 5, 10);
-- Returns blocks with block_order between 5 and 10
```

---

## Extraction Functions

Functions that extract information from block collections.

### doc_blocks_to_text

Converts blocks to plain text.

```sql
doc_blocks_to_text(blocks LIST(doc_block)) → VARCHAR
doc_blocks_to_text(blocks LIST(doc_block), separator VARCHAR) → VARCHAR
```

**Parameters:**
- `blocks`: List of blocks
- `separator`: (Optional) Text between blocks, defaults to `\n\n`

**Notes:**
- Skips `hr` and `raw` blocks
- Skips blocks with empty content

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

Extracts heading information.

```sql
doc_blocks_headings(blocks LIST(doc_block)) → LIST(STRUCT(level, title, id, block_order))
```

**Returns:** List of structs containing:
- `level`: Heading level (1-6)
- `title`: Heading text
- `id`: ID attribute if present
- `block_order`: Position in document

**Example:**
```sql
SELECT doc_blocks_headings(my_blocks);
-- Returns: [{level: 1, title: Introduction, id: , block_order: 0}, ...]
```

---

### doc_blocks_code_blocks

Extracts code block information.

```sql
doc_blocks_code_blocks(blocks LIST(doc_block)) → LIST(STRUCT(language, content, block_order))
```

**Returns:** List of structs containing:
- `language`: Programming language
- `content`: Code content
- `block_order`: Position in document

**Example:**
```sql
SELECT doc_blocks_code_blocks(my_blocks);
```

---

### doc_blocks_stats

Computes statistics by block type.

```sql
doc_blocks_stats(blocks LIST(doc_block)) → LIST(STRUCT(block_type, count, total_content_length, avg_content_length))
```

**Returns:** List of structs containing:
- `block_type`: The block type
- `count`: Number of blocks of this type
- `total_content_length`: Sum of content lengths
- `avg_content_length`: Average content length

**Example:**
```sql
SELECT doc_blocks_stats(my_blocks);
-- Returns: [{block_type: paragraph, count: 10, total_content_length: 5000, avg_content_length: 500.0}, ...]
```

---

## Type Functions

Standard functions for type construction, validation, and field access.

See [Type Functions](type_functions.md) for complete documentation.

### Quick Reference

| Function | Description |
|----------|-------------|
| `duck_block(type, content, ...)` | Full constructor |
| `duck_block(type, content)` | Simple constructor |
| `duck_block_valid(block)` | Validate block structure |
| `duck_block_type(block)` | Get block_type field |
| `duck_block_content(block)` | Get content field |
| `duck_block_level(block)` | Get level field |
| `duck_block_encoding(block)` | Get encoding field |
| `duck_block_order(block)` | Get block_order field |
| `duck_block_attr(block, key)` | Get attribute value |
| `duck_block_set_order(block, val)` | Set block_order |
| `duck_block_set_content(block, val)` | Set content |
| `duck_block_set_level(block, val)` | Set level |
