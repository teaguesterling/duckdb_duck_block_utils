# Getting Started

This guide will help you get up and running with `duck_block_utils` in minutes.

## Installation

```sql
-- Install from the community repository
INSTALL duck_block_utils FROM community;

-- Load the extension
LOAD duck_block_utils;
```

## Your First Document

Let's create a simple document with a heading and some paragraphs:

```sql
SELECT db_assemble([
    db_heading(1, 'Welcome'),
    db_paragraph('This is my first document.'),
    db_paragraph('It has multiple paragraphs.')
]);
```

The `db_assemble` function takes a list of blocks and assigns sequential `element_order` values (0, 1, 2, ...).

> **Note:** All builder functions follow a "config-first, content-last" pattern.
> For example: `db_heading(level, content)`, `db_code(language, content)`.

## Understanding duck_block

Both blocks and inlines use the unified `duck_block` type, distinguished by the `kind` field:

```sql
SELECT db_heading(1, 'Hello');
```

Output (returns LIST with one element):
```
[{
  kind: 'block',
  element_type: 'heading',
  content: 'Hello',
  level: 1,
  encoding: 'text',
  attributes: {heading_level: '1'},
  element_order: 0
}]
```

You can access fields using dot notation (index first since builders return lists):

```sql
SELECT db_heading(1, 'Hello')[1].element_type;  -- 'heading'
SELECT db_heading(1, 'Hello')[1].attributes['heading_level'];  -- '1'
SELECT db_heading(1, 'Hello')[1].kind;          -- 'block'
```

## Building Rich Documents

### Code Blocks

```sql
SELECT db_code('sql', 'SELECT * FROM users;');
```

The language is stored in the `attributes` map:

```sql
SELECT db_code('python', 'print("hi")')[1].attributes['language'];
-- Returns: 'python'
```

### Lists

```sql
-- Unordered list
SELECT db_list_block(['Item 1', 'Item 2', 'Item 3']);

-- Ordered list
SELECT db_list_block(true, ['First', 'Second', 'Third']);

-- Rich list items with inline content
SELECT db_list_block([
    db_list_item([db_link('https://github.com', 'GitHub'), db_text(' - code hosting')]),
    db_list_item([db_bold('DuckDB'), db_text(' - analytics')])
]);
```

### Images

```sql
SELECT db_image('/path/to/image.png', 'Alt text', 'Image title');
```

### Metadata (YAML Frontmatter)

```sql
SELECT db_metadata('title: My Document
author: Jane Doe
date: 2024-01-15');
```

## Working with Inline Elements

Inline elements have `kind='inline'` and are used for rich text formatting:

```sql
SELECT db_text('Hello world');
-- Returns: {kind: 'inline', element_type: 'text', content: 'Hello world', level: 1, ...}

SELECT db_link('https://example.com', 'Click here');
-- Returns: [{kind: 'inline', element_type: 'link', content: 'Click here',
--            attributes: {href: 'https://example.com'}, ...}]

SELECT db_bold('Important');
-- Returns: {kind: 'inline', element_type: 'bold', content: 'Important', ...}
```

### Building Rich Text

Combine inline elements into arrays:

```sql
SELECT [
    db_text('Click '),
    db_link('https://example.com', 'here'),
    db_text(' to learn more about '),
    db_bold('DuckDB'),
    db_text('.')
];
```

## Creating Sections

The `db_section` function creates a heading with child content:

```sql
SELECT db_section(1, 'Introduction', [
    db_paragraph('Welcome to the guide.'),
    db_paragraph('Let us begin.')
]);
```

This returns a list of 3 blocks: the heading plus two paragraphs.

## Generic Containers (db_div)

Create containers to group content with optional id/class attributes:

```sql
-- Simple container
SELECT db_div([db_paragraph('Content 1'), db_paragraph('Content 2')]);

-- Container with id and class
SELECT db_div('sidebar', 'widget', [
    db_heading(3, 'Related'),
    db_list_block(['A', 'B', 'C'])
]);
```

## Combining Documents

### Using db_concat

Concatenate two block lists:

```sql
SELECT db_assemble(db_concat(
    db_section(1, 'Part 1', [db_paragraph('Content 1')]),
    db_section(1, 'Part 2', [db_paragraph('Content 2')])
));
```

### Using db_blocks_merge

Merge with automatic order adjustment:

```sql
SELECT db_blocks_merge(
    db_heading(1, 'First'),
    db_heading(1, 'Second')
);
-- First block: element_order=0
-- Second block: element_order=1
```

