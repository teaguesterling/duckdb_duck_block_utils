# Type Functions

Standard type functions for the `doc_element` type, designed for integration with other DuckDB extensions.

## The Unified doc_element Type

Both block-level and inline elements use the same type, distinguished by the `kind` field:

```sql
STRUCT(
    kind VARCHAR,                       -- 'block' or 'inline'
    element_type VARCHAR,               -- Element type identifier
    content VARCHAR,                    -- Text content
    level INTEGER,                      -- Semantic level
    encoding VARCHAR,                   -- Content encoding
    attributes MAP(VARCHAR, VARCHAR),   -- Key-value metadata
    element_order INTEGER               -- Position in sequence
)
```

## Constructors

### duck_block (Full Constructor)

Creates a `doc_element` with kind='block' and all fields specified.

```sql
duck_block(
    element_type VARCHAR,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR,
    attributes MAP(VARCHAR, VARCHAR),
    element_order INTEGER
) → doc_element
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
-- Returns: {kind: 'block', element_type: 'heading', content: 'My Title', level: 1, ...}
```

### duck_block (Simple Constructor)

Creates a `doc_element` with kind='block' and minimal fields, using defaults for the rest.

```sql
duck_block(element_type VARCHAR, content VARCHAR) → doc_element
```

**Defaults:**
- `kind`: 'block'
- `level`: NULL
- `encoding`: 'text'
- `attributes`: empty map
- `element_order`: 0

**Example:**
```sql
SELECT duck_block('paragraph', 'Hello world');
```

### duck_inline (Full Constructor)

Creates a `doc_element` with kind='inline' and all fields specified.

```sql
duck_inline(
    element_type VARCHAR,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR,
    attributes MAP(VARCHAR, VARCHAR),
    element_order INTEGER
) → doc_element
```

**Example:**
```sql
SELECT duck_inline(
    'link',
    'Click here',
    1,
    'text',
    MAP{'href': 'https://example.com'},
    0
);
```

### duck_inline (Simple Constructor)

Creates a `doc_element` with kind='inline' and minimal fields.

```sql
duck_inline(element_type VARCHAR, content VARCHAR) → doc_element
```

**Defaults:**
- `kind`: 'inline'
- `level`: 1
- `encoding`: 'text'
- `attributes`: empty map
- `element_order`: 0

### to_doc_element

Converts a compatible struct to a `doc_element`.

```sql
to_doc_element(input STRUCT) → doc_element
```

**Notes:**
- Accepts structs with 2-7 fields
- Missing fields are filled with defaults
- Infers `kind` from context if not provided
- Useful for importing data from other sources

**Example:**
```sql
SELECT to_doc_element({'element_type': 'paragraph', 'content': 'Text'});
-- Returns: {kind: 'block', element_type: 'paragraph', content: 'Text', ...}
```

---

## Validation

### doc_element_valid

Checks if an element has valid structure and values.

```sql
doc_element_valid(elem doc_element) → BOOLEAN
```

**Validation Rules:**
- `kind` must be 'block' or 'inline'
- `element_type` must be non-empty
- For blocks: `element_type` must be recognized (heading, paragraph, code, etc.)
- For inlines: `element_type` must be recognized (text, link, bold, etc.)
- `encoding` (if not NULL) must be one of: text, json, yaml, html, xml
- `element_order` (if not NULL) must be non-negative
- For headings: `level` must be 1-6
- For inlines: `level` must be >= 1

**Example:**
```sql
SELECT doc_element_valid(doc_heading('Title', 1));
-- Returns: true

SELECT doc_element_valid(duck_block('invalid_type', 'content'));
-- Returns: false
```

### duck_block_valid

Alias for `doc_element_valid` for blocks.

```sql
duck_block_valid(block doc_element) → BOOLEAN
```

---

## Field Accessors

These functions extract individual fields from a `doc_element`. They're useful when you can't use the `.field` syntax (e.g., in certain aggregation contexts).

### doc_element_kind

```sql
doc_element_kind(elem doc_element) → VARCHAR
```

Returns the `kind` field ('block' or 'inline').

### doc_element_type

