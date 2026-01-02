# Inline Builder Functions

Functions for constructing inline `doc_element` elements for rich text formatting within document blocks.

## Overview

While block builders (`doc_heading`, `doc_paragraph`, etc.) create document structure, inline builders create the formatted text within blocks. Inline elements enable:

1. **Structured rich text** - Links, bold, italic, code, images as data
2. **Cross-format compatibility** - Same inline elements render to Markdown, HTML, etc.
3. **Programmatic text generation** - Build formatted text from query results

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Block + Inline Structure                                               │
│                                                                         │
│  doc_paragraph with inline content:                                     │
│  ┌───────────────────────────────────────────────────────────┐         │
│  │ doc_text("Click ") + doc_link("here", url) + doc_text("!") │         │
│  └───────────────────────────────────────────────────────────┘         │
│                            ↓                                            │
│  Markdown:  Click [here](https://example.com)!                         │
│  HTML:      Click <a href="https://example.com">here</a>!              │
└─────────────────────────────────────────────────────────────────────────┘
```

## The Unified doc_element Type

Both block and inline elements use the same unified type, distinguished by the `kind` field:

```sql
STRUCT(
    kind VARCHAR,                       -- 'inline' for inline elements
    element_type VARCHAR,               -- 'text', 'link', 'bold', 'italic', etc.
    content VARCHAR,                    -- Text content or alt text
    level INTEGER,                      -- Nesting depth (1 = top level)
    encoding VARCHAR,                   -- Content encoding (typically 'text')
    attributes MAP(VARCHAR, VARCHAR),   -- Type-specific: href, src, title, etc.
    element_order INTEGER               -- Position in inline sequence
)
```

## Inline Constructors

### doc_text

Create plain text content.

```sql
doc_text(content VARCHAR) → doc_element
```

**Example:**
```sql
SELECT doc_text('Hello world');
-- Returns: {kind: 'inline', element_type: 'text', content: 'Hello world',
--           level: 1, encoding: 'text', attributes: {}, element_order: 0}
```

---

### doc_link

Create a hyperlink.

```sql
doc_link(
    text VARCHAR,
    href VARCHAR,
    title VARCHAR DEFAULT NULL
) → doc_element
```

**Example:**
```sql
SELECT doc_link('Click here', 'https://example.com');
-- Returns: {kind: 'inline', element_type: 'link', content: 'Click here',
--           level: 1, encoding: 'text',
--           attributes: {'href': 'https://example.com'}, element_order: 0}

SELECT doc_link('DuckDB', 'https://duckdb.org', 'The DuckDB website');
-- Returns: {kind: 'inline', element_type: 'link', content: 'DuckDB',
--           level: 1, encoding: 'text',
--           attributes: {'href': 'https://duckdb.org', 'title': 'The DuckDB website'},
--           element_order: 0}
```

---

### doc_inline_image

Create an inline image.

```sql
doc_inline_image(
    src VARCHAR,
    alt VARCHAR DEFAULT NULL,
    title VARCHAR DEFAULT NULL
) → doc_element
```

**Example:**
```sql
SELECT doc_inline_image('/img/logo.png', 'Logo', 'Company Logo');
-- Returns: {kind: 'inline', element_type: 'image', content: 'Logo',
--           level: 1, encoding: 'text',
--           attributes: {'src': '/img/logo.png', 'alt': 'Logo', 'title': 'Company Logo'},
--           element_order: 0}
```

---

### doc_bold

Create bold/strong text.

```sql
doc_bold(content VARCHAR) → doc_element
```

**Example:**
```sql
SELECT doc_bold('Important');
-- Returns: {kind: 'inline', element_type: 'bold', content: 'Important',
--           level: 1, encoding: 'text', attributes: {}, element_order: 0}
```

---

### doc_italic

Create italic/emphasis text.

```sql
doc_italic(content VARCHAR) → doc_element
```

**Example:**
```sql
SELECT doc_italic('emphasized');
-- Returns: {kind: 'inline', element_type: 'italic', content: 'emphasized',
--           level: 1, encoding: 'text', attributes: {}, element_order: 0}
```

---

### doc_inline_code

Create inline code (monospace).

```sql
doc_inline_code(content VARCHAR) → doc_element
```

**Example:**
```sql
SELECT doc_inline_code('print()');
-- Returns: {kind: 'inline', element_type: 'code', content: 'print()',
--           level: 1, encoding: 'text', attributes: {}, element_order: 0}
```

---

### doc_strikethrough

Create strikethrough text.

```sql
doc_strikethrough(content VARCHAR) → doc_element
```

**Example:**
```sql
SELECT doc_strikethrough('deleted');
-- Returns: {kind: 'inline', element_type: 'strikethrough', content: 'deleted',
--           level: 1, encoding: 'text', attributes: {}, element_order: 0}
```

---

### Additional Inline Types

| Function | Element Type | Description |
|----------|--------------|-------------|
| `doc_superscript(content)` | `superscript` | Superscript text |
| `doc_subscript(content)` | `subscript` | Subscript text |
| `doc_smallcaps(content)` | `smallcaps` | Small capitals |
| `doc_underline(content)` | `underline` | Underlined text |
| `doc_math(content, block)` | `math` | Math expression |
| `doc_quoted(content, type)` | `quoted` | Quoted text |
| `doc_cite(key)` | `cite` | Citation reference |
| `doc_note(content)` | `note` | Footnote |
| `doc_span(content, id)` | `span` | Generic container |
| `doc_raw_inline(content, fmt)` | `raw` | Raw format content |

---

## Whitespace Elements

```sql
doc_space() → doc_element      -- Word separator
doc_softbreak() → doc_element  -- Soft line break
doc_linebreak() → doc_element  -- Hard line break
```

---

## Usage Patterns

### Building Rich Text

Combine inline elements into arrays for structured content:

```sql
-- Create a formatted sentence
SELECT [
    doc_text('Click '),
    doc_link('here', 'https://example.com'),
    doc_text(' to learn more about '),
    doc_bold('DuckDB'),
    doc_text('.')
];
```

### Dynamic Link Generation

Generate links from query data:

```sql
-- Create links from a table
SELECT doc_link(
    project_name,
    'https://github.com/' || owner || '/' || repo
) AS project_link
FROM projects;
```

### Badge Generation

Create status badges with images:

```sql
-- CI badge with link
SELECT [
    doc_link(
        doc_inline_image(
            'https://github.com/' || repo || '/actions/workflows/ci.yml/badge.svg',
            'CI Status'
        ).content,
        'https://github.com/' || repo || '/actions'
    )
] FROM repositories;
```

---

## Integration with Renderers

The unified `doc_element` type is designed for cross-format compatibility:

### Markdown Rendering (duckdb_markdown)

When inline elements are rendered to Markdown:

| Element Type | Markdown Output |
|--------------|-----------------|
| `text` | Content as-is |
| `link` | `[content](href "title")` |
| `image` | `![alt](src "title")` |
| `bold` | `**content**` |
| `italic` | `*content*` |
| `code` | `` `content` `` |
| `strikethrough` | `~~content~~` |

### HTML Rendering (webbed)

When inline elements are rendered to HTML:

| Element Type | HTML Output |
|--------------|-------------|
| `text` | Content (escaped) |
| `link` | `<a href="...">content</a>` |
| `image` | `<img src="..." alt="...">` |
| `bold` | `<strong>content</strong>` |
| `italic` | `<em>content</em>` |
| `code` | `<code>content</code>` |
| `strikethrough` | `<del>content</del>` |

---

## See Also

- [Block Builder Functions](block_builders.md) - Block-level construction
- [API Reference](api.md) - Complete function reference
- [Design Document](design.md) - Architecture overview
