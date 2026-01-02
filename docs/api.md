# Duck Block Utils - Complete API Reference

Complete reference for all functions provided by the `duck_block_utils` extension.

## Type Definitions

### duck_block (Unified Type)

The core document element type. Both block-level and inline elements use the same type, distinguished by the `kind` field:

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

### Block Types (kind='block')

| element_type | Description | level | encoding | Key Attributes |
|--------------|-------------|-------|----------|----------------|
| `heading` | Section heading | NULL | text | `heading_level` (1-6), `id` |
| `paragraph` | Text paragraph | NULL | text | |
| `code` | Code block | NULL | text | `language` |
| `blockquote` | Quoted content | 1+ | text | |
| `list` | List container | NULL | json | `ordered`, `start` |
| `table` | Table | NULL | json | |
| `hr` | Horizontal rule | NULL | text | |
| `metadata` | YAML frontmatter | 0 | yaml | |
| `image` | Block-level image | NULL | text | `src`, `alt`, `title` |
| `raw` | Raw format content | NULL | html/xml | `format` |

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

## Block Builder Functions

### db_heading

```sql
db_heading(content VARCHAR, level INTEGER) → duck_block
```

Creates a heading block.

**Parameters:**
- `content` - Heading text
- `level` - Heading level (1-6)

**Returns:** `duck_block` with `kind='block'`, `element_type='heading'`

**Note:** The heading level is stored in `attributes['heading_level']` as a string (e.g., `'2'`), not in the `level` field. The `level` field is NULL for headings. This distinguishes semantic heading levels from structural nesting depth used by other elements like blockquotes.

**Example:**
```sql
SELECT db_heading('Introduction', 2);
-- {kind: 'block', element_type: 'heading', content: 'Introduction',
--  level: NULL, attributes: {'heading_level': '2'}, ...}

-- Access heading level:
SELECT db_heading('Title', 1).attributes['heading_level'];
-- Returns: '1'
```

---

### db_paragraph

```sql
db_paragraph(content VARCHAR) → duck_block
```

Creates a paragraph block.

**Example:**
```sql
SELECT db_paragraph('This is body text.');
```

---

### db_code

```sql
db_code(content VARCHAR) → duck_block
db_code(content VARCHAR, language VARCHAR) → duck_block
```

Creates a code block.

**Parameters:**
- `content` - Code content
- `language` - (Optional) Programming language

**Attributes set:** `language`

**Example:**
```sql
SELECT db_code('print("hello")', 'python');
```

---

### db_blockquote

```sql
db_blockquote(content VARCHAR) → duck_block
db_blockquote(content VARCHAR, level INTEGER) → duck_block
```

Creates a blockquote block.

**Parameters:**
- `content` - Quoted text
- `level` - (Optional) Nesting level, defaults to 1

---

### db_list_block

```sql
db_list_block(items VARCHAR[]) → duck_block
db_list_block(items VARCHAR[], ordered BOOLEAN) → duck_block
```

Creates a list block with JSON-encoded items.

**Parameters:**
- `items` - Array of list item strings
- `ordered` - (Optional) True for numbered list, false for bullets (default)

**Attributes set:** `ordered`, `start`

**Example:**
```sql
SELECT db_list_block(['First', 'Second', 'Third'], true);
```

---

### db_hr

```sql
db_hr() → duck_block
```

Creates a horizontal rule block.

---

### db_metadata

```sql
db_metadata(yaml_content VARCHAR) → duck_block
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
db_image(src VARCHAR) → duck_block
db_image(src VARCHAR, alt VARCHAR) → duck_block
db_image(src VARCHAR, alt VARCHAR, title VARCHAR) → duck_block
```

Creates a block-level image.

**Attributes set:** `src`, `alt`, `title`

---

### db_raw

```sql
db_raw(content VARCHAR) → duck_block
db_raw(content VARCHAR, format VARCHAR) → duck_block
```

Creates a raw HTML/XML block.

**Parameters:**
- `content` - Raw markup content
- `format` - (Optional) 'html' or 'xml', defaults to 'html'

