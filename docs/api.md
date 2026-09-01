# Duck Block Utils - Complete API Reference

Complete reference for all functions provided by the `duck_block_utils` extension.

## Type Definitions

### duck_block

The core document element type. Both block-level and inline elements use the same type, distinguished by the `kind` field:

```sql
STRUCT(
    kind VARCHAR,                       -- 'block', 'inline' or 'value'
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
duck_block_paragraph('Simple text')                              -- VARCHAR
duck_block_paragraph(duck_block_bold('Important'))                       -- single duck_block
duck_block_paragraph([duck_block_text('Click '), duck_block_link(url, 'here')]) -- LIST(duck_block)
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
duck_block_heading(1, 'Title')           -- [{heading, content='Title', level=1}]

-- With inline children
duck_block_heading(1, [duck_block_text('Hello '), duck_block_bold('World')])
-- [{heading, content=NULL, level=1}, {text, content='Hello ', level=2}, {bold, content='World', level=2}]
```

---

## Block Builder Functions

### duck_block_heading

```sql
duck_block_heading(level INTEGER, content duck_block_content) → LIST(duck_block)
```

Creates a heading block.

**Parameters:**
- `level` - Heading level (1-6), stored in `attributes['heading_level']`
- `content` - Heading text or inline children

**Example:**
```sql
SELECT duck_block_heading(2, 'Introduction')[1].attributes['heading_level'];
-- Returns: '2'

SELECT duck_block_heading(1, [duck_block_text('Hello '), duck_block_bold('World')]);
-- Returns list with heading + inline children
```

---

### duck_block_paragraph

```sql
duck_block_paragraph(content duck_block_content) → LIST(duck_block)
```

Creates a paragraph block.

**Example:**
```sql
SELECT duck_block_paragraph('Simple text.');
SELECT duck_block_paragraph([duck_block_text('Click '), duck_block_link('https://example.com', 'here')]);
```

---

### duck_block_code

```sql
duck_block_code(content duck_block_content) → LIST(duck_block)
duck_block_code(language VARCHAR, content duck_block_content) → LIST(duck_block)
```

Creates a code block.

**Parameters:**
- `language` - (Optional, first param when specified) Programming language
- `content` - Code content

**Example:**
```sql
SELECT duck_block_code('print("hello")');
SELECT duck_block_code('python', 'print("hello")');
```

---

### duck_block_blockquote

```sql
duck_block_blockquote(content duck_block_content) → LIST(duck_block)
duck_block_blockquote(level INTEGER, content duck_block_content) → LIST(duck_block)
```

Creates a blockquote block.

**Parameters:**
- `level` - (Optional, first param when specified) Nesting level, defaults to 1
- `content` - Quoted content

---

### duck_block_list_block

```sql
duck_block_list_block(items VARCHAR[]) → LIST(duck_block)
duck_block_list_block(ordered BOOLEAN, items VARCHAR[]) → LIST(duck_block)
```

Creates a list block with simple string items.

**Parameters:**
- `ordered` - (Optional, first param when specified) True for numbered list
- `items` - Array of list item strings

**Example:**
```sql
SELECT duck_block_list_block(['First', 'Second', 'Third']);
SELECT duck_block_list_block(true, ['Step 1', 'Step 2']);
```

---

### duck_block_list_item

```sql
duck_block_list_item(content duck_block_content) → LIST(duck_block)
duck_block_list_item(ordered BOOLEAN, content duck_block_content) → LIST(duck_block)
```

Creates a single list item with rich content.

**Parameters:**
- `ordered` - (Optional, first param when specified) Bullet style
- `content` - Item content (text or inline children)

**Example:**
```sql
SELECT duck_block_list_item('Simple item');
SELECT duck_block_list_item([
    duck_block_link('https://github.com', 'GitHub'),
    duck_block_text(' '),
    duck_block_inline_image('badge.svg', 'CI')
]);
```

---

