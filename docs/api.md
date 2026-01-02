# Duck Block Utils - Complete API Reference

Complete reference for all functions provided by the `duck_block_utils` extension.

## Type Definitions

### duck_block

The core document element type. Both block-level and inline elements use the same type, distinguished by the `kind` field:

```sql
STRUCT(
    kind VARCHAR,                       -- 'block' or 'inline'
    element_type VARCHAR,               -- Element type identifier
    content VARCHAR,                    -- Primary content (NULL if has children)
    level INTEGER,                      -- Hierarchy level (1 = top level)
    encoding VARCHAR,                   -- Content encoding: 'text', 'json', 'yaml', 'html', 'xml'
    attributes MAP(VARCHAR, VARCHAR),   -- Type-specific metadata
    element_order INTEGER               -- Position in document (0-indexed)
)
```

### duck_block_content

Union type for flexible content input. All builders accept this type for content parameters:

```sql
UNION(
    text VARCHAR,                       -- Simple text content
    block duck_block,                   -- Single child element
    blocks LIST(duck_block)             -- Multiple child elements
)
```

This allows uniform handling of:
```sql
db_paragraph('Simple text')                              -- VARCHAR
db_paragraph(db_bold('Important'))                       -- single duck_block
db_paragraph([db_text('Click '), db_link(url, 'here')]) -- LIST(duck_block)
```

### Kind Values

| Kind | Description |
|------|-------------|
| `block` | Block-level elements (heading, paragraph, code, list, etc.) |
| `inline` | Inline elements (text, bold, italic, link, etc.) |

### Block Types (kind='block')

| element_type | Description | Key Attributes |
|--------------|-------------|----------------|
| `heading` | Section heading | `heading_level` (1-6), `id` |
| `paragraph` | Text paragraph | |
| `code` | Code block | `language` |
| `blockquote` | Quoted content | |
| `list` | List container | `ordered`, `start` |
| `list_item` | List item | `ordered` |
| `table` | Table | |
| `hr` | Horizontal rule | |
| `metadata` | YAML frontmatter | |
| `image` | Block-level image | `src`, `alt`, `title` |
| `raw` | Raw format content | `format` |

### Inline Types (kind='inline')

| element_type | Description | Key Attributes |
|--------------|-------------|----------------|
| `text` | Plain text | |
| `space` | Word separator | |
| `softbreak` | Soft line break | |
| `linebreak` | Hard line break | |
| `bold` | Strong emphasis | |
| `italic` | Emphasis | |
| `strikethrough` | Struck text | |
| `superscript` | Superscript | |
| `subscript` | Subscript | |
| `smallcaps` | Small capitals | |
| `underline` | Underlined | |
| `code` | Inline code | |
| `math` | Math expression | `display`: inline/block |
| `link` | Hyperlink | `href`, `title` |
| `image` | Inline image | `src`, `alt`, `title` |
| `quoted` | Quoted text | `quote_type`: single/double |
| `cite` | Citation | `key`, `prefix`, `suffix` |
| `note` | Footnote | |
| `span` | Generic container | `id`, `class` |
| `raw` | Raw format | `format` |

---

## Universal Builder Pattern

**All `db_*` builders return `LIST(duck_block)`** and follow a consistent pattern:

- **Config params first, content last**
- **Content can be**: VARCHAR, single duck_block, or LIST(duck_block)
- **When content is VARCHAR**: Parent's `content` field is set
- **When content is children**: Parent's `content` is NULL, children at `level+1`

```sql
-- Simple text
db_heading(1, 'Title')           -- [{heading, content='Title', level=1}]

-- With inline children
db_heading(1, [db_text('Hello '), db_bold('World')])
-- [{heading, content=NULL, level=1}, {text, content='Hello ', level=2}, {bold, content='World', level=2}]
```

---

## Block Builder Functions

### db_heading

```sql
db_heading(level INTEGER, content duck_block_content) → LIST(duck_block)
```

Creates a heading block.

**Parameters:**
- `level` - Heading level (1-6), stored in `attributes['heading_level']`
- `content` - Heading text or inline children

**Example:**
```sql
SELECT db_heading(2, 'Introduction')[1].attributes['heading_level'];
-- Returns: '2'

SELECT db_heading(1, [db_text('Hello '), db_bold('World')]);
-- Returns list with heading + inline children
```

---

### db_paragraph

```sql
db_paragraph(content duck_block_content) → LIST(duck_block)
```

Creates a paragraph block.

**Example:**
```sql
SELECT db_paragraph('Simple text.');
SELECT db_paragraph([db_text('Click '), db_link('https://example.com', 'here')]);
```

---

### db_code

```sql
db_code(content duck_block_content) → LIST(duck_block)
db_code(language VARCHAR, content duck_block_content) → LIST(duck_block)
```

Creates a code block.

