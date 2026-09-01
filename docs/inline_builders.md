# Inline Builder Functions

Functions for constructing inline `duck_block` elements for rich text formatting within document blocks.

## Overview

While block builders (`duck_block_heading`, `duck_block_paragraph`, etc.) create document structure, inline builders create the formatted text within blocks. Inline elements enable:

1. **Structured rich text** - Links, bold, italic, code, images as data
2. **Cross-format compatibility** - Same inline elements render to Markdown, HTML, etc.
3. **Programmatic text generation** - Build formatted text from query results

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Block + Inline Composition                                              │
│                                                                          │
│  duck_block_paragraph([                                                         │
│      duck_block_text('Click '),                                                 │
│      duck_block_link('https://example.com', 'here'),                            │
│      duck_block_text('!')                                                       │
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

### duck_block_text

```sql
duck_block_text(content VARCHAR) → LIST(duck_block)
```

Creates plain text content.

**Example:**
```sql
SELECT duck_block_text('Hello world');
-- Returns: [{kind: 'inline', element_type: 'text', content: 'Hello world', level: 1}]
```

### Whitespace Functions

```sql
duck_block_space() → LIST(duck_block)       -- Word separator
duck_block_softbreak() → LIST(duck_block)   -- Soft line break
duck_block_linebreak() → LIST(duck_block)   -- Hard line break
```

---

## Formatting Builders

All formatting builders accept `duck_block_content` (VARCHAR, single element, or list):

### duck_block_bold

```sql
duck_block_bold(content duck_block_content) → LIST(duck_block)
```

**Example:**
```sql
-- Simple
SELECT duck_block_bold('Important');

-- With nested formatting
SELECT duck_block_bold([duck_block_text('very '), duck_block_italic('important')]);
```

### duck_block_italic

```sql
duck_block_italic(content duck_block_content) → LIST(duck_block)
```

### duck_block_strikethrough

```sql
duck_block_strikethrough(content duck_block_content) → LIST(duck_block)
```

### duck_block_superscript

```sql
duck_block_superscript(content duck_block_content) → LIST(duck_block)
```

### duck_block_subscript

```sql
duck_block_subscript(content duck_block_content) → LIST(duck_block)
```

### duck_block_smallcaps

```sql
duck_block_smallcaps(content duck_block_content) → LIST(duck_block)
```

### duck_block_underline

```sql
duck_block_underline(content duck_block_content) → LIST(duck_block)
```

---

## Semantic Elements

### duck_block_inline_code

```sql
duck_block_inline_code(content VARCHAR) → LIST(duck_block)
```

Creates inline code (monospace). Does not accept children.

**Example:**
```sql
SELECT duck_block_inline_code('print()');
```

### duck_block_math

```sql
duck_block_math(content VARCHAR) → LIST(duck_block)
duck_block_math(display BOOLEAN, content VARCHAR) → LIST(duck_block)
```

Creates a math expression.

**Parameters:**
- `display` - (Optional, first when specified) True for block display, false for inline
- `content` - LaTeX math content

**Example:**
```sql
SELECT duck_block_math('E=mc^2');
SELECT duck_block_math(true, '\\sum_{i=1}^n x_i');
```

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
-- Simple link
SELECT duck_block_link('https://example.com', 'Click here');

-- Link with formatted text
SELECT duck_block_link('https://example.com', [
    duck_block_bold('Click'),
    duck_block_text(' here')
]);

-- Link with title
SELECT duck_block_link('https://duckdb.org', 'DuckDB Website', 'DuckDB');
```

### duck_block_inline_image

```sql
duck_block_inline_image(src VARCHAR) → LIST(duck_block)
duck_block_inline_image(src VARCHAR, alt VARCHAR) → LIST(duck_block)
duck_block_inline_image(src VARCHAR, alt VARCHAR, title VARCHAR) → LIST(duck_block)
```

Creates an inline image.

**Example:**
```sql
SELECT duck_block_inline_image('/img/logo.png', 'Logo', 'Company Logo');
```

### duck_block_quoted

```sql
duck_block_quoted(content duck_block_content) → LIST(duck_block)
duck_block_quoted(quote_type VARCHAR, content duck_block_content) → LIST(duck_block)
```

Creates quoted text.

**Parameters:**
- `quote_type` - (Optional, first when specified) 'single' or 'double'
- `content` - Quoted content

### duck_block_cite

```sql
duck_block_cite(key VARCHAR) → LIST(duck_block)
```

Creates a citation reference.

### duck_block_note

```sql
duck_block_note(content duck_block_content) → LIST(duck_block)
```

Creates a footnote.

### duck_block_span

```sql
duck_block_span(content duck_block_content) → LIST(duck_block)
duck_block_span(id VARCHAR, content duck_block_content) → LIST(duck_block)
duck_block_span(id VARCHAR, class VARCHAR, content duck_block_content) → LIST(duck_block)
```

Creates a generic inline container with optional id and class.

### duck_block_raw_inline

```sql
duck_block_raw_inline(content VARCHAR) → LIST(duck_block)
duck_block_raw_inline(format VARCHAR, content VARCHAR) → LIST(duck_block)
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
SELECT duck_block_paragraph([
    duck_block_text('Click '),
    duck_block_link('https://example.com', 'here'),
    duck_block_text(' to learn more about '),
    duck_block_bold('DuckDB'),
    duck_block_text('.')
]);
```

### Dynamic Link Generation

Generate links from query data:

```sql
SELECT duck_block_paragraph([
    duck_block_link(
        'https://github.com/' || owner || '/' || repo,
        project_name
    )
])
FROM projects;
```

### Badge Generation

Create status badges with images inside links:

```sql
SELECT duck_block_paragraph([
    duck_block_link(github_actions_url, [
        duck_block_inline_image(ci_badge_url, 'CI Status')
    ])
])
FROM repositories;
```

### Complex Inline Nesting

```sql
-- "This is **very _important_** text"
SELECT duck_block_paragraph([
    duck_block_text('This is '),
    duck_block_bold([
        duck_block_text('very '),
        duck_block_italic('important')
    ]),
    duck_block_text(' text')
]);
```

---

## Integration with Block Builders

Inline elements are children of block elements:

```sql
SELECT duck_blocks_assemble([
    duck_block_heading(1, [duck_block_text('Welcome to '), duck_block_bold('DuckDB')]),
    duck_block_paragraph([
        duck_block_text('Visit '),
        duck_block_link('https://duckdb.org', 'the website'),
        duck_block_text(' for more info.')
    ]),
    duck_block_list_item([
        duck_block_link('https://github.com/duckdb', 'GitHub'),
        duck_block_text(' '),
        duck_block_inline_image('https://img.shields.io/badge.svg', 'Stars')
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