```sql
doc_element_type(elem doc_element) → VARCHAR
```

Returns the `element_type` field.

### duck_block_type

Alias for `doc_element_type`.

```sql
duck_block_type(block doc_element) → VARCHAR
```

### doc_element_content

```sql
doc_element_content(elem doc_element) → VARCHAR
```

Returns the `content` field.

### duck_block_content

Alias for `doc_element_content`.

```sql
duck_block_content(block doc_element) → VARCHAR
```

### doc_element_level

```sql
doc_element_level(elem doc_element) → INTEGER
```

Returns the `level` field (may be NULL for blocks, >= 1 for inlines).

### duck_block_level

Alias for `doc_element_level`.

```sql
duck_block_level(block doc_element) → INTEGER
```

### doc_element_encoding

```sql
doc_element_encoding(elem doc_element) → VARCHAR
```

Returns the `encoding` field.

### duck_block_encoding

Alias for `doc_element_encoding`.

```sql
duck_block_encoding(block doc_element) → VARCHAR
```

### doc_element_order

```sql
doc_element_order(elem doc_element) → INTEGER
```

Returns the `element_order` field.

### duck_block_order

Alias for `doc_element_order`.

```sql
duck_block_order(block doc_element) → INTEGER
```

### doc_element_attr

```sql
doc_element_attr(elem doc_element, key VARCHAR) → VARCHAR
```

Returns the value of an attribute, or NULL if not found.

### duck_block_attr

Alias for `doc_element_attr`.

```sql
duck_block_attr(block doc_element, key VARCHAR) → VARCHAR
```

**Example:**
```sql
SELECT duck_block_attr(doc_code('x=1', 'python'), 'language');
-- Returns: 'python'

SELECT doc_element_attr(doc_link('Click', 'https://example.com'), 'href');
-- Returns: 'https://example.com'
```

---

## Field Setters

These functions return a new `doc_element` with one field modified. The original element is unchanged (immutable update pattern).

### doc_element_set_order

```sql
doc_element_set_order(elem doc_element, new_order INTEGER) → doc_element
```

Returns a new element with updated `element_order`.

### duck_block_set_order

Alias for `doc_element_set_order`.

```sql
duck_block_set_order(block doc_element, new_order INTEGER) → doc_element
```

**Example:**
```sql
SELECT duck_block_set_order(doc_heading('Title', 1), 42);
-- Returns heading with element_order=42
```

### doc_element_set_content

```sql
doc_element_set_content(elem doc_element, new_content VARCHAR) → doc_element
```

Returns a new element with updated `content`.

### duck_block_set_content

Alias for `doc_element_set_content`.

```sql
duck_block_set_content(block doc_element, new_content VARCHAR) → doc_element
```

**Example:**
```sql
SELECT duck_block_set_content(doc_heading('Old', 1), 'New');
-- Returns heading with content='New'
```

### doc_element_set_level

```sql
doc_element_set_level(elem doc_element, new_level INTEGER) → doc_element
```

Returns a new element with updated `level`.

### duck_block_set_level

Alias for `doc_element_set_level`.

```sql
duck_block_set_level(block doc_element, new_level INTEGER) → doc_element
```

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
SELECT to_doc_element(struct_pack(
    element_type := type_column,
    content := text_column
))
FROM my_table;
```

### Validation Pipeline

```sql
-- Filter to only valid elements
SELECT *
FROM my_blocks
WHERE doc_element_valid(block);
```

### Transforming Elements

```sql
-- Update all element orders
SELECT doc_element_set_order(block, row_number() OVER () - 1)
FROM my_blocks;
```

### Using with Aggregations

```sql
-- Count by element type
SELECT doc_element_type(block), COUNT(*)
FROM UNNEST(my_blocks) AS t(block)
GROUP BY doc_element_type(block);
```

### Working with Both Blocks and Inlines

```sql
-- Separate blocks from inlines
SELECT *
FROM UNNEST(my_elements) AS t(elem)
WHERE doc_element_kind(elem) = 'block';

SELECT *
FROM UNNEST(my_elements) AS t(elem)
WHERE doc_element_kind(elem) = 'inline';
```