**Parameters:**
- `language` - (Optional, first param when specified) Programming language
- `content` - Code content

**Example:**
```sql
SELECT db_code('print("hello")');
SELECT db_code('python', 'print("hello")');
```

---

### db_blockquote

```sql
db_blockquote(content duck_block_content) → LIST(duck_block)
db_blockquote(level INTEGER, content duck_block_content) → LIST(duck_block)
```

Creates a blockquote block.

**Parameters:**
- `level` - (Optional, first param when specified) Nesting level, defaults to 1
- `content` - Quoted content

---

### db_list_block

```sql
db_list_block(items VARCHAR[]) → LIST(duck_block)
db_list_block(ordered BOOLEAN, items VARCHAR[]) → LIST(duck_block)
```

Creates a list block with simple string items.

**Parameters:**
- `ordered` - (Optional, first param when specified) True for numbered list
- `items` - Array of list item strings

**Example:**
```sql
SELECT db_list_block(['First', 'Second', 'Third']);
SELECT db_list_block(true, ['Step 1', 'Step 2']);
```

---

### db_list_item

```sql
db_list_item(content duck_block_content) → LIST(duck_block)
db_list_item(ordered BOOLEAN, content duck_block_content) → LIST(duck_block)
```

Creates a single list item with rich content.

**Parameters:**
- `ordered` - (Optional, first param when specified) Bullet style
- `content` - Item content (text or inline children)

**Example:**
```sql
SELECT db_list_item('Simple item');
SELECT db_list_item([
    db_link('https://github.com', 'GitHub'),
    db_text(' '),
    db_inline_image('badge.svg', 'CI')
]);
```

---

### db_hr

```sql
db_hr() → LIST(duck_block)
```

Creates a horizontal rule block.

---

### db_metadata

```sql
db_metadata(yaml_content VARCHAR) → LIST(duck_block)
```

Creates a metadata block (YAML frontmatter).

**Example:**
```sql
SELECT db_metadata('title: My Document
author: Jane Doe');
```

---

### db_image

```sql
db_image(src VARCHAR) → LIST(duck_block)
db_image(src VARCHAR, alt VARCHAR) → LIST(duck_block)
db_image(src VARCHAR, alt VARCHAR, title VARCHAR) → LIST(duck_block)
```

Creates a block-level image.

---

### db_raw

```sql
db_raw(content VARCHAR) → LIST(duck_block)
db_raw(format VARCHAR, content VARCHAR) → LIST(duck_block)
```

Creates a raw HTML/XML block.

**Parameters:**
- `format` - (Optional, first param when specified) 'html', 'xml', etc.
- `content` - Raw markup content

---

## Inline Builder Functions

### db_text

```sql
db_text(content VARCHAR) → LIST(duck_block)
```

Creates plain text content.

**Example:**
```sql
SELECT db_text('Hello world');
```

---

### Whitespace Functions

```sql
db_space() → LIST(duck_block)       -- Word separator
db_softbreak() → LIST(duck_block)   -- Soft line break
db_linebreak() → LIST(duck_block)   -- Hard line break
```

---

### db_bold

```sql
db_bold(content duck_block_content) → LIST(duck_block)
```

Creates bold/strong text.

**Example:**
```sql
SELECT db_bold('Important');
SELECT db_bold([db_text('very '), db_italic('important')]);
```

---

### db_italic

```sql
db_italic(content duck_block_content) → LIST(duck_block)
```

Creates italic/emphasis text.

---

### db_strikethrough

```sql
db_strikethrough(content duck_block_content) → LIST(duck_block)
```

Creates strikethrough text.

---

### db_superscript

```sql
db_superscript(content duck_block_content) → LIST(duck_block)
```

Creates superscript text.

---

### db_subscript

```sql
db_subscript(content duck_block_content) → LIST(duck_block)
```

Creates subscript text.

---

### db_smallcaps

```sql
db_smallcaps(content duck_block_content) → LIST(duck_block)
```

Creates small capitals text.

---

### db_underline

```sql
db_underline(content duck_block_content) → LIST(duck_block)
```

Creates underlined text.

---

### db_inline_code

```sql
db_inline_code(content VARCHAR) → LIST(duck_block)
```

Creates inline code.

---

### db_math

```sql
db_math(content VARCHAR) → LIST(duck_block)
db_math(display BOOLEAN, content VARCHAR) → LIST(duck_block)
```

Creates a math expression.

**Parameters:**
- `display` - (Optional, first param when specified) True for block display
- `content` - LaTeX math content

---

### db_link

```sql
db_link(href VARCHAR, content duck_block_content) → LIST(duck_block)
db_link(href VARCHAR, title VARCHAR, content duck_block_content) → LIST(duck_block)
```

Creates a hyperlink.

**Parameters:**
- `href` - URL (first param)
- `title` - (Optional) Link title/tooltip
- `content` - Link text or inline children (last param)

