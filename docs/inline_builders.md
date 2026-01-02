# Inline Builder Functions

Functions for constructing inline `duck_block` elements for rich text formatting within document blocks.

## Overview

While block builders (`db_heading`, `db_paragraph`, etc.) create document structure, inline builders create the formatted text within blocks. Inline elements enable:

1. **Structured rich text** - Links, bold, italic, code, images as data
2. **Cross-format compatibility** - Same inline elements render to Markdown, HTML, etc.
3. **Programmatic text generation** - Build formatted text from query results

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Block + Inline Structure                                               │
│                                                                         │
│  db_paragraph with inline content:                                     │
│  ┌───────────────────────────────────────────────────────────┐         │
│  │ db_text("Click ") + db_link("here", url) + db_text("!") │         │
│  └───────────────────────────────────────────────────────────┘         │
│                            ↓                                            │
│  Markdown:  Click [here](https://example.com)!                         │
│  HTML:      Click <a href="https://example.com">here</a>!              │
└─────────────────────────────────────────────────────────────────────────┘
```

## The Unified duck_block Type

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

### db_text

Create plain text content.

```sql
db_text(content VARCHAR) → duck_block
```

**Example:**
```sql
SELECT db_text('Hello world');
-- Returns: {kind: 'inline', element_type: 'text', content: 'Hello world',
--           level: 1, encoding: 'text', attributes: {}, element_order: 0}
```

---

### db_link

Create a hyperlink.

```sql
db_link(
    text VARCHAR,
    href VARCHAR,
    title VARCHAR DEFAULT NULL
) → duck_block
```

**Example:**
```sql
SELECT db_link('Click here', 'https://example.com');
-- Returns: {kind: 'inline', element_type: 'link', content: 'Click here',
--           level: 1, encoding: 'text',
--           attributes: {'href': 'https://example.com'}, element_order: 0}

SELECT db_link('DuckDB', 'https://duckdb.org', 'The DuckDB website');
-- Returns: {kind: 'inline', element_type: 'link', content: 'DuckDB',
--           level: 1, encoding: 'text',
--           attributes: {'href': 'https://duckdb.org', 'title': 'The DuckDB website'},
--           element_order: 0}
```

---

### db_inline_image

Create an inline image.

```sql
db_inline_image(
    src VARCHAR,
    alt VARCHAR DEFAULT NULL,
    title VARCHAR DEFAULT NULL
) → duck_block
```

**Example:**
```sql
SELECT db_inline_image('/img/logo.png', 'Logo', 'Company Logo');
-- Returns: {kind: 'inline', element_type: 'image', content: 'Logo',
--           level: 1, encoding: 'text',
--           attributes: {'src': '/img/logo.png', 'alt': 'Logo', 'title': 'Company Logo'},
--           element_order: 0}
```

---

### db_bold

Create bold/strong text.

```sql
db_bold(content VARCHAR) → duck_block
```

**Example:**
```sql
SELECT db_bold('Important');
-- Returns: {kind: 'inline', element_type: 'bold', content: 'Important',
--           level: 1, encoding: 'text', attributes: {}, element_order: 0}
```

---

### db_italic

Create italic/emphasis text.

```sql
db_italic(content VARCHAR) → duck_block
```

**Example:**
```sql
SELECT db_italic('emphasized');
-- Returns: {kind: 'inline', element_type: 'italic', content: 'emphasized',
--           level: 1, encoding: 'text', attributes: {}, element_order: 0}
```

---

### db_inline_code

Create inline code (monospace).

```sql
db_inline_code(content VARCHAR) → duck_block
```

**Example:**
```sql
SELECT db_inline_code('print()');
-- Returns: {kind: 'inline', element_type: 'code', content: 'print()',
--           level: 1, encoding: 'text', attributes: {}, element_order: 0}
```

---

### db_strikethrough

Create strikethrough text.

```sql
db_strikethrough(content VARCHAR) → duck_block
```

**Example:**
```sql
SELECT db_strikethrough('deleted');
-- Returns: {kind: 'inline', element_type: 'strikethrough', content: 'deleted',
--           level: 1, encoding: 'text', attributes: {}, element_order: 0}
```

---

### Additional Inline Types

| Function | Element Type | Description |
|----------|--------------|-------------|
| `db_superscript(content)` | `superscript` | Superscript text |
| `db_subscript(content)` | `subscript` | Subscript text |
| `db_smallcaps(content)` | `smallcaps` | Small capitals |
| `db_underline(content)` | `underline` | Underlined text |
| `db_math(content, block)` | `math` | Math expression |
| `db_quoted(content, type)` | `quoted` | Quoted text |
| `db_cite(key)` | `cite` | Citation reference |
| `db_note(content)` | `note` | Footnote |
| `db_span(content, id)` | `span` | Generic container |
| `db_raw_inline(content, fmt)` | `raw` | Raw format content |

---

## Whitespace Elements

```sql
db_space() → duck_block      -- Word separator
db_softbreak() → duck_block  -- Soft line break
db_linebreak() → duck_block  -- Hard line break
```

---

## Usage Patterns

### Building Rich Text

Combine inline elements into arrays for structured content:

```sql
-- Create a formatted sentence
SELECT [
    db_text('Click '),
    db_link('here', 'https://example.com'),
    db_text(' to learn more about '),
    db_bold('DuckDB'),
    db_text('.')
];
```

### Dynamic Link Generation

Generate links from query data:

```sql
-- Create links from a table
SELECT db_link(
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
    db_link(
        db_inline_image(
            'https://github.com/' || repo || '/actions/workflows/ci.yml/badge.svg',
            'CI Status'
        ).content,
        'https://github.com/' || repo || '/actions'
    )
] FROM repositories;
```

---

## Integration with Renderers

The unified `duck_block` type is designed for cross-format compatibility:

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
