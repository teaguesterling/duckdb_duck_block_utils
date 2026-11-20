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

## Compact Line Format

A small text form of a duck_block list: one JSON object per line, one line per element,
in `element_order` order. Field names are one letter, types and attribute keys are short
codes, and NULL or default fields are left out. The line number, from 0, is the order,
so `element_order` is not written. It reads back with plain DuckDB SQL.

Measured on 16 duckdb_panduck fixtures: 0.52 of pandoc JSON raw and 0.91 with zstd,
against 1.68 and 1.37 for `COPY ... (FORMAT json)` of the same rows. The design and
measurements are in duckdb_panduck
`docs/superpowers/specs/2026-11-19-compact-blocks-line-format-design.md`.

### duck_blocks_to_compact

```sql
duck_blocks_to_compact(blocks LIST(duck_block)) → VARCHAR
```

Write the list as compact lines. Every line ends with a newline, including the last, so
outputs concatenate. An empty list gives `''`.

```sql
SELECT duck_blocks_to_compact([
  {'kind':'block','element_type':'heading','content':'Heading Two','level':1,'encoding':'text',
   'attributes':MAP{'heading_level':'2','id':'heading-two'},'element_order':0},
  {'kind':'inline','element_type':'image','content':NULL,'level':2,'encoding':'text',
   'attributes':MAP{'src':'cat.png','width':'200px'},'element_order':1}]::duck_block[]);
```

```
{"t":"h","c":"Heading Two","l":1,"a":{"h":"2","i":"heading-two"}}
{"t":"g","l":2,"a":{"u":"cat.png"},"m":{"width":"200px"}}
```

The format does not carry a NULL `level`, `encoding`, `attributes` map or attribute
value. The writer stops with an error on one, rather than writing a default the element
never had. An encoding named `t` or `j` is also refused, because those are codes.

To write a file, trim the last newline (the CSV writer adds its own) and write the value
unquoted:

```sql
COPY (SELECT rtrim(duck_blocks_to_compact(blocks), chr(10)) FROM doc)
TO 'doc.compact.jsonl' (FORMAT csv, HEADER false, QUOTE '', ESCAPE '', DELIMITER E'\x01');
```

### duck_blocks_from_compact

```sql
duck_blocks_from_compact(compact VARCHAR) → LIST(duck_block)
```

Read compact lines back. `element_order` is the line's position, from 0, counted over
non-empty lines, so input order gaps come back dense. Empty lines are skipped. A line
without `l`, or with a type code that is neither in the table nor of the form
`kind:element_type`, is refused with an error.

`attributes` comes back sorted by key. `a` and `m` are two objects, so the format cannot
keep the source's key order across them. DuckDB compares MAPs in key order, so an
element whose map was not sorted reads back equal to the sorted map, not to itself.

### read_compact_blocks

```sql
read_compact_blocks(path VARCHAR) → TABLE(kind, element_type, content, level, encoding,
                                          attributes, element_order, filename)
```

Read one or more compact files (a glob works) as rows: the seven canonical fields, then
`filename` (spec 6.4), so `list(b)` binds as a duck_block list. Rows are ordered by
`filename`, then `element_order`.

### The fields

| Field | Column | When it is written |
|---|---|---|
| `t` | `kind` + `element_type` | Always. A type code, or `kind:element_type` for a type with no code |
| `c` | `content` | When not NULL. `""` is written, so NULL and empty stay distinct |
| `l` | `level` | Always |
| `e` | `encoding` | When not `text`. `json` is written as `j` |
| `a` | `attributes`, known keys | When the element has one. An object of one-letter codes |
| `m` | `attributes`, other keys | When the element has one. Keys unchanged |

`a` and `m` are separate, so an unknown key can never collide with a code, even an
unknown key that is itself one letter. The codes in `a` are unique when case is ignored,
because DuckDB treats STRUCT field names that way.

### Type codes

Type codes are values, so case is significant: `C` is a code block, `c` is inline code.

| Code | Type | Code | Type | Code | Type |
|---|---|---|---|---|---|
| `h` | block heading | `x` | inline text | `v` | inline subscript |
| `p` | block paragraph | `S` | inline space | `u` | inline superscript |
| `i` | block list_item | `b` | inline bold | `z` | inline cite |
| `l` | block list | `e` | inline italic | `w` | inline raw |
| `q` | block blockquote | `c` | inline code | `o` | inline caption |
| `C` | block code | `k` | inline strikethrough | `y` | inline span |
| `d` | block div | `a` | inline link | `B` | inline linebreak |
| `T` | block table | `N` | inline note | `j` | inline softbreak |
| `R` | block raw | `g` | inline image | `V` | value inlines |
| `r` | block hr | `s` | block section | `L` | value list |
| `f` | block figure | `P` | block plain | `Z` | value string |
| `n` | block caption | | | `M` | value map |

### Attribute codes

| Code | Key | Code | Key | Code | Key |
|---|---|---|---|---|---|
| `c` | class | `k` | key | `n` | start |
| `i` | id | `r` | role | `m` | template_name |
| `s` | source_type | `w` | href | `a` | alt |
| `h` | heading_level | `f` | format | `l` | link_type |
| `t` | list_type | `u` | src | `p` | pandoc_ast |
| `o` | ordered | `g` | language | `x` | execution_count |
| `d` | number_delim | `y` | number_style | `e` | title |

The source of both tables is the macro text in `src/doc_macros.cpp`. Adding a code
changes the format: a file written with a longer table puts that key in `a`, where an
older reader does not look for it. Until a key has a code, it goes in `m`, and every
reader keeps it.

---

## Pandoc Conversion Functions

Moved to [panduck](https://github.com/teaguesterling/duckdb_panduck) with the rest of the
Pandoc converter. `LOAD panduck` for `pandoc_ast_to_blocks`, `read_pandoc_ast`,
`write_pandoc_ast`, `duck_blocks_to_pandoc_ast`, `duck_blocks_to_pandoc_blocks`, `pandoc_ast`,
`pandoc_inlines_to_db_inlines`, `pandoc_inlines_to_text` and
`duck_blocks_inlines_to_pandoc`, under the same names. duck_block_utils no longer
registers any of them.

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
