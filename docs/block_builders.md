# Block Builder Functions

Declarative functions for constructing duck_block structures. These enable:

1. **Programmatic document generation** from query results
2. **Nested/hierarchical authoring** that flattens automatically
3. **Template-based document construction**

## Design Philosophy

**All builders return `LIST(duck_block)`** with a consistent pattern:

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Universal Builder Pattern                                               │
│                                                                          │
│  Config params first, content last                                       │
│  Content: VARCHAR | duck_block | LIST(duck_block)                        │
│                                                                          │
│  ┌──────────────────┐     ┌─────────────────────────────────────────┐   │
│  │ VARCHAR content  │ →   │ [parent.content = text]                 │   │
│  └──────────────────┘     └─────────────────────────────────────────┘   │
│                                                                          │
│  ┌──────────────────┐     ┌─────────────────────────────────────────┐   │
│  │ Children         │ →   │ [parent.content = NULL], [children@lvl+1]│  │
│  └──────────────────┘     └─────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

## Block Constructors

### duck_block_heading

```sql
duck_block_heading(level INTEGER, content duck_block_content) → LIST(duck_block)
```

**Parameters:**
- `level` - Heading level (1-6), stored in `attributes['heading_level']`
- `content` - Text or inline children

**Example:**
```sql
-- Simple heading
SELECT duck_block_heading(1, 'Introduction');

-- Heading with inline formatting
SELECT duck_block_heading(2, [duck_block_text('Hello '), duck_block_bold('World')]);

-- Access heading level
SELECT duck_block_heading(2, 'Title')[1].attributes['heading_level'];
-- Returns: '2'
```

### duck_block_paragraph

```sql
duck_block_paragraph(content VARCHAR) → LIST(duck_block)
duck_block_paragraph(content VARCHAR[]) → LIST(duck_block)
duck_block_paragraph(children LIST(duck_block)) → LIST(duck_block)
duck_block_paragraph(children LIST(LIST(duck_block))) → LIST(duck_block)
```

**Example:**
```sql
-- Simple paragraph
SELECT duck_block_paragraph('This is body text.');

-- Paragraph from string array (each becomes duck_block_text)
SELECT duck_block_paragraph(['Hello ', 'world', '!']);

-- Paragraph with rich content
SELECT duck_block_paragraph([
    duck_block_text('Click '),
    duck_block_link('https://example.com', 'here'),
    duck_block_text(' to learn more.')
]);
```

### duck_block_code

```sql
duck_block_code(content duck_block_content) → LIST(duck_block)
duck_block_code(language VARCHAR, content duck_block_content) → LIST(duck_block)
```

**Parameters:**
- `language` - (Optional, first when specified) Programming language
- `content` - Code content

**Example:**
```sql
SELECT duck_block_code('print("hello")');
SELECT duck_block_code('python', 'def hello():\n    print("hi")');
```

### duck_block_blockquote

```sql
duck_block_blockquote(content duck_block_content) → LIST(duck_block)
duck_block_blockquote(level INTEGER, content duck_block_content) → LIST(duck_block)
```

**Parameters:**
- `level` - (Optional, first when specified) Nesting level, defaults to 1
- `content` - Quoted content

### duck_block_list_block

```sql
-- Simple string items (stored as JSON array in content)
duck_block_list_block(items VARCHAR[]) → LIST(duck_block)
duck_block_list_block(ordered BOOLEAN, items VARCHAR[]) → LIST(duck_block)

-- Rich list items with inline content
duck_block_list_block(items LIST(LIST(duck_block))) → LIST(duck_block)
duck_block_list_block(ordered BOOLEAN, items LIST(LIST(duck_block))) → LIST(duck_block)
```

Creates a list, either with simple string items or rich list items.

**Parameters:**
- `ordered` - (Optional, first when specified) True for numbered list
- `items` - Array of strings OR nested list of duck_blocks (from `duck_block_list_item`)

**Example:**
```sql
-- Simple string list
SELECT duck_block_list_block(['First', 'Second', 'Third']);
SELECT duck_block_list_block(true, ['Step 1', 'Step 2', 'Step 3']);

-- Rich list with inline content (using duck_block_list_item)
SELECT duck_block_list_block([
    duck_block_list_item([duck_block_link('https://github.com', 'GitHub'), duck_block_text(' - code hosting')]),
    duck_block_list_item([duck_block_bold('DuckDB'), duck_block_text(' - analytical database')])
]);
```

### duck_block_list_item

```sql
duck_block_list_item(content duck_block_content) → LIST(duck_block)
duck_block_list_item(ordered BOOLEAN, content duck_block_content) → LIST(duck_block)
```

