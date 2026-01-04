# Examples & Cookbook

Practical examples and common patterns for using `duck_block_utils`.

## Document Generation

### Generate Documentation from Schema

```sql
-- Create documentation for each table
SELECT db_assemble([
    db_heading(2, table_name),
    db_paragraph('Columns: ' || column_count::VARCHAR),
    db_code('sql',
        'SELECT * FROM ' || table_name || ' LIMIT 10;'
    )
])
FROM (
    SELECT table_name, COUNT(*) as column_count
    FROM information_schema.columns
    GROUP BY table_name
);
```

### Generate API Documentation

```sql
WITH endpoints AS (
    SELECT 'GET /users' as endpoint, 'List all users' as description
    UNION ALL
    SELECT 'POST /users', 'Create a new user'
    UNION ALL
    SELECT 'GET /users/:id', 'Get user by ID'
)
SELECT db_assemble(list(block ORDER BY ord))
FROM (
    SELECT 1 as ord, db_heading(1, 'API Reference') as block
    UNION ALL
    SELECT
        row_number() OVER () + 1,
        db_section(3, endpoint, [db_paragraph(description)])
    FROM endpoints
);
```

## Content Aggregation

### Combine Multiple Documents

```sql
-- Combine chapters from different sources
WITH chapters AS (
    SELECT 1 as chapter_num, blocks as content FROM chapter1
    UNION ALL
    SELECT 2, blocks FROM chapter2
    UNION ALL
    SELECT 3, blocks FROM chapter3
)
SELECT db_assemble(
    list(
        db_concat(
            db_section('Chapter ' || chapter_num::VARCHAR, 1, []),
            content
        )
    )
)
FROM chapters
ORDER BY chapter_num;
```

### Merge Documents with Level Adjustment

```sql
-- Main document with embedded subdocuments
SELECT db_assemble(db_concat(
    flatten([
        db_heading(1, 'Main Report'),
        db_paragraph('This report contains multiple sections.')
    ]),
    db_concat(
        db_rebase_levels(section1_blocks, 1),  -- h1→h2, h2→h3
        db_rebase_levels(section2_blocks, 1)
    )
));
```

## Content Analysis

### Extract Table of Contents

```sql
SELECT
    repeat('  ', level - 1) || '- ' || title as toc_line
FROM UNNEST(db_blocks_headings(my_doc)) AS t(level, title, id, element_order)
ORDER BY element_order;
```

### Find Code Examples by Language

```sql
SELECT
    language,
    content,
    element_order
FROM UNNEST(db_blocks_code_blocks(my_doc))
WHERE language = 'python';
```

### Document Statistics Report

```sql
SELECT
    element_type,
    count,
    round(avg_content_length, 1) as avg_length,
    total_content_length
FROM UNNEST(db_blocks_stats(my_doc))
ORDER BY count DESC;
```

### Word Count

```sql
SELECT
    array_length(
        string_split(db_blocks_to_text(my_doc), ' ')
    ) as word_count;
```

## Content Transformation

### Remove Empty Paragraphs

```sql
SELECT db_blocks_reorder(
    list_filter(
        my_doc,
        block -> NOT (
            block.element_type = 'paragraph'
            AND block.content = ''
        )
    )
);
```

### Extract Only Text Content

```sql
SELECT db_blocks_filter(my_doc, ['heading', 'paragraph', 'blockquote']);
```

### Remove Metadata and Horizontal Rules

```sql
SELECT db_blocks_exclude(my_doc, ['metadata', 'hr', 'raw']);
```

### Update All Code Block Languages

```sql
SELECT list_transform(
    my_doc,
    block -> CASE
        WHEN block.element_type = 'code'
        THEN duck_block_set_content(block, '# Python\n' || block.content)
        ELSE block
    END
);
```

## Document Queries

### Find Documents with Specific Heading

```sql
SELECT id, title
FROM documents
WHERE EXISTS (
    SELECT 1
    FROM UNNEST(db_blocks_headings(blocks)) AS h
    WHERE h.title ILIKE '%introduction%'
);
```

### Find Documents with Code in Specific Language

