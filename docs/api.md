# Duck Block Utils - Complete API Reference

Complete reference for all functions provided by the `duck_block_utils` extension.

## Type Definitions

### doc_element (Unified Type)

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

### doc_heading

```sql
doc_heading(content VARCHAR, level INTEGER) → doc_element
```

Creates a heading block.

**Parameters:**
- `content` - Heading text
- `level` - Heading level (1-6)

**Returns:** `doc_element` with `kind='block'`, `element_type='heading'`

**Note:** The heading level is stored in `attributes['heading_level']` as a string (e.g., `'2'`), not in the `level` field. The `level` field is NULL for headings. This distinguishes semantic heading levels from structural nesting depth used by other elements like blockquotes.

**Example:**
```sql
SELECT doc_heading('Introduction', 2);
-- {kind: 'block', element_type: 'heading', content: 'Introduction',
--  level: NULL, attributes: {'heading_level': '2'}, ...}

-- Access heading level:
SELECT doc_heading('Title', 1).attributes['heading_level'];
-- Returns: '1'
```

---

### doc_paragraph

```sql
doc_paragraph(content VARCHAR) → doc_element
```

Creates a paragraph block.

**Example:**
```sql
SELECT doc_paragraph('This is body text.');
```

---

### doc_code

```sql
doc_code(content VARCHAR) → doc_element
doc_code(content VARCHAR, language VARCHAR) → doc_element
```

Creates a code block.

**Parameters:**
- `content` - Code content
- `language` - (Optional) Programming language

**Attributes set:** `language`

**Example:**
```sql
SELECT doc_code('print("hello")', 'python');
```

---

### doc_blockquote

```sql
doc_blockquote(content VARCHAR) → doc_element
doc_blockquote(content VARCHAR, level INTEGER) → doc_element
```

Creates a blockquote block.

**Parameters:**
- `content` - Quoted text
- `level` - (Optional) Nesting level, defaults to 1

---

### doc_list_block

```sql
doc_list_block(items VARCHAR[]) → doc_element
doc_list_block(items VARCHAR[], ordered BOOLEAN) → doc_element
```

Creates a list block with JSON-encoded items.

**Parameters:**
- `items` - Array of list item strings
- `ordered` - (Optional) True for numbered list, false for bullets (default)

**Attributes set:** `ordered`, `start`

**Example:**
```sql
SELECT doc_list_block(['First', 'Second', 'Third'], true);
```

---

### doc_hr

```sql
doc_hr() → doc_element
```

Creates a horizontal rule block.

---

### doc_metadata

```sql
doc_metadata(yaml_content VARCHAR) → doc_element
```

Creates a metadata block (YAML frontmatter).

**Example:**
```sql
SELECT doc_metadata('title: My Document
author: Jane Doe');
```

---

### doc_image

```sql
doc_image(src VARCHAR) → doc_element
doc_image(src VARCHAR, alt VARCHAR) → doc_element
doc_image(src VARCHAR, alt VARCHAR, title VARCHAR) → doc_element
```

Creates a block-level image.

**Attributes set:** `src`, `alt`, `title`

---

### doc_raw

```sql
doc_raw(content VARCHAR) → doc_element
doc_raw(content VARCHAR, format VARCHAR) → doc_element
```

Creates a raw HTML/XML block.

**Parameters:**
- `content` - Raw markup content
- `format` - (Optional) 'html' or 'xml', defaults to 'html'

---

## Inline Builder Functions

### doc_text

```sql
doc_text(content VARCHAR) → doc_element
```

Creates plain text content.

**Returns:** `doc_element` with `kind='inline'`, `element_type='text'`

**Example:**
```sql
SELECT doc_text('Hello world');
-- {kind: 'inline', element_type: 'text', content: 'Hello world', level: 1, ...}
```

---

### Whitespace Functions

```sql
doc_space() → doc_element       -- Word separator
doc_softbreak() → doc_element   -- Soft line break
doc_linebreak() → doc_element   -- Hard line break
```

All return `doc_element` with `kind='inline'` and empty content.

---

### doc_bold

```sql
doc_bold(content VARCHAR) → doc_element
```

Creates bold/strong text.

---

### doc_italic

```sql
doc_italic(content VARCHAR) → doc_element
```

Creates italic/emphasis text.

---

### doc_strikethrough

```sql
doc_strikethrough(content VARCHAR) → doc_element
```

Creates strikethrough text.

---

### doc_superscript

```sql
doc_superscript(content VARCHAR) → doc_element
```

Creates superscript text.

---

### doc_subscript

```sql
doc_subscript(content VARCHAR) → doc_element
```

Creates subscript text.

---

### doc_smallcaps

```sql
doc_smallcaps(content VARCHAR) → doc_element
```

Creates small capitals text.

---

### doc_underline

```sql
doc_underline(content VARCHAR) → doc_element
```