### duck_block_hr

```sql
duck_block_hr() → LIST(duck_block)
```

Creates a horizontal rule block.

---

### duck_block_metadata

```sql
duck_block_metadata(yaml_content VARCHAR) → LIST(duck_block)
```

Creates a metadata block (YAML frontmatter).

**Example:**
```sql
SELECT duck_block_metadata('title: My Document
author: Jane Doe');
```

---

### duck_block_image

```sql
duck_block_image(src VARCHAR) → LIST(duck_block)
duck_block_image(src VARCHAR, alt VARCHAR) → LIST(duck_block)
duck_block_image(src VARCHAR, alt VARCHAR, title VARCHAR) → LIST(duck_block)
```

Creates a block-level image.

---

### duck_block_raw

```sql
duck_block_raw(content VARCHAR) → LIST(duck_block)
duck_block_raw(format VARCHAR, content VARCHAR) → LIST(duck_block)
```

Creates a raw HTML/XML block.

**Parameters:**
- `format` - (Optional, first param when specified) 'html', 'xml', etc.
- `content` - Raw markup content

---

## Inline Builder Functions

### duck_block_text

```sql
duck_block_text(content VARCHAR) → LIST(duck_block)
```

Creates plain text content.

**Example:**
```sql
SELECT duck_block_text('Hello world');
```

---

### Whitespace Functions

```sql
duck_block_space() → LIST(duck_block)       -- Word separator
duck_block_softbreak() → LIST(duck_block)   -- Soft line break
duck_block_linebreak() → LIST(duck_block)   -- Hard line break
```

---

### duck_block_bold

```sql
duck_block_bold(content duck_block_content) → LIST(duck_block)
```

Creates bold/strong text.

**Example:**
```sql
SELECT duck_block_bold('Important');
SELECT duck_block_bold([duck_block_text('very '), duck_block_italic('important')]);
```

---

### duck_block_italic

```sql
duck_block_italic(content duck_block_content) → LIST(duck_block)
```

Creates italic/emphasis text.

---

### duck_block_strikethrough

```sql
duck_block_strikethrough(content duck_block_content) → LIST(duck_block)
```

Creates strikethrough text.

---

### duck_block_superscript

```sql
duck_block_superscript(content duck_block_content) → LIST(duck_block)
```

Creates superscript text.

---

### duck_block_subscript

```sql
duck_block_subscript(content duck_block_content) → LIST(duck_block)
```

Creates subscript text.

---

### duck_block_smallcaps

```sql
duck_block_smallcaps(content duck_block_content) → LIST(duck_block)
```

Creates small capitals text.

---

### duck_block_underline

```sql
duck_block_underline(content duck_block_content) → LIST(duck_block)
```

Creates underlined text.

---

### duck_block_inline_code

```sql
duck_block_inline_code(content VARCHAR) → LIST(duck_block)
```

Creates inline code.

---

### duck_block_math

```sql
duck_block_math(content VARCHAR) → LIST(duck_block)
duck_block_math(display BOOLEAN, content VARCHAR) → LIST(duck_block)
```

Creates a math expression.

**Parameters:**
- `display` - (Optional, first param when specified) True for block display
- `content` - LaTeX math content

---

### duck_block_link

```sql
duck_block_link(href VARCHAR, content duck_block_content) → LIST(duck_block)
duck_block_link(href VARCHAR, title VARCHAR, content duck_block_content) → LIST(duck_block)
```

Creates a hyperlink.

**Parameters:**
- `href` - URL (first param)
- `title` - (Optional) Link title/tooltip
- `content` - Link text or inline children (last param)

**Example:**
```sql
SELECT duck_block_link('https://example.com', 'Click here');
SELECT duck_block_link('https://example.com', [duck_block_bold('Click'), duck_block_text(' here')]);
```

---

### duck_block_inline_image