---

## Inline Builder Functions

### db_text

```sql
db_text(content VARCHAR) → duck_block
```

Creates plain text content.

**Returns:** `duck_block` with `kind='inline'`, `element_type='text'`

**Example:**
```sql
SELECT db_text('Hello world');
-- {kind: 'inline', element_type: 'text', content: 'Hello world', level: 1, ...}
```

---

### Whitespace Functions

```sql
db_space() → duck_block       -- Word separator
db_softbreak() → duck_block   -- Soft line break
db_linebreak() → duck_block   -- Hard line break
```

All return `duck_block` with `kind='inline'` and empty content.

---

### db_bold

```sql
db_bold(content VARCHAR) → duck_block
```

Creates bold/strong text.

---

### db_italic

```sql
db_italic(content VARCHAR) → duck_block
```

Creates italic/emphasis text.

---

### db_strikethrough

```sql
db_strikethrough(content VARCHAR) → duck_block
```

Creates strikethrough text.

---

### db_superscript

```sql
db_superscript(content VARCHAR) → duck_block
```

Creates superscript text.

---

### db_subscript

```sql
db_subscript(content VARCHAR) → duck_block
```

Creates subscript text.

---

### db_smallcaps

```sql
db_smallcaps(content VARCHAR) → duck_block
```

Creates small capitals text.

---

### db_underline

```sql
db_underline(content VARCHAR) → duck_block
```

Creates underlined text.

---

### db_inline_code

```sql
db_inline_code(content VARCHAR) → duck_block
```

Creates inline code.

**Example:**
```sql
SELECT db_inline_code('print()');
-- {kind: 'inline', element_type: 'code', content: 'print()', ...}
```

---

### db_math

```sql
db_math(content VARCHAR) → duck_block
db_math(content VARCHAR, block_display BOOLEAN) → duck_block
```

Creates a math expression.

**Parameters:**
- `content` - LaTeX math content
- `block_display` - (Optional) True for block display, false for inline (default)

**Attributes set:** `display` ('inline' or 'block')

**Example:**
```sql
SELECT db_math('E=mc^2');
SELECT db_math('\\sum_{i=1}^n x_i', true);
```

---

### db_link

```sql
db_link(text VARCHAR, href VARCHAR) → duck_block
db_link(text VARCHAR, href VARCHAR, title VARCHAR) → duck_block
```

Creates a hyperlink.

**Parameters:**
- `text` - Link text
- `href` - URL
- `title` - (Optional) Link title/tooltip

**Attributes set:** `href`, `title`

**Example:**
```sql
SELECT db_link('Click here', 'https://example.com');
-- {kind: 'inline', element_type: 'link', content: 'Click here',
--  attributes: {'href': 'https://example.com'}, ...}
```

---

### db_inline_image

```sql
db_inline_image(src VARCHAR) → duck_block
db_inline_image(src VARCHAR, alt VARCHAR) → duck_block
db_inline_image(src VARCHAR, alt VARCHAR, title VARCHAR) → duck_block
```

Creates an inline image.

**Parameters:**
- `src` - Image URL or path
- `alt` - (Optional) Alt text (also stored in content)
- `title` - (Optional) Image title

**Attributes set:** `src`, `alt`, `title`

---

### db_quoted

```sql
db_quoted(content VARCHAR) → duck_block
db_quoted(content VARCHAR, quote_type VARCHAR) → duck_block
```

Creates quoted text.

**Parameters:**
- `content` - Quoted text
- `quote_type` - (Optional) 'single' or 'double' (default)

**Attributes set:** `quote_type`

---

### db_cite

```sql
db_cite(key VARCHAR) → duck_block
db_cite(key VARCHAR, prefix VARCHAR) → duck_block
db_cite(key VARCHAR, prefix VARCHAR, suffix VARCHAR) → duck_block
```

Creates a citation reference.

**Parameters:**
- `key` - Citation key
- `prefix` - (Optional) Text before citation
- `suffix` - (Optional) Text after citation