**Example:**
```sql
SELECT db_link('https://example.com', 'Click here');
SELECT db_link('https://example.com', [db_bold('Click'), db_text(' here')]);
```

---

### db_inline_image

```sql
db_inline_image(src VARCHAR) → LIST(duck_block)
db_inline_image(src VARCHAR, alt VARCHAR) → LIST(duck_block)
db_inline_image(src VARCHAR, alt VARCHAR, title VARCHAR) → LIST(duck_block)
```

Creates an inline image.

---

### db_quoted

```sql
db_quoted(content duck_block_content) → LIST(duck_block)
db_quoted(quote_type VARCHAR, content duck_block_content) → LIST(duck_block)
```

Creates quoted text.

**Parameters:**
- `quote_type` - (Optional, first param when specified) 'single' or 'double'
- `content` - Quoted content

---

### db_cite

```sql
db_cite(key VARCHAR) → LIST(duck_block)
```

Creates a citation reference.

---

### db_note

```sql
db_note(content duck_block_content) → LIST(duck_block)
```

Creates a footnote.

---

### db_span

```sql
db_span(content duck_block_content) → LIST(duck_block)
db_span(id VARCHAR, content duck_block_content) → LIST(duck_block)
db_span(id VARCHAR, class VARCHAR, content duck_block_content) → LIST(duck_block)
```

Creates a generic inline container.

---

### db_raw_inline

```sql
db_raw_inline(content VARCHAR) → LIST(duck_block)
db_raw_inline(format VARCHAR, content VARCHAR) → LIST(duck_block)
```

Creates raw inline content.

---

## Assembly Functions

### db_assemble

```sql
db_assemble(blocks LIST(LIST(duck_block))) → LIST(duck_block)
```

Flattens nested block lists and assigns sequential `element_order` values.

**Example:**
```sql
SELECT db_assemble([
    db_heading(1, 'Title'),
    db_paragraph('Content'),
    db_paragraph([db_text('Rich '), db_bold('content')])
]);
-- Flattens to single list with element_order 0, 1, 2, 3, 4
```

---

### db_document

```sql
db_document(blocks LIST(LIST(duck_block))) → LIST(duck_block)
```

Alias for `db_assemble`. Use for semantic clarity when creating complete documents.

---

### db_section

```sql
db_section(level INTEGER, title duck_block_content) → LIST(duck_block)
db_section(level INTEGER, title duck_block_content, children LIST(LIST(duck_block))) → LIST(duck_block)
```

Creates a section with a heading followed by content blocks.

**Parameters:**
- `level` - Heading level (1-6)
- `title` - Section heading (text or inline children)
- `children` - (Optional) Content blocks for the section

**Example:**
```sql
SELECT db_section(2, 'Getting Started', [
    db_paragraph('First, install the extension.'),
    db_code('sql', 'LOAD duck_block_utils;')
]);
```

---

### db_rebase_levels

```sql
db_rebase_levels(blocks LIST(duck_block), offset INTEGER) → LIST(duck_block)
```

Adjusts heading levels by a fixed offset.

**Parameters:**
- `blocks` - List of elements (flattened)
- `offset` - Amount to add to heading levels (can be negative)

**Notes:**
- Clamps results to valid range (1-6)
- Modifies `attributes['heading_level']`

---

### db_concat

```sql
db_concat(blocks1 LIST(duck_block), blocks2 LIST(duck_block)) → LIST(duck_block)
```

Concatenates two element lists without modifying `element_order`.

---

## Type Functions

### duck_block (Full Constructor)

```sql
duck_block(
    element_type VARCHAR,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR,
    attributes MAP(VARCHAR, VARCHAR),
    element_order INTEGER
) → duck_block
```

Creates a `duck_block` with `kind='block'` and all fields specified.

---

### duck_block (Simple Constructor)

```sql
duck_block(element_type VARCHAR, content VARCHAR) → duck_block
```

Creates a `duck_block` with `kind='block'` using defaults.

---

### duck_block_valid

```sql
duck_block_valid(element duck_block) → BOOLEAN
```

Checks if an element has valid structure and values.

---

### Field Accessors

```sql
duck_block_type(element duck_block) → VARCHAR
duck_block_content(element duck_block) → VARCHAR
duck_block_level(element duck_block) → INTEGER
duck_block_encoding(element duck_block) → VARCHAR
duck_block_order(element duck_block) → INTEGER
duck_block_attr(element duck_block, key VARCHAR) → VARCHAR
```

---

### Field Setters

```sql
duck_block_set_order(element duck_block, new_order INTEGER) → duck_block
duck_block_set_content(element duck_block, new_content VARCHAR) → duck_block
duck_block_set_level(element duck_block, new_level INTEGER) → duck_block
```

---

## Manipulation Functions

