# Inline Builder Functions

Functions for constructing inline `duck_block` elements for rich text formatting within document blocks.

## Overview

While block builders (`db_heading`, `db_paragraph`, etc.) create document structure, inline builders create the formatted text within blocks. Inline elements enable:

1. **Structured rich text** - Links, bold, italic, code, images as data
2. **Cross-format compatibility** - Same inline elements render to Markdown, HTML, etc.
3. **Programmatic text generation** - Build formatted text from query results

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Block + Inline Composition                                              │
│                                                                          │
│  db_paragraph([                                                         │
│      db_text('Click '),                                                 │
│      db_link('https://example.com', 'here'),                            │
│      db_text('!')                                                       │
│  ])                                                                      │
│                                                                          │
│  Returns: [{paragraph, content=NULL}, {text}, {link}, {text}]           │
│                                                                          │
│  Markdown:  Click [here](https://example.com)!                          │
│  HTML:      Click <a href="https://example.com">here</a>!               │
└─────────────────────────────────────────────────────────────────────────┘
```

## Universal Pattern

**All inline builders return `LIST(duck_block)`** and follow the same rules as block builders:

- **Config params first, content last**
- **VARCHAR content** → Element's `content` field is set
- **Children** → Element's `content` is NULL, children at `level+1`

## Text and Whitespace

### db_text

```sql
db_text(content VARCHAR) → LIST(duck_block)
```

Creates plain text content.

**Example:**
```sql
SELECT db_text('Hello world');
-- Returns: [{kind: 'inline', element_type: 'text', content: 'Hello world', level: 1}]
```

### Whitespace Functions

```sql
db_space() → LIST(duck_block)       -- Word separator
db_softbreak() → LIST(duck_block)   -- Soft line break
db_linebreak() → LIST(duck_block)   -- Hard line break
```

---

## Formatting Builders

All formatting builders accept `duck_block_content` (VARCHAR, single element, or list):

### db_bold

```sql
db_bold(content duck_block_content) → LIST(duck_block)
```

**Example:**
```sql
-- Simple
SELECT db_bold('Important');

-- With nested formatting
SELECT db_bold([db_text('very '), db_italic('important')]);
```

### db_italic

```sql
db_italic(content duck_block_content) → LIST(duck_block)
```

### db_strikethrough

```sql
db_strikethrough(content duck_block_content) → LIST(duck_block)
```

### db_superscript

```sql
db_superscript(content duck_block_content) → LIST(duck_block)
```

### db_subscript

```sql
db_subscript(content duck_block_content) → LIST(duck_block)
```

### db_smallcaps

```sql
db_smallcaps(content duck_block_content) → LIST(duck_block)
```

### db_underline

```sql
db_underline(content duck_block_content) → LIST(duck_block)
```

---

## Semantic Elements

### db_inline_code

```sql
db_inline_code(content VARCHAR) → LIST(duck_block)
```

Creates inline code (monospace). Does not accept children.

**Example:**
```sql
SELECT db_inline_code('print()');
```

### db_math

```sql
db_math(content VARCHAR) → LIST(duck_block)
db_math(display BOOLEAN, content VARCHAR) → LIST(duck_block)
```

Creates a math expression.

**Parameters:**
- `display` - (Optional, first when specified) True for block display, false for inline
- `content` - LaTeX math content

**Example:**
```sql
SELECT db_math('E=mc^2');
SELECT db_math(true, '\\sum_{i=1}^n x_i');
```

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
-- Simple link
SELECT db_link('https://example.com', 'Click here');

-- Link with formatted text
SELECT db_link('https://example.com', [
    db_bold('Click'),
    db_text(' here')
]);

-- Link with title
SELECT db_link('https://duckdb.org', 'DuckDB Website', 'DuckDB');
```

### db_inline_image

```sql
db_inline_image(src VARCHAR) → LIST(duck_block)
db_inline_image(src VARCHAR, alt VARCHAR) → LIST(duck_block)
db_inline_image(src VARCHAR, alt VARCHAR, title VARCHAR) → LIST(duck_block)
```

Creates an inline image.

**Example:**
```sql
SELECT db_inline_image('/img/logo.png', 'Logo', 'Company Logo');
```

### db_quoted

```sql
db_quoted(content duck_block_content) → LIST(duck_block)
db_quoted(quote_type VARCHAR, content duck_block_content) → LIST(duck_block)
```

Creates quoted text.

**Parameters:**
- `quote_type` - (Optional, first when specified) 'single' or 'double'
- `content` - Quoted content

### db_cite

```sql
db_cite(key VARCHAR) → LIST(duck_block)
```

Creates a citation reference.

### db_note

```sql
db_note(content duck_block_content) → LIST(duck_block)
```

Creates a footnote.

### db_span

```sql
db_span(content duck_block_content) → LIST(duck_block)
db_span(id VARCHAR, content duck_block_content) → LIST(duck_block)
db_span(id VARCHAR, class VARCHAR, content duck_block_content) → LIST(duck_block)
```

Creates a generic inline container with optional id and class.

### db_raw_inline

```sql
db_raw_inline(content VARCHAR) → LIST(duck_block)
db_raw_inline(format VARCHAR, content VARCHAR) → LIST(duck_block)
```

Creates raw inline content.

**Parameters:**
- `format` - (Optional, first when specified) 'html', 'latex', etc.
- `content` - Raw content

---

## Usage Patterns

### Building Rich Text

Compose inline elements into arrays for structured content:

```sql
SELECT db_paragraph([
    db_text('Click '),
    db_link('https://example.com', 'here'),
    db_text(' to learn more about '),
    db_bold('DuckDB'),
    db_text('.')
]);
```

### Dynamic Link Generation

Generate links from query data:

```sql
SELECT db_paragraph([
    db_link(
        'https://github.com/' || owner || '/' || repo,
        project_name
    )
])
FROM projects;
```

### Badge Generation

Create status badges with images inside links:

```sql
SELECT db_paragraph([
    db_link(github_actions_url, [
        db_inline_image(ci_badge_url, 'CI Status')
    ])
])
FROM repositories;
```

### Complex Inline Nesting

```sql
-- "This is **very _important_** text"
SELECT db_paragraph([
    db_text('This is '),
    db_bold([
        db_text('very '),
        db_italic('important')
    ]),
    db_text(' text')
]);
```

---

## Integration with Block Builders

Inline elements are children of block elements:

```sql
SELECT db_assemble([
    db_heading(1, [db_text('Welcome to '), db_bold('DuckDB')]),
    db_paragraph([
        db_text('Visit '),
        db_link('https://duckdb.org', 'the website'),
        db_text(' for more info.')
    ]),
    db_list_item([
        db_link('https://github.com/duckdb', 'GitHub'),
        db_text(' '),
        db_inline_image('https://img.shields.io/badge.svg', 'Stars')
    ])
]);
```

---

## Rendering Output

When inline elements are rendered:

### Markdown

| Element Type | Output |
|--------------|--------|
| `text` | Content as-is |
| `link` | `[content](href "title")` |
| `image` | `![alt](src "title")` |
| `bold` | `**content**` |
| `italic` | `*content*` |
| `code` | `` `content` `` |
| `strikethrough` | `~~content~~` |

### HTML

| Element Type | Output |
|--------------|--------|
| `text` | Content (escaped) |
| `link` | `<a href="...">content</a>` |
| `image` | `<img src="..." alt="...">` |
| `bold` | `<strong>content</strong>` |
| `italic` | `<em>content</em>` |
| `code` | `<code>content</code>` |
| `strikethrough` | `<del>content</del>` |

---

## See Also

- [Block Builders](block_builders.md) - Block-level construction
- [API Reference](api.md) - Complete function reference
- [V2 API Design](v2_api_design.md) - Design rationale