**Attributes set:** `key`, `prefix`, `suffix`

---

### db_note

```sql
db_note(content VARCHAR) → duck_block
```

Creates a footnote.

---

### db_span

```sql
db_span(content VARCHAR) → duck_block
db_span(content VARCHAR, id VARCHAR) → duck_block
db_span(content VARCHAR, id VARCHAR, class VARCHAR) → duck_block
```

Creates a generic inline container.

**Attributes set:** `id`, `class`

---

### db_raw_inline

```sql
db_raw_inline(content VARCHAR) → duck_block
db_raw_inline(content VARCHAR, format VARCHAR) → duck_block
```

Creates raw inline content.

**Parameters:**
- `content` - Raw content
- `format` - (Optional) 'html', 'latex', etc. (default: 'html')

**Attributes set:** `format`

---

## Assembly Functions

### db_assemble

```sql
db_assemble(blocks LIST(duck_block)) → LIST(duck_block)
```

Assigns sequential `element_order` values (0, 1, 2, ...) to a list of elements.

**Example:**
```sql
SELECT db_assemble([
    db_heading('Title', 1),
    db_paragraph('Content')
]);
-- First element gets element_order=0, second gets element_order=1
```

---

### db_document

```sql
db_document(blocks LIST(duck_block)) → LIST(duck_block)
```

Alias for `db_assemble`. Use for semantic clarity when creating complete documents.

---

### db_section

```sql
db_section(title VARCHAR, level INTEGER) → LIST(duck_block)
db_section(title VARCHAR, level INTEGER, children LIST(duck_block)) → LIST(duck_block)
```

Creates a section with a heading followed by content blocks.

**Parameters:**
- `title` - Section heading text
- `level` - Heading level (1-6)
- `children` - (Optional) Content blocks for the section

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

```sql
db_rebase_levels(blocks LIST(duck_block), offset INTEGER) → LIST(duck_block)
```

Adjusts heading levels by a fixed offset.

**Parameters:**
- `blocks` - List of elements
- `offset` - Amount to add to heading levels (can be negative)

**Notes:**
- Only affects heading blocks
- Modifies `attributes['heading_level']`, not the `level` field
- Clamps results to valid range (1-6)
- For backward compatibility, reads from `attributes['heading_level']` first, falling back to `level` field

**Example:**
```sql
SELECT db_rebase_levels([db_heading('Title', 1)], 1)[1].attributes['heading_level'];
-- Returns: '2'
```

---

### db_concat

```sql
db_concat(blocks1 LIST(duck_block), blocks2 LIST(duck_block)) → LIST(duck_block)
```

Concatenates two element lists without modifying `element_order`.

**Notes:**
- Unlike `db_blocks_merge`, does not adjust `element_order`
- Use with `db_assemble` to renumber after concatenation

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

Creates a `duck_block` with `kind='block'` using defaults:
- `level`: NULL
- `encoding`: 'text'
- `attributes`: empty map
- `element_order`: 0

---

### to_duck_block

```sql
to_duck_block(input STRUCT) → duck_block
```

Converts a compatible struct to a `duck_block`.

**Notes:**
- Accepts structs with 2-7 fields
- Missing fields are filled with defaults
- Infers `kind` from context if not provided

---

### duck_block_valid

```sql
duck_block_valid(element duck_block) → BOOLEAN
```

Checks if an element has valid structure and values.

**Validation Rules:**
- `kind` must be 'block' or 'inline'
- `element_type` must be non-empty
- `encoding` must be valid if not NULL
- `element_order` must be non-negative if not NULL
- For headings: `level` must be 1-6
- For inlines: `level` must be >= 1

---

### Field Accessors

```sql
duck_block_type(element duck_block) → VARCHAR      -- Get element_type
duck_block_content(element duck_block) → VARCHAR   -- Get content
duck_block_level(element duck_block) → INTEGER     -- Get level
duck_block_encoding(element duck_block) → VARCHAR  -- Get encoding
duck_block_order(element duck_block) → INTEGER     -- Get element_order
duck_block_attr(element duck_block, key VARCHAR) → VARCHAR  -- Get attribute value
```