### db_blocks_filter

```sql
db_blocks_filter(blocks LIST(duck_block), types VARCHAR[]) → LIST(duck_block)
```

Filter elements to include only specified types.

---

### db_blocks_exclude

```sql
db_blocks_exclude(blocks LIST(duck_block), types VARCHAR[]) → LIST(duck_block)
```

Filter elements to exclude specified types.

---

### db_blocks_merge

```sql
db_blocks_merge(blocks1 LIST(duck_block), blocks2 LIST(duck_block)) → LIST(duck_block)
```

Combine two element sequences with automatic `element_order` adjustment.

---

### db_blocks_reorder

```sql
db_blocks_reorder(blocks LIST(duck_block)) → LIST(duck_block)
```

Renumber `element_order` values sequentially from 0.

---

### db_blocks_slice

```sql
db_blocks_slice(blocks LIST(duck_block), start_order INTEGER, end_order INTEGER) → LIST(duck_block)
```

Extract elements within an `element_order` range (inclusive).

---

## Extraction Functions

### db_blocks_to_text

```sql
db_blocks_to_text(blocks LIST(duck_block)) → VARCHAR
db_blocks_to_text(blocks LIST(duck_block), separator VARCHAR) → VARCHAR
```

Extract plain text content from elements.

---

### db_blocks_headings

```sql
db_blocks_headings(blocks LIST(duck_block)) → LIST(STRUCT(level, title, id, element_order))
```

Extract heading information.

---

### db_blocks_toc

```sql
db_blocks_toc(blocks LIST(duck_block)) → LIST(STRUCT(level, title, id, indent, element_order))
```

Generate table of contents from headings.

---

### db_blocks_code_blocks

```sql
db_blocks_code_blocks(blocks LIST(duck_block)) → LIST(STRUCT(language, content, element_order))
```

Extract code block information.

---

### db_blocks_links

```sql
db_blocks_links(blocks LIST(duck_block)) → LIST(STRUCT(href, text, title, element_order))
```

Extract links and images from blocks.

---

### db_blocks_stats

```sql
db_blocks_stats(blocks LIST(duck_block)) → LIST(STRUCT(element_type, count, total_content_length))
```

Compute statistics by element type.

---

## Validation Functions

### db_blocks_validate

```sql
db_blocks_validate(blocks LIST(duck_block)) → STRUCT(valid BOOLEAN, errors LIST(STRUCT))
```

Check schema compliance.

---

### db_blocks_lint

```sql
db_blocks_lint(blocks LIST(duck_block)) → LIST(STRUCT(severity, message, element_order))
```

Check best practices (heading skips, code without language, etc.).

---

### db_blocks_structure

```sql
db_blocks_structure(blocks LIST(duck_block)) → STRUCT(...)
```

Return document summary (block counts, heading levels, etc.).

---

## Pandoc Conversion Functions

### pandoc_ast_to_blocks

```sql
pandoc_ast_to_blocks(json VARCHAR) → LIST(duck_block)
```

Convert Pandoc JSON AST to duck_blocks.

---

### pandoc_blocks_to_ast

```sql
pandoc_blocks_to_ast(blocks LIST(duck_block)) → VARCHAR
```

Convert duck_blocks to Pandoc JSON AST.

---

### pandoc_inlines_to_db_inlines

```sql
pandoc_inlines_to_db_inlines(json_inlines VARCHAR) → LIST(duck_block)
```

Convert Pandoc inline JSON array to duck_block inlines.

---

### pandoc_inlines_to_text

```sql
pandoc_inlines_to_text(json_inlines VARCHAR, mode VARCHAR) → VARCHAR
```

Convert Pandoc inline JSON to text ('text' or 'markdown' mode).

---

### db_inlines_to_pandoc

```sql
db_inlines_to_pandoc(inlines LIST(duck_block)) → VARCHAR
```

Convert duck_block inlines to Pandoc JSON.

---

## Complete Example

```sql
-- Build a complete document with rich content
SELECT db_assemble([
    db_metadata('title: User Guide'),
    db_heading(1, 'Getting Started'),
    db_paragraph([
        db_text('Welcome to '),
        db_bold('DuckDB'),
        db_text(' extensions.')
    ]),
    db_heading(2, 'Installation'),
    db_paragraph([
        db_text('Run: '),
        db_inline_code('INSTALL duck_block_utils')
    ]),
    db_heading(2, 'Resources'),
    db_list_item([
        db_link('https://github.com/org/repo', 'GitHub'),
        db_text(' '),
        db_inline_image('https://img.shields.io/badge.svg', 'CI')
    ]),
    db_hr()
]);
```

---

## See Also

- [V2 API Design](v2_api_design.md) - Design rationale and migration guide
- [Block Builders](block_builders.md) - Block construction guide
- [Inline Builders](inline_builders.md) - Inline construction guide