```sql
SELECT id, title
FROM documents
WHERE EXISTS (
    SELECT 1
    FROM UNNEST(db_blocks_code_blocks(blocks)) AS c
    WHERE c.language = 'python'
);
```

### Count Headings per Document

```sql
SELECT
    id,
    title,
    len(db_blocks_headings(blocks)) as heading_count
FROM documents
ORDER BY heading_count DESC;
```

## Data-Driven Documents

### Generate Report from Query Results

```sql
WITH sales_data AS (
    SELECT region, SUM(amount) as total
    FROM sales
    GROUP BY region
)
SELECT db_assemble([
    db_heading(1, 'Sales Report'),
    db_metadata('generated: ' || current_date::VARCHAR),
    db_heading(2, 'Summary'),
    db_paragraph('Total regions: ' || COUNT(*)::VARCHAR),
    db_heading(2, 'By Region')
] || list(
    db_section(3, region, [
        db_paragraph('Total: $' || total::VARCHAR)
    ])
))
FROM sales_data;
```

### Convert Table to Document

```sql
SELECT db_assemble([
    db_heading(1, 'User Directory')
] || list(
    db_section(2, name, [
        db_paragraph('Email: ' || email),
        db_paragraph('Role: ' || role)
    ])
    ORDER BY name
))
FROM users;
```

## Working with Inline Elements

### Build Rich Text Links

```sql
-- Create a paragraph with formatted inline content
SELECT flatten([
    db_text('Visit '),
    db_link('https://example.com', 'our website'),
    db_text(' or email '),
    db_link('mailto:support@example.com', 'support@example.com'),
    db_text('.')
]);
```

### Generate Badge Links

```sql
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

## Integration Patterns

### Export for Markdown Conversion

```sql
-- Structure suitable for conversion to Markdown
SELECT
    element_order,
    element_type,
    level,
    content
FROM UNNEST(db_assemble(my_blocks))
ORDER BY element_order;
```

### Create from JSON Import

```sql
-- Assuming JSON with {type, text, heading_level} structure
SELECT db_assemble(list(
    CASE json_data->>'type'
        WHEN 'heading' THEN db_heading(
            (json_data->>'heading_level')::INTEGER,
            json_data->>'text'
        )
        WHEN 'paragraph' THEN db_paragraph(json_data->>'text')
        WHEN 'code' THEN db_code(
            json_data->>'language',
            json_data->>'text'
        )
    END
))
FROM json_documents;
```

### Validate Before Insert

```sql
INSERT INTO documents (id, title, blocks)
SELECT id, title, blocks
FROM new_documents
WHERE (
    SELECT bool_and(duck_block_valid(block))
    FROM UNNEST(blocks) AS t(block)
);
```

## Using Short Aliases

Enable short aliases for more concise code:

```sql
PRAGMA duck_block_aliases;

-- Same examples with short names
SELECT page([
    h1('Sales Report'),
    p([text('Generated: '), b(current_date::VARCHAR)]),
    h2('Summary'),
    ul(['Revenue: $1M', 'Customers: 500', 'Growth: 25%']),
    h2('Details'),
    div('charts', [
        p('See attached visualizations.')
    ])
]);
```

### Rich Lists with Aliases

```sql
PRAGMA duck_block_aliases;

SELECT page([
    h1('Project Links'),
    ol([
        li([a('https://github.com/duckdb/duckdb', 'DuckDB'), text(' - main repo')]),
        li([a('https://duckdb.org', 'Documentation'), text(' - user guide')]),
        li([b('Extensions'), text(' - see community repo')])
    ])
]);
```

## Performance Tips

### Use Materialized Views for Large Documents

```sql
CREATE TABLE document_headings AS
SELECT
    doc_id,
    h.*
FROM documents,
LATERAL UNNEST(db_blocks_headings(blocks)) AS h;

-- Query headings efficiently
SELECT * FROM document_headings WHERE doc_id = 123;
```

### Index on Block Statistics

```sql
-- Add computed columns for common queries
ALTER TABLE documents ADD COLUMN heading_count INTEGER;
UPDATE documents SET heading_count = len(db_blocks_headings(blocks));
```