Creates a single list item with rich content.

**Parameters:**
- `ordered` - (Optional, first when specified) Bullet style
- `content` - Text or inline children

**Example:**
```sql
-- Simple item
SELECT duck_block_list_item('Item text');

-- Item with inline content (links, badges, etc.)
SELECT duck_block_list_item([
    duck_block_link('https://github.com/org/repo', 'GitHub'),
    duck_block_text(' '),
    duck_block_inline_image('https://badge.svg', 'CI Status')
]);
```

### duck_block_hr

```sql
duck_block_hr() → LIST(duck_block)
```

### duck_block_metadata

```sql
duck_block_metadata(yaml_content VARCHAR) → LIST(duck_block)
```

**Example:**
```sql
SELECT duck_block_metadata('title: My Document
author: Jane Doe
date: 2024-01-01');
```

### duck_block_image

```sql
duck_block_image(src VARCHAR) → LIST(duck_block)
duck_block_image(src VARCHAR, alt VARCHAR) → LIST(duck_block)
duck_block_image(src VARCHAR, alt VARCHAR, title VARCHAR) → LIST(duck_block)
```

### duck_block_raw

```sql
duck_block_raw(content VARCHAR) → LIST(duck_block)
duck_block_raw(format VARCHAR, content VARCHAR) → LIST(duck_block)
```

**Parameters:**
- `format` - (Optional, first when specified) 'html', 'xml', 'latex'
- `content` - Raw markup

### duck_block_div

```sql
duck_block_div(children LIST(duck_block)) → LIST(duck_block)
duck_block_div(id VARCHAR, children LIST(duck_block)) → LIST(duck_block)
duck_block_div(id VARCHAR, class VARCHAR, children LIST(duck_block)) → LIST(duck_block)
duck_block_div(children LIST(LIST(duck_block))) → LIST(duck_block)
duck_block_div(id VARCHAR, children LIST(LIST(duck_block))) → LIST(duck_block)
duck_block_div(id VARCHAR, class VARCHAR, children LIST(LIST(duck_block))) → LIST(duck_block)
```

Creates a generic block-level container (like HTML `<div>`). Useful for grouping content with optional id/class attributes.

**Parameters:**
- `id` - (Optional) Element ID for targeting
- `class` - (Optional) CSS class name
- `children` - Child blocks

**Example:**
```sql
-- Simple container
SELECT duck_block_div([duck_block_paragraph('Content 1'), duck_block_paragraph('Content 2')]);

-- Container with id
SELECT duck_block_div('intro-section', [duck_block_heading(2, 'Introduction'), duck_block_paragraph('...')]);

-- Container with id and class
SELECT duck_block_div('sidebar', 'widget', [duck_block_heading(3, 'Related'), duck_block_list_block(['A', 'B'])]);

-- Nested composition
SELECT duck_block_div('main', [
    duck_block_heading(1, 'Title'),
    duck_block_div('content', [duck_block_paragraph('Body text')])
]);
```

---

## Assembly Functions

### duck_blocks_assemble

Flattens nested block lists and assigns sequential `element_order`.

```sql
duck_blocks_assemble(blocks LIST(LIST(duck_block))) → LIST(duck_block)
```

**Example:**
```sql
SELECT duck_blocks_assemble([
    duck_block_heading(1, 'Title'),
    duck_block_paragraph('Intro text'),
    duck_block_paragraph([duck_block_text('Rich '), duck_block_bold('content')]),
    duck_block_hr()
]);
-- Returns flat list with element_order 0, 1, 2, 3, 4, 5
```

### duck_blocks_document

Alias for `duck_blocks_assemble` for semantic clarity.

```sql
duck_blocks_document(blocks LIST(LIST(duck_block))) → LIST(duck_block)
```

### duck_block_section

Creates a section with heading and optional children.

```sql
duck_block_section(level INTEGER, title duck_block_content) → LIST(duck_block)
duck_block_section(level INTEGER, title duck_block_content, children LIST(LIST(duck_block))) → LIST(duck_block)
```

**Example:**
```sql
SELECT duck_block_section(2, 'Getting Started', [
    duck_block_paragraph('Welcome to the guide.'),
    duck_block_code('bash', 'npm install mypackage'),
    duck_block_paragraph('Now you are ready.')
]);
-- Returns: [heading, paragraph, code, paragraph]
```

### duck_blocks_rebase_levels

Adjust heading levels by offset.

```sql
duck_blocks_rebase_levels(blocks LIST(duck_block), offset INTEGER) → LIST(duck_block)
```