Creates underlined text.

---

### doc_inline_code

```sql
doc_inline_code(content VARCHAR) → doc_element
```

Creates inline code.

**Example:**
```sql
SELECT doc_inline_code('print()');
-- {kind: 'inline', element_type: 'code', content: 'print()', ...}
```

---

### doc_math

```sql
doc_math(content VARCHAR) → doc_element
doc_math(content VARCHAR, block_display BOOLEAN) → doc_element
```

Creates a math expression.

**Parameters:**
- `content` - LaTeX math content
- `block_display` - (Optional) True for block display, false for inline (default)

**Attributes set:** `display` ('inline' or 'block')

**Example:**
```sql
SELECT doc_math('E=mc^2');
SELECT doc_math('\\sum_{i=1}^n x_i', true);
```

---

### doc_link

```sql
doc_link(text VARCHAR, href VARCHAR) → doc_element
doc_link(text VARCHAR, href VARCHAR, title VARCHAR) → doc_element
```

Creates a hyperlink.

**Parameters:**
- `text` - Link text
- `href` - URL
- `title` - (Optional) Link title/tooltip

**Attributes set:** `href`, `title`

**Example:**
```sql
SELECT doc_link('Click here', 'https://example.com');
-- {kind: 'inline', element_type: 'link', content: 'Click here',
--  attributes: {'href': 'https://example.com'}, ...}
```

---

### doc_inline_image

```sql
doc_inline_image(src VARCHAR) → doc_element
doc_inline_image(src VARCHAR, alt VARCHAR) → doc_element
doc_inline_image(src VARCHAR, alt VARCHAR, title VARCHAR) → doc_element
```

Creates an inline image.

**Parameters:**
- `src` - Image URL or path
- `alt` - (Optional) Alt text (also stored in content)
- `title` - (Optional) Image title

**Attributes set:** `src`, `alt`, `title`

---

### doc_quoted

```sql
doc_quoted(content VARCHAR) → doc_element
doc_quoted(content VARCHAR, quote_type VARCHAR) → doc_element
```

Creates quoted text.

**Parameters:**
- `content` - Quoted text
- `quote_type` - (Optional) 'single' or 'double' (default)

**Attributes set:** `quote_type`

---

### doc_cite

```sql
doc_cite(key VARCHAR) → doc_element
doc_cite(key VARCHAR, prefix VARCHAR) → doc_element
doc_cite(key VARCHAR, prefix VARCHAR, suffix VARCHAR) → doc_element
```

Creates a citation reference.

**Parameters:**
- `key` - Citation key
- `prefix` - (Optional) Text before citation
- `suffix` - (Optional) Text after citation

**Attributes set:** `key`, `prefix`, `suffix`

---

### doc_note

```sql
doc_note(content VARCHAR) → doc_element
```

Creates a footnote.

---

### doc_span

```sql
doc_span(content VARCHAR) → doc_element
doc_span(content VARCHAR, id VARCHAR) → doc_element
doc_span(content VARCHAR, id VARCHAR, class VARCHAR) → doc_element
```

Creates a generic inline container.

**Attributes set:** `id`, `class`

---

### doc_raw_inline

```sql
doc_raw_inline(content VARCHAR) → doc_element
doc_raw_inline(content VARCHAR, format VARCHAR) → doc_element
```

Creates raw inline content.

**Parameters:**
- `content` - Raw content
- `format` - (Optional) 'html', 'latex', etc. (default: 'html')

**Attributes set:** `format`

---

## Assembly Functions

### doc_assemble

```sql
doc_assemble(blocks LIST(doc_element)) → LIST(doc_element)
```

Assigns sequential `element_order` values (0, 1, 2, ...) to a list of elements.

**Example:**
```sql
SELECT doc_assemble([
    doc_heading('Title', 1),
    doc_paragraph('Content')
]);
-- First element gets element_order=0, second gets element_order=1
```

---

### doc_document

```sql
doc_document(blocks LIST(doc_element)) → LIST(doc_element)
```

Alias for `doc_assemble`. Use for semantic clarity when creating complete documents.

---

### doc_section

```sql
doc_section(title VARCHAR, level INTEGER) → LIST(doc_element)
doc_section(title VARCHAR, level INTEGER, children LIST(doc_element)) → LIST(doc_element)
```

Creates a section with a heading followed by content blocks.

**Parameters:**
- `title` - Section heading text
- `level` - Heading level (1-6)
- `children` - (Optional) Content blocks for the section

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

```sql
doc_rebase_levels(blocks LIST(doc_element), offset INTEGER) → LIST(doc_element)
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
SELECT doc_rebase_levels([doc_heading('Title', 1)], 1)[1].attributes['heading_level'];
-- Returns: '2'
```

---

### doc_concat

```sql
doc_concat(blocks1 LIST(doc_element), blocks2 LIST(doc_element)) → LIST(doc_element)
```

