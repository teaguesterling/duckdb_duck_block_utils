# Type Functions

Standard type functions for the `duck_block` type, designed for integration with other DuckDB extensions.

## The Unified duck_block Type

Both block-level and inline elements use the same type, distinguished by the `kind` field:

```sql
STRUCT(
    kind VARCHAR,                       -- 'block', 'inline' or 'value'
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

Creates a `duck_block` with kind='block' and all fields specified.

```sql
duck_block(
    element_type VARCHAR,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR,
    attributes MAP(VARCHAR, VARCHAR),
    element_order INTEGER
) → duck_block
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

Creates a `duck_block` with kind='block' and minimal fields, using defaults for the rest.

```sql
duck_block(element_type VARCHAR, content VARCHAR) → duck_block
```

**Defaults:**
- `kind`: 'block'
- `level`: 1 (top level — `level` is never NULL)
- `encoding`: 'text'
- `attributes`: empty map
- `element_order`: 0

**Example:**
```sql
SELECT duck_block('paragraph', 'Hello world');
```

### duck_inline (Full Constructor)

Creates a `duck_block` with kind='inline' and all fields specified.

```sql
duck_inline(
    element_type VARCHAR,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR,
    attributes MAP(VARCHAR, VARCHAR),
    element_order INTEGER
) → duck_block
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

Creates a `duck_block` with kind='inline' and minimal fields.

```sql
duck_inline(element_type VARCHAR, content VARCHAR) → duck_block
```

**Defaults:**
- `kind`: 'inline'
- `level`: 1
- `encoding`: 'text'
- `attributes`: empty map
- `element_order`: 0

### to_duck_block

Converts a compatible struct to a `duck_block`.

```sql
to_duck_block(input STRUCT) → duck_block
```

**Notes:**
- Accepts structs with 2-7 fields
- Missing fields are filled with defaults
- Infers `kind` from context if not provided
- Useful for importing data from other sources

**Example:**
```sql
SELECT to_duck_block({'element_type': 'paragraph', 'content': 'Text'});
-- Returns: {kind: 'block', element_type: 'paragraph', content: 'Text', ...}
```

---

## Validation

### duck_block_valid

Checks if an element has valid structure and values.

```sql
duck_block_valid(elem duck_block) → BOOLEAN
```

**Validation Rules:**
- `kind` must be 'block', 'inline' or 'value' (the authoritative list is `duck_block_kind_names()`)
- `element_type` must be non-empty
- For blocks: `element_type` must be recognized (heading, paragraph, code, etc.)
- For inlines: `element_type` must be recognized (text, link, bold, etc.)
- `encoding` (if not NULL) must be one of: text, json, yaml, html, xml
- `element_order` (if not NULL) must be non-negative
- For headings: `level` must be 1-6
- For inlines: `level` must be >= 1

**Example:**
```sql
SELECT duck_block_valid(duck_block_heading('Title', 1));
-- Returns: true