---

### Field Setters

Return a new element with one field modified (immutable update):

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

**Example:**
```sql
SELECT db_blocks_filter(blocks, ['heading', 'code']);
```

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

**Parameters:**
- `separator` - Text between blocks (default: double newline)

---

### db_blocks_headings

```sql
db_blocks_headings(blocks LIST(duck_block)) → LIST(STRUCT(level, title, id, element_order))
```

Extract heading information.

---

### db_blocks_code_blocks

```sql
db_blocks_code_blocks(blocks LIST(duck_block)) → LIST(STRUCT(language, content, element_order))
```

Extract code block information.

---

### db_blocks_stats

```sql
db_blocks_stats(blocks LIST(duck_block)) → LIST(STRUCT(element_type, count, total_length, avg_length))
```

Compute statistics by element type.

---

## Pandoc Conversion Functions

### pandoc_inlines_to_db_inlines

```sql
pandoc_inlines_to_db_inlines(json_inlines VARCHAR) → LIST(duck_block)
```

Convert Pandoc inline JSON array to duck_block inlines.

**Example:**
```sql
SELECT pandoc_inlines_to_db_inlines('[{"t":"Str","c":"Hello"},{"t":"Space"},{"t":"Strong","c":[{"t":"Str","c":"world"}]}]');
```

---

### pandoc_inlines_to_text

```sql
pandoc_inlines_to_text(json_inlines VARCHAR, mode VARCHAR) → VARCHAR
```

Convert Pandoc inline JSON to text.

**Parameters:**
- `mode` - 'text' for plain text, 'markdown' for formatted

**Example:**
```sql
SELECT pandoc_inlines_to_text('[{"t":"Str","c":"Hello"},{"t":"Space"},{"t":"Strong","c":[{"t":"Str","c":"world"}]}]', 'markdown');
-- Returns: 'Hello **world**'
```

---

### db_inlines_to_pandoc

```sql
db_inlines_to_pandoc(inlines LIST(duck_block)) → VARCHAR
```

Convert duck_block inlines to Pandoc JSON.

**Example:**
```sql
SELECT db_inlines_to_pandoc([db_text('Hello')]);
-- Returns: '[{"t":"Str","c":"Hello"}]'
```

---

### db_text_to_pandoc

```sql
db_text_to_pandoc(text VARCHAR) → VARCHAR
```

Convert plain text to Pandoc inline JSON.

---

## Implementation Notes for Webbed

When implementing these types in webbed (or any other extension):

1. **Use the same struct field order:**
   - kind (index 0)
   - element_type (index 1)
   - content (index 2)
   - level (index 3)
   - encoding (index 4)
   - attributes (index 5)
   - element_order (index 6)

2. **Field access by index is faster** than by name

3. **Block elements** should have:
   - `kind = 'block'`
   - `level` is NULL for most blocks, including headings
   - For headings: use `attributes['heading_level']` (string '1'-'6')
   - For blockquotes: `level` indicates nesting depth (1+)

4. **Heading level handling:**
   - **Producers** should set `attributes['heading_level']` (e.g., `'2'` for h2)
   - **Consumers** should check `attributes['heading_level']` first, then fall back to `level` field for backward compatibility
   - This separates semantic heading levels (h1-h6) from structural nesting depth

5. **Inline elements** should have:
   - `kind = 'inline'`
   - `level >= 1` (indicates nesting depth in container inlines)
   - `element_order` for position within inline sequence

6. **Whitespace inlines** (space, softbreak, linebreak) have empty content

7. **Container inlines** (bold, italic, link, etc.):
   - Simple case: content field contains the text
   - Complex case: content is empty, children are at level+1

---

## See Also

- [Duck Blocks Specification](duck_blocks_spec.md) - Canonical type specification
- [Block Builders](block_builders.md) - Block construction guide
- [Inline Builders](inline_builders.md) - Inline construction guide
- [Type Functions](type_functions.md) - Type function details
