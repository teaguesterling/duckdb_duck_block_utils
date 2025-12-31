# Integration Guide

How to integrate `duck_block_utils` with other DuckDB extensions and external systems.

## Integrating with Other Extensions

### Standard Type Interface

The extension provides a standard interface for working with `doc_block` values:

| Function | Purpose |
|----------|---------|
| `duck_block(...)` | Create a block from components |
| `to_duck_block(struct)` | Convert compatible struct to doc_block |
| `duck_block_valid(block)` | Validate a block |
| `duck_block_type(block)` | Extract block_type field |
| `duck_block_content(block)` | Extract content field |
| `duck_block_level(block)` | Extract level field |
| `duck_block_attr(block, key)` | Extract attribute value |

### Creating Blocks from Your Extension

If your extension produces document content, use the constructors:

```cpp
// C++ example in another extension
#include "duck_block_utils/block_types.hpp"

Value CreateHeadingBlock(const string& title, int level) {
    child_list_t<Value> values;
    values.push_back(make_pair("block_type", Value("heading")));
    values.push_back(make_pair("content", Value(title)));
    values.push_back(make_pair("level", Value(level)));
    values.push_back(make_pair("encoding", Value("text")));
    values.push_back(make_pair("attributes", Value::MAP(...)));
    values.push_back(make_pair("block_order", Value(0)));
    return Value::STRUCT(std::move(values));
}
```

Or use SQL:

```sql
-- Create blocks using the constructor
SELECT duck_block('heading', my_title, my_level, 'text', MAP{}, 0)
FROM my_table;
```

### Consuming Blocks in Your Extension

Read block fields using the accessors:

```sql
SELECT
    duck_block_type(block) as type,
    duck_block_content(block) as content,
    duck_block_level(block) as level
FROM UNNEST(my_blocks) AS t(block);
```

## Working with Markdown Extensions

### duckdb_markdown Integration

If using a Markdown parsing extension, convert parsed blocks:

```sql
-- Assuming markdown extension returns similar struct
SELECT doc_assemble(list(
    to_duck_block(parsed_block)
))
FROM markdown_parse(my_markdown_text);
```

### Exporting to Markdown

Convert blocks to a format suitable for Markdown rendering:

```sql
SELECT
    CASE block_type
        WHEN 'heading' THEN repeat('#', level) || ' ' || content
        WHEN 'paragraph' THEN content
        WHEN 'code' THEN '```' || COALESCE(duck_block_attr(block, 'language'), '') || E'\n' || content || E'\n```'
        WHEN 'hr' THEN '---'
        WHEN 'blockquote' THEN '> ' || content
        ELSE content
    END as markdown_line
FROM UNNEST(my_blocks) AS t(block)
ORDER BY block_order;
```

## JSON Import/Export

### Exporting to JSON

```sql
SELECT json_group_array(json_object(
    'type', block_type,
    'content', content,
    'level', level,
    'encoding', encoding,
    'order', block_order
))
FROM UNNEST(my_blocks);
```

### Importing from JSON

```sql
WITH parsed AS (
    SELECT json_each.value as item
    FROM json_each(my_json_array)
)
SELECT doc_assemble(list(
    duck_block(
        item->>'type',
        item->>'content',
        (item->>'level')::INTEGER,
        COALESCE(item->>'encoding', 'text'),
        MAP{},
        0
    )
))
FROM parsed;
```

## Pandoc AST Integration

The `doc_block` structure is designed to be compatible with Pandoc's AST. Here's how to map between them:

### Pandoc Block Types to doc_block

| Pandoc Type | doc_block type | Notes |
|-------------|----------------|-------|
| Header | heading | Level in first element |
| Para | paragraph | |
| CodeBlock | code | Language in attributes |
| BlockQuote | blockquote | |
| BulletList | list | JSON-encoded, ordered=false |
| OrderedList | list | JSON-encoded, ordered=true |
| Table | table | JSON-encoded |
| HorizontalRule | hr | |
| RawBlock | raw | Format in attributes |

### Converting Pandoc JSON

```sql
-- Assuming Pandoc JSON AST structure
SELECT doc_assemble(list(
    CASE block->>'t'
        WHEN 'Header' THEN doc_heading(
            pandoc_inlines_to_text(block->'c'->2),
            (block->'c'->0)::INTEGER
        )
        WHEN 'Para' THEN doc_paragraph(
            pandoc_inlines_to_text(block->'c')
        )
        WHEN 'CodeBlock' THEN doc_code(
            block->'c'->1,
            block->'c'->0->1->0
        )
        -- ... other types
    END
))
FROM json_each(pandoc_ast->'blocks') AS block;
```

## Database Schema Patterns

### Normalized Storage

```sql
-- Store blocks in a normalized table
CREATE TABLE document_blocks (
    doc_id INTEGER REFERENCES documents(id),
    block_order INTEGER,
    block_type VARCHAR NOT NULL,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR DEFAULT 'text',
    attributes JSON,
    PRIMARY KEY (doc_id, block_order)
);

-- Reconstruct document
SELECT doc_assemble(list(
    duck_block(
        block_type,
        content,
        level,
        encoding,
        json_to_map(attributes),
        block_order
    )
    ORDER BY block_order
))
FROM document_blocks
WHERE doc_id = ?;
```

### Denormalized Storage

```sql
-- Store entire documents as LIST
CREATE TABLE documents (
    id INTEGER PRIMARY KEY,
    title VARCHAR,
    blocks LIST(STRUCT(
        block_type VARCHAR,
        content VARCHAR,
        level INTEGER,
        encoding VARCHAR,
        attributes MAP(VARCHAR, VARCHAR),
        block_order INTEGER
    )),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

## Event Sourcing Pattern

```sql
-- Store document changes as events
CREATE TABLE document_events (
    event_id INTEGER PRIMARY KEY,
    doc_id INTEGER,
    event_type VARCHAR,  -- 'insert', 'update', 'delete'
    block_order INTEGER,
    block_data JSON,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Reconstruct document from events
WITH current_blocks AS (
    SELECT DISTINCT ON (block_order)
        block_order,
        block_data
    FROM document_events
    WHERE doc_id = ?
        AND event_type != 'delete'
    ORDER BY block_order, timestamp DESC
)
SELECT doc_assemble(list(
    to_duck_block(json_to_struct(block_data))
    ORDER BY block_order
))
FROM current_blocks;
```

## REST API Integration

### Serialize for API Response

```sql
SELECT json_object(
    'id', doc_id,
    'title', title,
    'blocks', (
        SELECT json_group_array(json_object(
            'type', block_type,
            'content', content,
            'level', level,
            'attributes', attributes
        ))
        FROM UNNEST(blocks)
    ),
    'stats', (
        SELECT json_object(
            'block_count', len(blocks),
            'heading_count', len(doc_blocks_headings(blocks)),
            'word_count', array_length(string_split(doc_blocks_to_text(blocks), ' '))
        )
    )
)
FROM documents
WHERE id = ?;
```

## Validation Middleware

```sql
-- Create a validation function for use in triggers or checks
CREATE MACRO validate_document(blocks) AS (
    (SELECT bool_and(duck_block_valid(block)) FROM UNNEST(blocks) AS t(block))
    AND len(blocks) > 0
    AND (SELECT COUNT(*) FROM UNNEST(blocks) WHERE block_type = 'heading' AND level = 1) <= 1
);

-- Use in a check constraint
ALTER TABLE documents ADD CONSTRAINT valid_blocks
    CHECK (validate_document(blocks));
```
