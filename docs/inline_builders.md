# Inline Builder Functions

Functions for constructing `doc_inline` elements for rich text formatting within document blocks.

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

## The doc_inline Type

```sql
STRUCT(
    inline_type VARCHAR,                -- 'text', 'link', 'bold', 'italic', etc.
    content VARCHAR,                    -- Text content or alt text
    attributes MAP(VARCHAR, VARCHAR)    -- Type-specific: href, src, title, etc.
)
```

## Inline Constructors

### doc_text

Create plain text content.

```sql
doc_text(content VARCHAR) → doc_inline
```

**Example:**
```sql
SELECT doc_text('Hello world');
-- Returns: {inline_type: 'text', content: 'Hello world', attributes: {}}
```

---

### doc_link

Create a hyperlink.

```sql
doc_link(
    text VARCHAR,
    href VARCHAR,
    title VARCHAR DEFAULT NULL
) → doc_inline
```

**Example:**
```sql
SELECT doc_link('Click here', 'https://example.com');
-- Returns: {inline_type: 'link', content: 'Click here',
--           attributes: {'href': 'https://example.com'}}

SELECT doc_link('DuckDB', 'https://duckdb.org', 'The DuckDB website');
-- Returns: {inline_type: 'link', content: 'DuckDB',
--           attributes: {'href': 'https://duckdb.org', 'title': 'The DuckDB website'}}
```

---

### doc_inline_image

Create an inline image.

```sql
doc_inline_image(
    src VARCHAR,
    alt VARCHAR DEFAULT NULL,
    title VARCHAR DEFAULT NULL
) → doc_inline
```

**Example:**
```sql
SELECT doc_inline_image('/img/logo.png', 'Logo', 'Company Logo');
-- Returns: {inline_type: 'image', content: 'Logo',
--           attributes: {'src': '/img/logo.png', 'alt': 'Logo', 'title': 'Company Logo'}}
```

---

### doc_bold

Create bold/strong text.

```sql
doc_bold(content VARCHAR) → doc_inline
```

**Example:**
```sql
SELECT doc_bold('Important');
-- Returns: {inline_type: 'bold', content: 'Important', attributes: {}}
```

---

### doc_italic

Create italic/emphasis text.

```sql
doc_italic(content VARCHAR) → doc_inline
```

**Example:**
```sql
SELECT doc_italic('emphasized');
-- Returns: {inline_type: 'italic', content: 'emphasized', attributes: {}}
```

---

### doc_inline_code

Create inline code (monospace).

```sql
doc_inline_code(content VARCHAR) → doc_inline
```

**Example:**
```sql
SELECT doc_inline_code('print()');
-- Returns: {inline_type: 'code', content: 'print()', attributes: {}}
```

---

### doc_strikethrough

Create strikethrough text.

```sql
doc_strikethrough(content VARCHAR) → doc_inline
```

**Example:**
```sql
SELECT doc_strikethrough('deleted');
-- Returns: {inline_type: 'strikethrough', content: 'deleted', attributes: {}}
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

The `doc_inline` type is designed for cross-format compatibility:

### Markdown Rendering (duckdb_markdown)

When inline elements are rendered to Markdown:

| Inline Type | Markdown Output |
|-------------|-----------------|
| `text` | Content as-is |
| `link` | `[content](href "title")` |
| `image` | `![alt](src "title")` |
| `bold` | `**content**` |
| `italic` | `*content*` |
| `code` | `` `content` `` |
| `strikethrough` | `~~content~~` |

### HTML Rendering (webbed)

When inline elements are rendered to HTML:

| Inline Type | HTML Output |
|-------------|-------------|
| `text` | Content (escaped) |
| `link` | `<a href="...">content</a>` |
| `image` | `<img src="..." alt="...">` |
| `bold` | `<strong>content</strong>` |
| `italic` | `<em>content</em>` |
| `code` | `<code>content</code>` |
| `strikethrough` | `<del>content</del>` |

---

## Future Extensions

Planned inline element types:

- `subscript` / `superscript` - Sub/superscript text
- `mark` - Highlighted text
- `footnote_ref` - Footnote reference
- `citation` - Citation reference
- `math` - Inline math expression

---

## See Also

- [Block Builder Functions](block_builders.md) - Block-level construction
- [API Reference](api.md) - Complete function reference
- [Design Document](design.md) - Architecture overview