```sql
duck_block_inline_image(src VARCHAR) → LIST(duck_block)
duck_block_inline_image(src VARCHAR, alt VARCHAR) → LIST(duck_block)
duck_block_inline_image(src VARCHAR, alt VARCHAR, title VARCHAR) → LIST(duck_block)
```

Creates an inline image.

---

### duck_block_quoted

```sql
duck_block_quoted(content duck_block_content) → LIST(duck_block)
duck_block_quoted(quote_type VARCHAR, content duck_block_content) → LIST(duck_block)
```

Creates quoted text.

**Parameters:**
- `quote_type` - (Optional, first param when specified) 'single' or 'double'
- `content` - Quoted content

---

### duck_block_cite

```sql
duck_block_cite(key VARCHAR) → LIST(duck_block)
```

Creates a citation reference.

---

### duck_block_note

```sql
duck_block_note(content duck_block_content) → LIST(duck_block)
```

Creates a footnote.

---

### duck_block_span

```sql
duck_block_span(content duck_block_content) → LIST(duck_block)
duck_block_span(id VARCHAR, content duck_block_content) → LIST(duck_block)
duck_block_span(id VARCHAR, class VARCHAR, content duck_block_content) → LIST(duck_block)
```

Creates a generic inline container.

---

### duck_block_raw_inline

```sql
duck_block_raw_inline(content VARCHAR) → LIST(duck_block)
duck_block_raw_inline(format VARCHAR, content VARCHAR) → LIST(duck_block)
```

Creates raw inline content.

---

## Assembly Functions

### duck_blocks_assemble

```sql
duck_blocks_assemble(blocks LIST(LIST(duck_block))) → LIST(duck_block)
```

Flattens nested block lists and assigns sequential `element_order` values.

**Example:**
```sql
SELECT duck_blocks_assemble([
    duck_block_heading(1, 'Title'),
    duck_block_paragraph('Content'),
    duck_block_paragraph([duck_block_text('Rich '), duck_block_bold('content')])
]);
-- Flattens to single list with element_order 0, 1, 2, 3, 4
```

---

### duck_blocks_document

```sql
duck_blocks_document(blocks LIST(LIST(duck_block))) → LIST(duck_block)
```

Alias for `duck_blocks_assemble`. Use for semantic clarity when creating complete documents.

---

### duck_block_section

```sql
duck_block_section(level INTEGER, title duck_block_content) → LIST(duck_block)
duck_block_section(level INTEGER, title duck_block_content, children LIST(LIST(duck_block))) → LIST(duck_block)
```

Creates a section with a heading followed by content blocks.

**Parameters:**
- `level` - Heading level (1-6)
- `title` - Section heading (text or inline children)
- `children` - (Optional) Content blocks for the section

**Example:**
```sql
SELECT duck_block_section(2, 'Getting Started', [
    duck_block_paragraph('First, install the extension.'),
    duck_block_code('sql', 'LOAD duck_block_utils;')
]);
```

---

### duck_blocks_rebase_levels

```sql
duck_blocks_rebase_levels(blocks LIST(duck_block), offset INTEGER) → LIST(duck_block)
```

Adjusts heading levels by a fixed offset.

**Parameters:**
- `blocks` - List of elements (flattened)
- `offset` - Amount to add to heading levels (can be negative)

**Notes:**
- Clamps results to valid range (1-6)
- Modifies `attributes['heading_level']`

---

### duck_blocks_concat

