# Integration Guide

How to integrate `duck_block_utils` with other DuckDB extensions and external systems.

## Integrating with Other Extensions

### Standard Type Interface

The extension provides a standard interface for working with `doc_element` values:

| Function | Purpose |
|----------|---------|
| `duck_block(...)` | Create a block element from components |
| `duck_inline(...)` | Create an inline element from components |
| `to_doc_element(struct)` | Convert compatible struct to doc_element |
| `doc_element_valid(elem)` | Validate an element |
| `doc_element_kind(elem)` | Extract kind field ('block' or 'inline') |
| `doc_element_type(elem)` | Extract element_type field |
| `doc_element_content(elem)` | Extract content field |
| `doc_element_level(elem)` | Extract level field |
| `doc_element_attr(elem, key)` | Extract attribute value |

### Creating Elements from Your Extension

If your extension produces document content, use the constructors:

```cpp
// C++ example in another extension
#include "duck_block_utils/block_types.hpp"

Value CreateHeadingBlock(const string& title, int level) {
    child_list_t<Value> values;
    values.push_back(make_pair("kind", Value("block")));
    values.push_back(make_pair("element_type", Value("heading")));
    values.push_back(make_pair("content", Value(title)));
    values.push_back(make_pair("level", Value(level)));
    values.push_back(make_pair("encoding", Value("text")));
    values.push_back(make_pair("attributes", Value::MAP(...)));
    values.push_back(make_pair("element_order", Value(0)));
    return Value::STRUCT(std::move(values));
}
```

Or use SQL:

```sql
-- Create blocks using the constructor
SELECT duck_block('heading', my_title, my_level, 'text', MAP{}, 0)
FROM my_table;

-- Create inline elements
SELECT duck_inline('link', my_text, 1, 'text', MAP{'href': my_url}, 0)
FROM my_table;
```

### Consuming Elements in Your Extension

Read element fields using the accessors:

```sql
SELECT
    doc_element_kind(elem) as kind,
    doc_element_type(elem) as type,
    doc_element_content(elem) as content,
    doc_element_level(elem) as level
FROM UNNEST(my_elements) AS t(elem);
```

## Working with Markdown Extensions

### duckdb_markdown Integration

If using a Markdown parsing extension, convert parsed blocks:

```sql
-- Assuming markdown extension returns similar struct
SELECT doc_assemble(list(
    to_doc_element(parsed_block)
))
FROM markdown_parse(my_markdown_text);
```

### Exporting to Markdown

Convert elements to a format suitable for Markdown rendering:

```sql
SELECT
    CASE element_type
        WHEN 'heading' THEN repeat('#', level) || ' ' || content
        WHEN 'paragraph' THEN content
        WHEN 'code' THEN '```' || COALESCE(doc_element_attr(elem, 'language'), '') || E'\n' || content || E'\n```'
        WHEN 'hr' THEN '---'
        WHEN 'blockquote' THEN '> ' || content
        ELSE content
    END as markdown_line
FROM UNNEST(my_blocks) AS t(elem)
ORDER BY element_order;
```

## JSON Import/Export

### Exporting to JSON

```sql
SELECT json_group_array(json_object(
    'kind', kind,
    'type', element_type,
    'content', content,
    'level', level,
    'encoding', encoding,
    'order', element_order
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

The `doc_element` structure is designed to be compatible with Pandoc's AST. Here's how to map between them:

### Pandoc Block Types to doc_element

| Pandoc Type | element_type | Notes |
|-------------|--------------|-------|
| Header | heading | Level in level field |
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
-- Store elements in a normalized table
CREATE TABLE document_elements (
    doc_id INTEGER REFERENCES documents(id),
    element_order INTEGER,
    kind VARCHAR NOT NULL,
    element_type VARCHAR NOT NULL,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR DEFAULT 'text',
    attributes JSON,
    PRIMARY KEY (doc_id, element_order)
);

-- Reconstruct document
SELECT doc_assemble(list(
    duck_block(
        element_type,
        content,
        level,
        encoding,
        json_to_map(attributes),
        element_order
    )
    ORDER BY element_order
))
FROM document_elements
WHERE doc_id = ?;
```

### Denormalized Storage

```sql
-- Store entire documents as LIST
CREATE TABLE documents (
    id INTEGER PRIMARY KEY,
    title VARCHAR,
    blocks LIST(STRUCT(
        kind VARCHAR,
        element_type VARCHAR,
        content VARCHAR,
        level INTEGER,
        encoding VARCHAR,
        attributes MAP(VARCHAR, VARCHAR),
        element_order INTEGER
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
    element_order INTEGER,
    element_data JSON,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Reconstruct document from events
WITH current_elements AS (
    SELECT DISTINCT ON (element_order)
        element_order,
        element_data
    FROM document_events
    WHERE doc_id = ?
        AND event_type != 'delete'
    ORDER BY element_order, timestamp DESC
)
SELECT doc_assemble(list(
    to_doc_element(json_to_struct(element_data))
    ORDER BY element_order
))
FROM current_elements;
```

## REST API Integration

### Serialize for API Response

```sql
SELECT json_object(
    'id', doc_id,
    'title', title,
    'blocks', (
        SELECT json_group_array(json_object(
            'kind', kind,
            'type', element_type,
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
    (SELECT bool_and(doc_element_valid(block)) FROM UNNEST(blocks) AS t(block))
    AND len(blocks) > 0
    AND (SELECT COUNT(*) FROM UNNEST(blocks) WHERE element_type = 'heading' AND level = 1) <= 1
);

-- Use in a check constraint
ALTER TABLE documents ADD CONSTRAINT valid_blocks
    CHECK (validate_document(blocks));
```