## Filtering Documents

### Keep Only Certain Types

```sql
SELECT db_blocks_filter(my_doc, ['heading', 'paragraph']);
```

### Exclude Certain Types

```sql
SELECT db_blocks_exclude(my_doc, ['hr', 'raw']);
```

## Extracting Information

### Get Plain Text

```sql
SELECT db_blocks_to_text(flatten([
    db_heading(1, 'Title'),
    db_paragraph('Body text here.')
]));
-- Returns: "Title\n\nBody text here."
```

### Get All Headings

```sql
SELECT db_blocks_headings(my_doc);
-- Returns: [{level: 1, title: 'Title', id: '', element_order: 0}, ...]
```

### Get Document Statistics

```sql
SELECT db_blocks_stats(my_doc);
-- Returns: [{element_type: 'paragraph', count: 5, ...}, ...]
```

## Working with Tables

Store documents in tables:

```sql
CREATE TABLE documents (
    id INTEGER,
    title VARCHAR,
    blocks LIST(STRUCT(
        kind VARCHAR,
        element_type VARCHAR,
        content VARCHAR,
        level INTEGER,
        encoding VARCHAR,
        attributes MAP(VARCHAR, VARCHAR),
        element_order INTEGER
    ))
);

INSERT INTO documents VALUES (
    1,
    'My Doc',
    db_assemble([
        db_heading(1, 'Introduction'),
        db_paragraph('Hello world.')
    ])
);
```

Query document content:

```sql
-- Get all headings from all documents
SELECT id, title, db_blocks_headings(blocks)
FROM documents;

-- Find documents with code blocks
SELECT id, title
FROM documents
WHERE len(db_blocks_code_blocks(blocks)) > 0;
```

## Adjusting Heading Levels

When embedding one document inside another, adjust heading levels:

```sql
-- Original subdocument has h1, h2
-- Embed it as h2, h3 (offset by 1)
SELECT db_assemble(db_concat(
    db_heading(1, 'Main Document'),
    db_rebase_levels(subdoc_blocks, 1)
));
```

## Validating Blocks

Check if elements are valid:

```sql
SELECT duck_block_valid(db_heading('Title', 1));  -- true
SELECT duck_block_valid(duck_block('invalid', 'x'));  -- false
```

## Short Aliases (Optional)

For less verbose document composition, enable HTML-inspired short aliases:

```sql
PRAGMA duck_block_aliases;

-- Now you can use short names
SELECT page([
    h1('My Document'),
    p([text('Hello '), b('world'), text('!')]),
    pre('sql', 'SELECT 1;')
]);
```

Available aliases: `h1`-`h6`, `p`, `pre`, `ul`, `ol`, `li`, `div`, `b`, `i`, `a`, `code`, and more.

## Type Casting

Cast strings directly to duck_block (creates text inline):

```sql
SELECT 'hello'::duck_block;
-- Equivalent to db_text('hello')
```

## Rendering to the Terminal

`PRAGMA duck_block_render` registers macros that pretty-print blocks as ANSI
terminal output — headings, tables, lists, code, and structured inline
formatting (bold/italic/code/links). Rich text is built from inline elements,
not markdown syntax in `content`:

```sql
PRAGMA duck_block_render;

-- Render a document (formatting via structured inline elements)
SELECT db_render_blocks(
    db_heading(1, 'Report')
    || db_paragraph([db_text('All systems '), db_bold('nominal'), db_text('.')])
);

-- Render any query as an ANSI table
SELECT rendered FROM db_render_query('SELECT * FROM my_table LIMIT 10');
```

### Pages with embedded query results

`db_page(title, blocks)` composes a titled page, and `db_query_table(q)` drops a
query's results in as a table — a dashboard in one expression:

```sql
SELECT db_render_blocks(db_page('Sales Report', [
    db_paragraph('Top rows:'),
    db_query_table('SELECT * FROM t ORDER BY id'),
    db_paragraph('Aggregate:'),
    db_query_table('SELECT count(*) AS n, round(avg(score), 2) AS avg FROM t')
]));
```

From the shell, use list mode so the escapes render (`... | less -R`). See
[Rendering](rendering.md) for the full macro reference and the width-aware C++
renderer.

## Next Steps

- See [API Reference](api_reference.md) for all available functions
- See [Examples](examples.md) for common patterns
- See [Type Functions](type_functions.md) for integration with other extensions
- See [Inline Builders](inline_builders.md) for rich text formatting
- See [Block Builders](block_builders.md) for complete alias list