```sql
duck_blocks_concat(blocks1 LIST(duck_block), blocks2 LIST(duck_block)) → LIST(duck_block)
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

### duck_blocks_filter

```sql
duck_blocks_filter(blocks LIST(duck_block), types VARCHAR[]) → LIST(duck_block)
```

Filter elements to include only specified types.

---

### duck_blocks_exclude

```sql
duck_blocks_exclude(blocks LIST(duck_block), types VARCHAR[]) → LIST(duck_block)
```

Filter elements to exclude specified types.

---

### duck_blocks_merge

```sql
duck_blocks_merge(blocks1 LIST(duck_block), blocks2 LIST(duck_block)) → LIST(duck_block)
```

Combine two element sequences with automatic `element_order` adjustment.

---

### duck_blocks_reorder

```sql
duck_blocks_reorder(blocks LIST(duck_block)) → LIST(duck_block)
```

Renumber `element_order` values sequentially from 0.

---

### duck_blocks_slice

```sql
duck_blocks_slice(blocks LIST(duck_block), start_order INTEGER, end_order INTEGER) → LIST(duck_block)
```

Extract elements within an `element_order` range (inclusive).

---

## Extraction Functions

### duck_blocks_to_text

```sql
duck_blocks_to_text(blocks LIST(duck_block)) → VARCHAR
duck_blocks_to_text(blocks LIST(duck_block), separator VARCHAR) → VARCHAR
```

Extract plain text content from elements.

---

### duck_blocks_headings

```sql
duck_blocks_headings(blocks LIST(duck_block)) → LIST(STRUCT(level, title, id, element_order))
```

Extract heading information.

---

### duck_blocks_toc

```sql
duck_blocks_toc(blocks LIST(duck_block)) → LIST(STRUCT(level, title, id, indent, element_order))
```

Generate table of contents from headings.

---

### duck_blocks_code_blocks

```sql
duck_blocks_code_blocks(blocks LIST(duck_block)) → LIST(STRUCT(language, content, element_order))
```

Extract code block information.

---

### duck_blocks_links

```sql
duck_blocks_links(blocks LIST(duck_block)) → LIST(STRUCT(href, text, title, element_order))
```

Extract links and images from blocks.

---

### duck_blocks_stats

```sql
duck_blocks_stats(blocks LIST(duck_block)) → LIST(STRUCT(element_type, count, total_content_length))
```

Compute statistics by element type.

---

## Validation Functions

### duck_blocks_validate

```sql
duck_blocks_validate(blocks LIST(duck_block)) → STRUCT(valid BOOLEAN, errors LIST(STRUCT))
```

Check schema compliance.

---

### duck_blocks_lint

```sql
duck_blocks_lint(blocks LIST(duck_block)) → LIST(STRUCT(severity, message, element_order))
```

Check best practices (heading skips, code without language, etc.).

---

### duck_blocks_structure

```sql
duck_blocks_structure(blocks LIST(duck_block)) → STRUCT(...)
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

### duck_blocks_to_pandoc_blocks

```sql
duck_blocks_to_pandoc_blocks(blocks LIST(duck_block)) → VARCHAR
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

### duck_blocks_inlines_to_pandoc

```sql
duck_blocks_inlines_to_pandoc(inlines LIST(duck_block)) → VARCHAR
```

Convert duck_block inlines to Pandoc JSON.

---

## Complete Example

```sql
-- Build a complete document with rich content
SELECT duck_blocks_assemble([
    duck_block_metadata('title: User Guide'),
    duck_block_heading(1, 'Getting Started'),
    duck_block_paragraph([
        duck_block_text('Welcome to '),
        duck_block_bold('DuckDB'),
        duck_block_text(' extensions.')
    ]),
    duck_block_heading(2, 'Installation'),
    duck_block_paragraph([
        duck_block_text('Run: '),
        duck_block_inline_code('INSTALL duck_block_utils')
    ]),
    duck_block_heading(2, 'Resources'),
    duck_block_list_item([
        duck_block_link('https://github.com/org/repo', 'GitHub'),
        duck_block_text(' '),
        duck_block_inline_image('https://img.shields.io/badge.svg', 'CI')
    ]),
    duck_block_hr()
]);
```

---

## See Also

- [V2 API Design](v2_api_design.md) - Design rationale and migration guide
- [Block Builders](block_builders.md) - Block construction guide
- [Inline Builders](inline_builders.md) - Inline construction guide