Concatenates two element lists without modifying `element_order`.

**Notes:**
- Unlike `doc_blocks_merge`, does not adjust `element_order`
- Use with `doc_assemble` to renumber after concatenation

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
) → doc_element
```

Creates a `doc_element` with `kind='block'` and all fields specified.

---

### duck_block (Simple Constructor)

```sql
duck_block(element_type VARCHAR, content VARCHAR) → doc_element
```

Creates a `doc_element` with `kind='block'` using defaults:
- `level`: NULL
- `encoding`: 'text'
- `attributes`: empty map
- `element_order`: 0

---

### to_duck_block

```sql
to_duck_block(input STRUCT) → doc_element
```

Converts a compatible struct to a `doc_element`.

**Notes:**
- Accepts structs with 2-7 fields
- Missing fields are filled with defaults
- Infers `kind` from context if not provided

---

### duck_block_valid

```sql
duck_block_valid(element doc_element) → BOOLEAN
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
duck_block_type(element doc_element) → VARCHAR      -- Get element_type
duck_block_content(element doc_element) → VARCHAR   -- Get content
duck_block_level(element doc_element) → INTEGER     -- Get level
duck_block_encoding(element doc_element) → VARCHAR  -- Get encoding
duck_block_order(element doc_element) → INTEGER     -- Get element_order
duck_block_attr(element doc_element, key VARCHAR) → VARCHAR  -- Get attribute value
```

---

### Field Setters

Return a new element with one field modified (immutable update):

```sql
duck_block_set_order(element doc_element, new_order INTEGER) → doc_element
duck_block_set_content(element doc_element, new_content VARCHAR) → doc_element
duck_block_set_level(element doc_element, new_level INTEGER) → doc_element
```

---

## Manipulation Functions

### doc_blocks_filter

```sql
doc_blocks_filter(blocks LIST(doc_element), types VARCHAR[]) → LIST(doc_element)
```

Filter elements to include only specified types.

**Example:**
```sql
SELECT doc_blocks_filter(blocks, ['heading', 'code']);
```

---

### doc_blocks_exclude

```sql
doc_blocks_exclude(blocks LIST(doc_element), types VARCHAR[]) → LIST(doc_element)
```

Filter elements to exclude specified types.

---

### doc_blocks_merge

```sql
doc_blocks_merge(blocks1 LIST(doc_element), blocks2 LIST(doc_element)) → LIST(doc_element)
```

Combine two element sequences with automatic `element_order` adjustment.

---

### doc_blocks_reorder

```sql
doc_blocks_reorder(blocks LIST(doc_element)) → LIST(doc_element)
```

Renumber `element_order` values sequentially from 0.

---

### doc_blocks_slice

```sql
doc_blocks_slice(blocks LIST(doc_element), start_order INTEGER, end_order INTEGER) → LIST(doc_element)
```

Extract elements within an `element_order` range (inclusive).

---

## Extraction Functions

### doc_blocks_to_text

```sql
doc_blocks_to_text(blocks LIST(doc_element)) → VARCHAR
doc_blocks_to_text(blocks LIST(doc_element), separator VARCHAR) → VARCHAR
```

Extract plain text content from elements.

**Parameters:**
- `separator` - Text between blocks (default: double newline)

---

### doc_blocks_headings

```sql
doc_blocks_headings(blocks LIST(doc_element)) → LIST(STRUCT(level, title, id, element_order))
```

Extract heading information.

---

### doc_blocks_code_blocks

```sql
doc_blocks_code_blocks(blocks LIST(doc_element)) → LIST(STRUCT(language, content, element_order))
```

Extract code block information.

---

### doc_blocks_stats

```sql
doc_blocks_stats(blocks LIST(doc_element)) → LIST(STRUCT(element_type, count, total_length, avg_length))
```

Compute statistics by element type.

---

## Pandoc Conversion Functions

### pandoc_inlines_to_doc_inlines

```sql
pandoc_inlines_to_doc_inlines(json_inlines VARCHAR) → LIST(doc_element)
```

Convert Pandoc inline JSON array to doc_element inlines.

**Example:**
```sql
SELECT pandoc_inlines_to_doc_inlines('[{"t":"Str","c":"Hello"},{"t":"Space"},{"t":"Strong","c":[{"t":"Str","c":"world"}]}]');
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

### doc_inlines_to_pandoc

```sql
doc_inlines_to_pandoc(inlines LIST(doc_element)) → VARCHAR
```

Convert doc_element inlines to Pandoc JSON.

**Example:**
```sql
SELECT doc_inlines_to_pandoc([doc_text('Hello')]);
-- Returns: '[{"t":"Str","c":"Hello"}]'
```

---

### doc_text_to_pandoc

```sql
doc_text_to_pandoc(text VARCHAR) → VARCHAR
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