SELECT duck_block_valid(duck_block('invalid_type', 'content'));
-- Returns: false
```

### duck_block_valid

Alias for `duck_block_valid` for blocks.

```sql
duck_block_valid(block duck_block) → BOOLEAN
```

---

## Field Accessors

These functions extract individual fields from a `duck_block`. They're useful when you can't use the `.field` syntax (e.g., in certain aggregation contexts).

### duck_block_kind

```sql
duck_block_kind(elem duck_block) → VARCHAR
```

Returns the `kind` field ('block', 'inline' or 'value').

### duck_block_type

```sql
duck_block_type(elem duck_block) → VARCHAR
```

Returns the `element_type` field.

### duck_block_type

Alias for `duck_block_type`.

```sql
duck_block_type(block duck_block) → VARCHAR
```

### duck_block_content

```sql
duck_block_content(elem duck_block) → VARCHAR
```

Returns the `content` field.

### duck_block_content

Alias for `duck_block_content`.

```sql
duck_block_content(block duck_block) → VARCHAR
```

### duck_block_level

```sql
duck_block_level(elem duck_block) → INTEGER
```

Returns the `level` field: structural depth, always >= 1 for every kind. It is
never NULL — `duck_blocks_validate()` rejects a NULL level outright.

### duck_block_level

Alias for `duck_block_level`.

```sql
duck_block_level(block duck_block) → INTEGER
```

### duck_block_encoding

```sql
duck_block_encoding(elem duck_block) → VARCHAR
```

Returns the `encoding` field.

### duck_block_encoding

Alias for `duck_block_encoding`.

```sql
duck_block_encoding(block duck_block) → VARCHAR
```

### duck_block_order

```sql
duck_block_order(elem duck_block) → INTEGER
```

Returns the `element_order` field.

### duck_block_order

Alias for `duck_block_order`.

```sql
duck_block_order(block duck_block) → INTEGER
```

### duck_block_attr

```sql
duck_block_attr(elem duck_block, key VARCHAR) → VARCHAR
```

Returns the value of an attribute, or NULL if not found.

### duck_block_attr

Alias for `duck_block_attr`.

```sql
duck_block_attr(block duck_block, key VARCHAR) → VARCHAR
```

**Example:**
```sql
SELECT duck_block_attr(duck_block_code('x=1', 'python'), 'language');
-- Returns: 'python'

SELECT duck_block_attr(duck_block_link('Click', 'https://example.com'), 'href');
-- Returns: 'https://example.com'
```

---

## Field Setters

These functions return a new `duck_block` with one field modified. The original element is unchanged (immutable update pattern).

### duck_block_set_order

```sql
duck_block_set_order(elem duck_block, new_order INTEGER) → duck_block
```

Returns a new element with updated `element_order`.

### duck_block_set_order

Alias for `duck_block_set_order`.

```sql
duck_block_set_order(block duck_block, new_order INTEGER) → duck_block
```

**Example:**
```sql
SELECT duck_block_set_order(duck_block_heading('Title', 1), 42);
-- Returns heading with element_order=42
```

### duck_block_set_content

```sql
duck_block_set_content(elem duck_block, new_content VARCHAR) → duck_block
```

Returns a new element with updated `content`.

### duck_block_set_content

Alias for `duck_block_set_content`.

```sql
duck_block_set_content(block duck_block, new_content VARCHAR) → duck_block
```

**Example:**
```sql
SELECT duck_block_set_content(duck_block_heading('Old', 1), 'New');
-- Returns heading with content='New'
```

### duck_block_set_level

```sql
duck_block_set_level(elem duck_block, new_level INTEGER) → duck_block
```

Returns a new element with updated `level`.

### duck_block_set_level

Alias for `duck_block_set_level`.

```sql
duck_block_set_level(block duck_block, new_level INTEGER) → duck_block
```

**Example:**
```sql
SELECT duck_block_set_level(duck_block_heading('Title', 1), 2);
-- Returns heading with level=2
```

---

## Integration Patterns

### Converting External Data

```sql
-- From a table with document content
SELECT to_duck_block(struct_pack(
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
WHERE duck_block_valid(block);
```

### Transforming Elements

```sql
-- Update all element orders
SELECT duck_block_set_order(block, row_number() OVER () - 1)
FROM my_blocks;
```

### Using with Aggregations

```sql
-- Count by element type
SELECT duck_block_type(block), COUNT(*)
FROM UNNEST(my_blocks) AS t(block)
GROUP BY duck_block_type(block);
```

### Working with Both Blocks and Inlines

```sql
-- Separate blocks from inlines
SELECT *
FROM UNNEST(my_elements) AS t(elem)
WHERE duck_block_kind(elem) = 'block';

SELECT *
FROM UNNEST(my_elements) AS t(elem)
WHERE duck_block_kind(elem) = 'inline';
```
