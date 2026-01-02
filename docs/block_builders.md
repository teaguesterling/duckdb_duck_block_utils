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

### db_heading

```sql
db_heading(level INTEGER, content duck_block_content) → LIST(duck_block)
```

**Parameters:**
- `level` - Heading level (1-6), stored in `attributes['heading_level']`
- `content` - Text or inline children

**Example:**
```sql
-- Simple heading
SELECT db_heading(1, 'Introduction');

-- Heading with inline formatting
SELECT db_heading(2, [db_text('Hello '), db_bold('World')]);

-- Access heading level
SELECT db_heading(2, 'Title')[1].attributes['heading_level'];
-- Returns: '2'
```

### db_paragraph

```sql
db_paragraph(content duck_block_content) → LIST(duck_block)
```

**Example:**
```sql
-- Simple paragraph
SELECT db_paragraph('This is body text.');

-- Paragraph with rich content
SELECT db_paragraph([
    db_text('Click '),
    db_link('https://example.com', 'here'),
    db_text(' to learn more.')
]);
```

### db_code

```sql
db_code(content duck_block_content) → LIST(duck_block)
db_code(language VARCHAR, content duck_block_content) → LIST(duck_block)
```

**Parameters:**
- `language` - (Optional, first when specified) Programming language
- `content` - Code content

**Example:**
```sql
SELECT db_code('print("hello")');
SELECT db_code('python', 'def hello():\n    print("hi")');
```

### db_blockquote

```sql
db_blockquote(content duck_block_content) → LIST(duck_block)
db_blockquote(level INTEGER, content duck_block_content) → LIST(duck_block)
```

**Parameters:**
- `level` - (Optional, first when specified) Nesting level, defaults to 1
- `content` - Quoted content

### db_list_block

```sql
db_list_block(items VARCHAR[]) → LIST(duck_block)
db_list_block(ordered BOOLEAN, items VARCHAR[]) → LIST(duck_block)
```

Creates a list with simple string items.

**Parameters:**
- `ordered` - (Optional, first when specified) True for numbered list
- `items` - Array of item strings

**Example:**
```sql
SELECT db_list_block(['First', 'Second', 'Third']);
SELECT db_list_block(true, ['Step 1', 'Step 2', 'Step 3']);
```

### db_list_item

```sql
db_list_item(content duck_block_content) → LIST(duck_block)
db_list_item(ordered BOOLEAN, content duck_block_content) → LIST(duck_block)
```

Creates a single list item with rich content.

**Parameters:**
- `ordered` - (Optional, first when specified) Bullet style
- `content` - Text or inline children

**Example:**
```sql
-- Simple item
SELECT db_list_item('Item text');

-- Item with inline content (links, badges, etc.)
SELECT db_list_item([
    db_link('https://github.com/org/repo', 'GitHub'),
    db_text(' '),
    db_inline_image('https://badge.svg', 'CI Status')
]);
```

### db_hr

```sql
db_hr() → LIST(duck_block)
```

### db_metadata

```sql
db_metadata(yaml_content VARCHAR) → LIST(duck_block)
```

**Example:**
```sql
SELECT db_metadata('title: My Document
author: Jane Doe
date: 2024-01-01');
```

### db_image

```sql
db_image(src VARCHAR) → LIST(duck_block)
db_image(src VARCHAR, alt VARCHAR) → LIST(duck_block)
db_image(src VARCHAR, alt VARCHAR, title VARCHAR) → LIST(duck_block)
```

### db_raw

```sql
db_raw(content VARCHAR) → LIST(duck_block)
db_raw(format VARCHAR, content VARCHAR) → LIST(duck_block)
```

**Parameters:**
- `format` - (Optional, first when specified) 'html', 'xml', 'latex'
- `content` - Raw markup

---

## Assembly Functions

### db_assemble

Flattens nested block lists and assigns sequential `element_order`.

```sql
db_assemble(blocks LIST(LIST(duck_block))) → LIST(duck_block)
```

**Example:**
```sql
SELECT db_assemble([
    db_heading(1, 'Title'),
    db_paragraph('Intro text'),
    db_paragraph([db_text('Rich '), db_bold('content')]),
    db_hr()
]);
-- Returns flat list with element_order 0, 1, 2, 3, 4, 5
```

### db_document

Alias for `db_assemble` for semantic clarity.

```sql
db_document(blocks LIST(LIST(duck_block))) → LIST(duck_block)
```

### db_section

Creates a section with heading and optional children.

```sql
db_section(level INTEGER, title duck_block_content) → LIST(duck_block)
db_section(level INTEGER, title duck_block_content, children LIST(LIST(duck_block))) → LIST(duck_block)
```

**Example:**
```sql
SELECT db_section(2, 'Getting Started', [
    db_paragraph('Welcome to the guide.'),
    db_code('bash', 'npm install mypackage'),
    db_paragraph('Now you are ready.')
]);
-- Returns: [heading, paragraph, code, paragraph]
```

### db_rebase_levels

Adjust heading levels by offset.

```sql
db_rebase_levels(blocks LIST(duck_block), offset INTEGER) → LIST(duck_block)
```

**Example:**
```sql
-- Include subdocument with adjusted levels
SELECT db_assemble([
    db_heading(1, 'Main Document'),
    db_rebase_levels(subdoc_blocks, 1)  -- h1→h2, h2→h3, etc.
]);
```

### db_concat

Concatenate lists without renumbering.

```sql
db_concat(blocks1 LIST(duck_block), blocks2 LIST(duck_block)) → LIST(duck_block)
```

---

## Complete Examples

### Report Generation

```sql
SELECT db_assemble([
    db_metadata('title: Q4 Sales Report'),
    db_heading(1, 'Q4 2024 Sales Report'),
    db_paragraph([
        db_text('Generated on '),
        db_bold(current_date::VARCHAR)
    ]),
    db_section(2, 'Executive Summary', [
        db_paragraph('Total revenue: $1.2M')
    ]),
    db_section(2, 'Top Products', [
        db_list_block(['Product A', 'Product B', 'Product C'])
    ]),
    db_hr()
]);
```

### Dashboard with Rich Links

```sql
SELECT db_assemble([
    db_heading(1, 'Project Dashboard'),
    db_heading(2, project_name),
    db_paragraph(COALESCE(description, db_italic('No description'))),
    db_list_item([
        db_link(github_url, 'GitHub'),
        db_text(' '),
        db_inline_image(ci_badge_url, 'CI')
    ]),
    db_list_item([
        db_link(docs_url, 'Documentation'),
        db_text(' '),
        db_inline_image(docs_badge_url, 'Docs')
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
4. **db_assemble** flattens LIST(LIST(duck_block)) → LIST(duck_block)
5. **element_order** assigned sequentially (0, 1, 2, ...)

---

## See Also

- [API Reference](api.md) - Complete function signatures
- [Inline Builders](inline_builders.md) - Inline element construction
- [V2 API Design](v2_api_design.md) - Design rationale