**Example:**
```sql
-- Include subdocument with adjusted levels
SELECT duck_blocks_assemble([
    duck_block_heading(1, 'Main Document'),
    duck_blocks_rebase_levels(subdoc_blocks, 1)  -- h1→h2, h2→h3, etc.
]);
```

### duck_blocks_concat

Concatenate lists without renumbering.

```sql
duck_blocks_concat(blocks1 LIST(duck_block), blocks2 LIST(duck_block)) → LIST(duck_block)
```

---

## Complete Examples

### Report Generation

```sql
SELECT duck_blocks_assemble([
    duck_block_metadata('title: Q4 Sales Report'),
    duck_block_heading(1, 'Q4 2024 Sales Report'),
    duck_block_paragraph([
        duck_block_text('Generated on '),
        duck_block_bold(current_date::VARCHAR)
    ]),
    duck_block_section(2, 'Executive Summary', [
        duck_block_paragraph('Total revenue: $1.2M')
    ]),
    duck_block_section(2, 'Top Products', [
        duck_block_list_block(['Product A', 'Product B', 'Product C'])
    ]),
    duck_block_hr()
]);
```

### Dashboard with Rich Links

```sql
SELECT duck_blocks_assemble([
    duck_block_heading(1, 'Project Dashboard'),
    duck_block_heading(2, project_name),
    duck_block_paragraph(COALESCE(description, duck_block_italic('No description'))),
    duck_block_list_item([
        duck_block_link(github_url, 'GitHub'),
        duck_block_text(' '),
        duck_block_inline_image(ci_badge_url, 'CI')
    ]),
    duck_block_list_item([
        duck_block_link(docs_url, 'Documentation'),
        duck_block_text(' '),
        duck_block_inline_image(docs_badge_url, 'Docs')
    ])
])
FROM projects;
```

---

## Flattening Rules

When assembling blocks:

1. **Each builder returns LIST(duck_block)**
2. **VARCHAR content** → Parent's `content` field is set
3. **Children (duck_block or LIST)** → Parent's `content` is NULL, children at `level+1`
4. **duck_blocks_assemble** flattens LIST(LIST(duck_block)) → LIST(duck_block)
5. **element_order** assigned sequentially (0, 1, 2, ...)

---

## Type Casting

### VARCHAR to duck_block

Explicit cast from VARCHAR to duck_block creates a text inline element:

```sql
-- Explicit cast
SELECT 'hello'::duck_block;
-- Equivalent to: duck_block_text('hello')

-- Creates: {kind: 'inline', element_type: 'text', content: 'hello', level: 1, ...}
```

This is an explicit-only cast to avoid ambiguity in function overload resolution.

---

## Short Aliases (PRAGMA)

For less verbose document composition, enable HTML-inspired short aliases:

```sql
PRAGMA duck_block_aliases;

-- Now you can use short names
SELECT page([
    h1('DuckDB Search'),
    p([text('Found '), b('3'), text(' results.')]),
    h2('Extension 1'),
    p([text('Link: '), a('https://github.com', 'GitHub')]),
    pre('sql', 'LOAD extension;')
]);
```

### Available Aliases

| Short | Full Function | Description |
|-------|---------------|-------------|
| `page`, `doc` | `duck_blocks_assemble` | Document assembly |
| `h1`-`h6` | `duck_block_heading(1-6, ...)` | Headings |
| `p` | `duck_block_paragraph` | Paragraph |
| `pre` | `duck_block_code` | Code block |
| `blockquote`, `bq` | `duck_block_blockquote` | Block quote |
| `ul` | `duck_block_list_block(false, ...)` | Unordered list |
| `ol` | `duck_block_list_block(true, ...)` | Ordered list |
| `li` | `duck_block_list_item` | List item |
| `hr` | `duck_block_hr` | Horizontal rule |
| `img` | `duck_block_image` | Image |
| `div` | `duck_block_div` | Generic container |
| `text` | `duck_block_text` | Plain text |
| `b`, `strong` | `duck_block_bold` | Bold |
| `i`, `em` | `duck_block_italic` | Italic |
| `a` | `duck_block_link` | Link |
| `code` | `duck_block_inline_code` | Inline code |
| `s`, `del` | `duck_block_strikethrough` | Strikethrough |
| `sup` | `duck_block_superscript` | Superscript |
| `sub` | `duck_block_subscript` | Subscript |
| `span` | `duck_block_span` | Inline container |
| `math` | `duck_block_math` | Math expression |

---

## See Also

- [API Reference](api.md) - Complete function signatures
- [Inline Builders](inline_builders.md) - Inline element construction
- [V2 API Design](v2_api_design.md) - Design rationale
