# Type Functions

Standard type functions for the `doc_block` type, designed for integration with other DuckDB extensions.

## Constructors

### duck_block (Full Constructor)

Creates a `doc_block` with all fields specified.

```sql
duck_block(
    block_type VARCHAR,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR,
    attributes MAP(VARCHAR, VARCHAR),
    block_order INTEGER
) → doc_block
```

**Example:**
```sql
SELECT duck_block(
    'heading',
    'My Title',
    1,
    'text',
    MAP{'id': 'my-title'},
    0
);
```

### duck_block (Simple Constructor)

Creates a `doc_block` with minimal fields, using defaults for the rest.

```sql
duck_block(block_type VARCHAR, content VARCHAR) → doc_block
```

**Defaults:**
- `level`: NULL
- `encoding`: 'text'
- `attributes`: empty map
- `block_order`: 0

**Example:**
```sql
SELECT duck_block('paragraph', 'Hello world');
```

### to_duck_block

Converts a compatible struct to a `doc_block`.

```sql
to_duck_block(input STRUCT) → doc_block
```

**Notes:**
- Accepts structs with 2-6 fields
- Missing fields are filled with defaults
- Useful for importing data from other sources

**Example:**
```sql
SELECT to_duck_block({'block_type': 'paragraph', 'content': 'Text'});
```

---

## Validation

### duck_block_valid

Checks if a block has valid structure and values.

```sql
duck_block_valid(block doc_block) → BOOLEAN
```

**Validation Rules:**
- `block_type` must be one of: heading, paragraph, code, blockquote, list, table, hr, metadata, image, raw
- `encoding` (if not NULL) must be one of: text, json, yaml, html, xml
- `block_order` (if not NULL) must be non-negative

**Example:**
```sql
SELECT duck_block_valid(doc_heading('Title', 1));
-- Returns: true

SELECT duck_block_valid(duck_block('invalid_type', 'content'));
-- Returns: false
```

---

## Field Accessors

These functions extract individual fields from a `doc_block`. They're useful when you can't use the `.field` syntax (e.g., in certain aggregation contexts).

### duck_block_type

```sql
duck_block_type(block doc_block) → VARCHAR
```

Returns the `block_type` field.

### duck_block_content

```sql
duck_block_content(block doc_block) → VARCHAR
```

Returns the `content` field.

### duck_block_level

```sql
duck_block_level(block doc_block) → INTEGER
```

Returns the `level` field (may be NULL).

### duck_block_encoding

```sql
duck_block_encoding(block doc_block) → VARCHAR
```

Returns the `encoding` field.

### duck_block_order

```sql
duck_block_order(block doc_block) → INTEGER
```

Returns the `block_order` field.

### duck_block_attr

```sql
duck_block_attr(block doc_block, key VARCHAR) → VARCHAR
```

Returns the value of an attribute, or NULL if not found.

**Example:**
```sql
SELECT duck_block_attr(doc_code('x=1', 'python'), 'language');
-- Returns: 'python'
```

---

## Field Setters

These functions return a new `doc_block` with one field modified. The original block is unchanged (immutable update pattern).

### duck_block_set_order

```sql
duck_block_set_order(block doc_block, new_order INTEGER) → doc_block
```

Returns a new block with updated `block_order`.

**Example:**
```sql
SELECT duck_block_set_order(doc_heading('Title', 1), 42);
-- Returns heading with block_order=42
```

### duck_block_set_content

```sql
duck_block_set_content(block doc_block, new_content VARCHAR) → doc_block
```

Returns a new block with updated `content`.

**Example:**
```sql
SELECT duck_block_set_content(doc_heading('Old', 1), 'New');
-- Returns heading with content='New'
```

### duck_block_set_level

```sql
duck_block_set_level(block doc_block, new_level INTEGER) → doc_block
```

Returns a new block with updated `level`.

**Example:**
```sql
SELECT duck_block_set_level(doc_heading('Title', 1), 2);
-- Returns heading with level=2
```

---

## Integration Patterns

### Converting External Data

```sql
-- From a table with document content
SELECT to_duck_block(struct_pack(
    block_type := type_column,
    content := text_column
))
FROM my_table;
```

### Validation Pipeline

```sql
-- Filter to only valid blocks
SELECT *
FROM my_blocks
WHERE duck_block_valid(block);
```

### Transforming Blocks

```sql
-- Update all block orders
SELECT duck_block_set_order(block, row_number() OVER () - 1)
FROM my_blocks;
```

### Using with Aggregations

```sql
-- Count by block type
SELECT duck_block_type(block), COUNT(*)
FROM UNNEST(my_blocks) AS t(block)
GROUP BY duck_block_type(block);
```
