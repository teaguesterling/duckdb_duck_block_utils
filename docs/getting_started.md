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
SELECT doc_assemble([
    doc_heading('Welcome', 1),
    doc_paragraph('This is my first document.'),
    doc_paragraph('It has multiple paragraphs.')
]);
```

The `doc_assemble` function takes a list of blocks and assigns sequential `element_order` values (0, 1, 2, ...).

## Understanding doc_element

Both blocks and inlines use the unified `doc_element` type, distinguished by the `kind` field:

```sql
SELECT doc_heading('Hello', 1);
```

Output:
```
{
  kind: 'block',
  element_type: 'heading',
  content: 'Hello',
  level: 1,
  encoding: 'text',
  attributes: {},
  element_order: 0
}
```

You can access fields using dot notation:

```sql
SELECT doc_heading('Hello', 1).element_type;  -- 'heading'
SELECT doc_heading('Hello', 1).level;         -- 1
SELECT doc_heading('Hello', 1).kind;          -- 'block'
```

## Building Rich Documents

### Code Blocks

```sql
SELECT doc_code('SELECT * FROM users;', 'sql');
```

The language is stored in the `attributes` map:

```sql
SELECT doc_code('print("hi")', 'python').attributes['language'];
-- Returns: 'python'
```

### Lists

```sql
-- Unordered list
SELECT doc_list_block(['Item 1', 'Item 2', 'Item 3']);

-- Ordered list
SELECT doc_list_block(['First', 'Second', 'Third'], true);
```

### Images

```sql
SELECT doc_image('/path/to/image.png', 'Alt text', 'Image title');
```

### Metadata (YAML Frontmatter)

```sql
SELECT doc_metadata('title: My Document
author: Jane Doe
date: 2024-01-15');
```

## Working with Inline Elements

Inline elements have `kind='inline'` and are used for rich text formatting:

```sql
SELECT doc_text('Hello world');
-- Returns: {kind: 'inline', element_type: 'text', content: 'Hello world', level: 1, ...}

SELECT doc_link('Click here', 'https://example.com');
-- Returns: {kind: 'inline', element_type: 'link', content: 'Click here',
--           attributes: {href: 'https://example.com'}, ...}

SELECT doc_bold('Important');
-- Returns: {kind: 'inline', element_type: 'bold', content: 'Important', ...}
```

### Building Rich Text

Combine inline elements into arrays:

```sql
SELECT [
    doc_text('Click '),
    doc_link('here', 'https://example.com'),
    doc_text(' to learn more about '),
    doc_bold('DuckDB'),
    doc_text('.')
];
```

## Creating Sections

The `doc_section` function creates a heading with child content:

```sql
SELECT doc_section('Introduction', 1, [
    doc_paragraph('Welcome to the guide.'),
    doc_paragraph('Let us begin.')
]);
```

This returns a list of 3 blocks: the heading plus two paragraphs.

## Combining Documents

### Using doc_concat

Concatenate two block lists:

```sql
SELECT doc_assemble(doc_concat(
    doc_section('Part 1', 1, [doc_paragraph('Content 1')]),
    doc_section('Part 2', 1, [doc_paragraph('Content 2')])
));
```

### Using doc_blocks_merge

Merge with automatic order adjustment:

```sql
SELECT doc_blocks_merge(
    [doc_heading('First', 1)],
    [doc_heading('Second', 1)]
);
-- First block: element_order=0
-- Second block: element_order=1
```

## Filtering Documents

### Keep Only Certain Types

```sql
SELECT doc_blocks_filter(my_doc, ['heading', 'paragraph']);
```

### Exclude Certain Types

```sql
SELECT doc_blocks_exclude(my_doc, ['hr', 'raw']);
```

## Extracting Information

### Get Plain Text

```sql
SELECT doc_blocks_to_text([
    doc_heading('Title', 1),
    doc_paragraph('Body text here.')
]);
-- Returns: "Title\n\nBody text here."
```

### Get All Headings

```sql
SELECT doc_blocks_headings(my_doc);
-- Returns: [{level: 1, title: 'Title', id: '', element_order: 0}, ...]
```

### Get Document Statistics

```sql
SELECT doc_blocks_stats(my_doc);
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
    doc_assemble([
        doc_heading('Introduction', 1),
        doc_paragraph('Hello world.')
    ])
);
```

Query document content:

```sql
-- Get all headings from all documents
SELECT id, title, doc_blocks_headings(blocks)
FROM documents;

-- Find documents with code blocks
SELECT id, title
FROM documents
WHERE len(doc_blocks_code_blocks(blocks)) > 0;
```

## Adjusting Heading Levels

When embedding one document inside another, adjust heading levels:

```sql
-- Original subdocument has h1, h2
-- Embed it as h2, h3 (offset by 1)
SELECT doc_assemble(doc_concat(
    [doc_heading('Main Document', 1)],
    doc_rebase_levels(subdoc_blocks, 1)
));
```

## Validating Blocks

Check if elements are valid:

```sql
SELECT doc_element_valid(doc_heading('Title', 1));  -- true
SELECT doc_element_valid(duck_block('invalid', 'x'));  -- false
```

## Next Steps

- See [API Reference](api_reference.md) for all available functions
- See [Examples](examples.md) for common patterns
- See [Type Functions](type_functions.md) for integration with other extensions
- See [Inline Builders](inline_builders.md) for rich text formatting
