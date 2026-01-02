# Block Builder Functions

Declarative functions for constructing duck_block structures. These enable:

1. **Programmatic document generation** from query results
2. **Nested/hierarchical authoring** that flattens automatically
3. **Template-based document construction**

## Design Philosophy

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Nested/Declarative Construction          Flat duck_blocks Output        │
│                                                                         │
│  db_document(                            element_type │ content │ level  │
│    db_heading('Title', 1),                heading   │ Title   │ 1      │
│    db_section('Intro', 2,                 heading   │ Intro   │ 2      │
│      db_paragraph('Hello'),               paragraph │ Hello   │ NULL   │
│      db_code('x=1', 'py')                 code      │ x=1     │ NULL   │
│    )                                                                    │
│  )                                                                      │
└─────────────────────────────────────────────────────────────────────────┘
```

## Atomic Constructors

These return a single `duck_block` struct.

### db_heading

```sql
db_heading(
    content VARCHAR,
    level INTEGER DEFAULT 1,
    id VARCHAR DEFAULT NULL
) → duck_block
```

**Example:**
```sql
SELECT db_heading('Introduction', 2, 'intro');
-- Returns: {element_type: 'heading', content: 'Introduction', level: NULL,
--           encoding: 'text', attributes: {'heading_level': '2', 'id': 'intro'}, element_order: 0}

-- Access heading level:
SELECT db_heading('Title', 1).attributes['heading_level'];
-- Returns: '1'
```

**Note:** The heading level is stored in `attributes['heading_level']` as a string, not in the `level` field. This separates semantic heading levels (h1-h6) from structural nesting depth used by other elements.

### db_paragraph

```sql
db_paragraph(
    content VARCHAR
) → duck_block
```

**Example:**
```sql
SELECT db_paragraph('This is some **markdown** text.');
```

### db_code

```sql
db_code(
    content VARCHAR,
    language VARCHAR DEFAULT NULL
) → duck_block
```

**Example:**
```sql
SELECT db_code('def hello():\n    print("hi")', 'python');
```

### duck_blockquote

```sql
duck_blockquote(
    content VARCHAR,
    level INTEGER DEFAULT 1
) → duck_block
```

### db_list_block

```sql
db_list_block(
    items VARCHAR[],
    ordered BOOLEAN DEFAULT false,
    start INTEGER DEFAULT 1
) → duck_block
```

**Example:**
```sql
SELECT db_list_block(['First', 'Second', 'Third'], ordered := true);
-- Returns: {element_type: 'list', content: '["First","Second","Third"]',
--           level: 1, encoding: 'json', attributes: {'ordered': 'true', 'start': '1'}}
```

### db_table_block

```sql
db_table_block(
    headers VARCHAR[],
    rows VARCHAR[][]
) → duck_block
```

**Example:**
```sql
SELECT db_table_block(
    ['Name', 'Age'],
    [['Alice', '30'], ['Bob', '25']]
);
```

### db_hr

```sql
db_hr() → duck_block
```

### db_metadata

```sql
db_metadata(
    yaml_content VARCHAR
) → duck_block

-- Or from key-value pairs
db_metadata_map(
    meta MAP(VARCHAR, VARCHAR)
) → duck_block
```

**Example:**
```sql
SELECT db_metadata('title: My Document\nauthor: Jane Doe');

SELECT db_metadata_map(MAP{'title': 'My Document', 'author': 'Jane Doe'});
```

### db_image

```sql
db_image(
    src VARCHAR,
    alt VARCHAR DEFAULT '',
    title VARCHAR DEFAULT NULL
) → duck_block
```

### db_raw

```sql
db_raw(
    content VARCHAR,
    format VARCHAR DEFAULT 'html'
) → duck_block
```

---

## Nested Constructors

These accept children and flatten automatically.

### db_section

Create a section with a heading and nested content.

```sql
db_section(
    title VARCHAR,
    level INTEGER,
    children VARIADIC duck_block_or_list...
) → LIST(duck_block)
```

**Example:**
```sql
SELECT db_section('Getting Started', 2,
    db_paragraph('Welcome to the guide.'),
    db_code('npm install mypackage', 'bash'),
    db_paragraph('Now you are ready.')
);
-- Returns LIST with 4 blocks:
-- 1. heading 'Getting Started' level 2
-- 2. paragraph 'Welcome...'
-- 3. code 'npm install...'
-- 4. paragraph 'Now you...'
```

### db_document

Assemble a complete document with automatic element_order assignment.

```sql
db_document(
    children VARIADIC duck_block_or_list...
) → LIST(duck_block)
```

**Example:**
```sql
SELECT db_document(
    db_metadata('title: User Guide'),
    db_heading('Introduction', 1),
    db_paragraph('This guide covers...'),
    db_section('Installation', 2,
        db_paragraph('First, install dependencies:'),
        db_code('pip install -r requirements.txt', 'bash')
    ),
    db_section('Usage', 2,
        db_paragraph('Import and use:'),
        db_code('import mylib', 'python')
    )
);
```

### db_nested_list

Create nested list structures.

```sql
db_nested_list(
    items LIST(STRUCT(text VARCHAR, children LIST(...))),
    ordered BOOLEAN DEFAULT false
) → duck_block
```

**Example:**
```sql
SELECT db_nested_list([
    {'text': 'Chapter 1', 'children': ['Section 1.1', 'Section 1.2']},
    {'text': 'Chapter 2', 'children': ['Section 2.1']}
]);
```

---

## Query Transformers

Transform SQL query results into document blocks.

### db_table_from_query

Convert any query result to a table block.

```sql
db_table_from_query(
    query_result ANY,
    include_types BOOLEAN DEFAULT false
) → duck_block
```

**Example:**
```sql
-- Direct from subquery
SELECT db_table_from_query(
    (SELECT name, age, city FROM users WHERE active)
);

-- From CTE
WITH summary AS (
    SELECT department, count(*) as count, avg(salary) as avg_salary
    FROM employees GROUP BY department
)
SELECT db_table_from_query((SELECT * FROM summary));
```

### db_list_from_column

Convert a column to a list block.

```sql
db_list_from_column(
    values ANY[],
    ordered BOOLEAN DEFAULT false,
    format_template VARCHAR DEFAULT NULL
) → duck_block
```

**Example:**
```sql
-- Simple list from column
SELECT db_list_from_column(
    (SELECT array_agg(name) FROM products)
);

-- Formatted list
SELECT db_list_from_column(
    (SELECT array_agg(name || ' - $' || price) FROM products),
    ordered := true
);
```

### db_heading_from_value

Create heading from a value (useful in row-by-row transforms).

```sql
db_heading_from_value(
    value ANY,
    level INTEGER DEFAULT 2,
    format_template VARCHAR DEFAULT '{}'
) → duck_block
```

**Example:**
```sql
SELECT db_heading_from_value(department_name, 2) FROM departments;
```

### db_summary_stats

Generate a summary section from numeric columns.

```sql
db_summary_stats(
    query_result ANY,
    title VARCHAR DEFAULT 'Summary Statistics'
) → LIST(duck_block)
```

**Example:**
```sql
SELECT db_summary_stats(
    (SELECT price, quantity, total FROM orders),
    'Order Statistics'
);
-- Returns heading + table with min, max, avg, count for each numeric column
```

---

## Row-wise Document Generation

Generate document sections from each row of a query.

### db_from_rows

Transform each row into document blocks using a template function.

```sql
db_from_rows(
    query_result ANY,
    row_transformer LAMBDA
) → LIST(duck_block)
```

**Example:**
```sql
-- Generate a section for each product
SELECT db_from_rows(
    (SELECT name, description, price, features FROM products),
    row -> db_section(row.name, 2,
        db_paragraph(row.description),
        db_paragraph('**Price:** $' || row.price),
        db_list_from_column(row.features)
    )
);
```

### db_grouped_sections

Group rows and create sections per group.

```sql
db_grouped_sections(
    query_result ANY,
    group_column VARCHAR,
    group_level INTEGER DEFAULT 2,
    row_transformer LAMBDA
) → LIST(duck_block)
```

**Example:**
```sql
-- Group employees by department
SELECT db_grouped_sections(
    (SELECT * FROM employees ORDER BY department, name),
    'department',
    2,  -- h2 for department names
    row -> db_paragraph('- ' || row.name || ' (' || row.title || ')')
);
-- Output:
-- ## Engineering
-- - Alice (Senior Dev)
-- - Bob (Dev)
-- ## Marketing
-- - Carol (Manager)
```

---

## Assembly & Utilities

### db_assemble

Combine multiple block sources and assign sequential element_order.

```sql
db_assemble(
    parts VARIADIC (duck_block | LIST(duck_block))...
) → LIST(duck_block)
```

**Example:**
```sql
SELECT db_assemble(
    -- Static header
    db_metadata('title: Monthly Report'),
    db_heading('Monthly Report', 1),

    -- Dynamic content from query
    db_section('Sales Summary', 2,
        db_table_from_query((SELECT * FROM monthly_sales))
    ),

    -- More static content
    db_section('Notes', 2,
        db_paragraph('Data as of ' || current_date)
    )
);
```

### db_concat

Concatenate multiple block lists.

```sql
db_concat(
    lists VARIADIC LIST(duck_block)...
) → LIST(duck_block)
```

### db_rebase_levels

Adjust all heading levels by an offset.

```sql
db_rebase_levels(
    blocks LIST(duck_block),
    offset INTEGER
) → LIST(duck_block)
```

**Note:** This modifies `attributes['heading_level']`, not the `level` field. For backward compatibility, it reads from `attributes['heading_level']` first, falling back to the `level` field if not present.

**Example:**
```sql
-- Include a subdocument with adjusted heading levels
SELECT db_assemble(
    db_heading('Main Document', 1),
    db_rebase_levels(
        (SELECT list(b) FROM read_markdown_blocks('chapter.md') b),
        1  -- h1 becomes h2, h2 becomes h3, etc.
    )
);

-- Check rebased level
SELECT db_rebase_levels([db_heading('Title', 1)], 1)[1].attributes['heading_level'];
-- Returns: '2'
```

### db_with_toc

Insert a table of contents based on headings.

```sql
db_with_toc(
    blocks LIST(duck_block),
    toc_title VARCHAR DEFAULT 'Table of Contents',
    min_level INTEGER DEFAULT 1,
    max_level INTEGER DEFAULT 3,
    position INTEGER DEFAULT 1  -- Insert after block N (0 = beginning)
) → LIST(duck_block)
```

**Example:**
```sql
SELECT db_with_toc(
    db_document(
        db_heading('My Guide', 1),
        db_section('Chapter 1', 2, ...),
        db_section('Chapter 2', 2, ...)
    ),
    'Contents',
    2, 3,  -- Include h2 and h3 in TOC
    1      -- After the h1
);
```

---

## Complete Examples

### Report Generation

```sql
-- Generate a complete report from database queries
COPY (
    SELECT unnest(db_assemble(
        -- Frontmatter
        db_metadata_map(MAP{
            'title': 'Q4 Sales Report',
            'author': 'Analytics Team',
            'date': current_date::VARCHAR
        }),

        -- Title
        db_heading('Q4 2024 Sales Report', 1),
        db_paragraph('Generated on ' || current_date),

        -- Executive Summary
        db_section('Executive Summary', 2,
            db_paragraph((
                SELECT 'Total revenue: $' || sum(revenue)::VARCHAR ||
                       '. Units sold: ' || sum(units)::VARCHAR
                FROM quarterly_sales WHERE quarter = 'Q4'
            ))
        ),

        -- Sales by Region
        db_section('Sales by Region', 2,
            db_table_from_query((
                SELECT region, sum(revenue) as revenue, sum(units) as units
                FROM quarterly_sales WHERE quarter = 'Q4'
                GROUP BY region ORDER BY revenue DESC
            ))
        ),

        -- Top Products
        db_section('Top 10 Products', 2,
            db_table_from_query((
                SELECT product_name, units_sold, revenue
                FROM product_sales
                ORDER BY revenue DESC LIMIT 10
            ))
        ),

        -- Per-region breakdown
        db_grouped_sections(
            (SELECT * FROM regional_details WHERE quarter = 'Q4'),
            'region',
            2,
            row -> db_assemble(
                db_paragraph('Manager: ' || row.manager),
                db_paragraph('YoY Growth: ' || row.growth || '%')
            )
        )
    ))
) TO 'q4_report.md' (FORMAT MARKDOWN, markdown_mode 'blocks');
```

### API Documentation Generator

```sql
-- Generate API docs from a schema table
WITH endpoints AS (
    SELECT * FROM api_endpoints ORDER BY path
)
SELECT unnest(db_assemble(
    db_metadata('title: API Reference'),
    db_heading('API Reference', 1),
    db_paragraph('Auto-generated from schema.'),

    db_from_rows(
        (SELECT * FROM endpoints),
        ep -> db_section(ep.method || ' ' || ep.path, 2,
            db_paragraph(ep.description),
            db_heading('Parameters', 3),
            db_table_from_query((
                SELECT name, type, required, description
                FROM api_parameters WHERE endpoint_id = ep.id
            )),
            db_heading('Response', 3),
            db_code(ep.example_response, 'json')
        )
    )
));
```

### Data Dictionary

```sql
-- Generate data dictionary from information_schema
SELECT unnest(db_assemble(
    db_heading('Data Dictionary', 1),
    db_paragraph('Database schema documentation.'),

    db_grouped_sections(
        (SELECT table_name, column_name, data_type, is_nullable
         FROM information_schema.columns
         WHERE table_schema = 'main'
         ORDER BY table_name, ordinal_position),
        'table_name',
        2,
        row -> db_paragraph('- **' || row.column_name || '**: ' ||
                             row.data_type ||
                             CASE WHEN row.is_nullable = 'NO' THEN ' (required)' ELSE '' END)
    )
));
```

---

## Type Signatures

### Intermediate Types

```sql
-- Block or list of blocks (for variadic functions)
CREATE TYPE duck_block_or_list AS UNION(
    single duck_block,
    multiple LIST(duck_block)
);

-- Row transformer function type
CREATE TYPE doc_row_transformer AS LAMBDA(row STRUCT(...)) → duck_block_or_list;
```

### Flattening Rules

When assembling blocks:

1. **Single duck_block**: Added directly to output
2. **LIST(duck_block)**: Each element added in order
3. **Nested db_section**: Heading added, then children flattened recursively
4. **NULL values**: Skipped silently
5. **element_order**: Assigned sequentially (0, 1, 2, ...) during final assembly

---

## Implementation Notes

### Core Flattening Function

```cpp
vector<DocBlock> FlattenBlocks(const vector<DocBlockOrList>& inputs) {
    vector<DocBlock> result;
    int order = 0;

    function<void(const DocBlockOrList&)> flatten = [&](const DocBlockOrList& item) {
        if (holds_alternative<DocBlock>(item)) {
            DocBlock block = get<DocBlock>(item);
            block.element_order = order++;
            result.push_back(block);
        } else if (holds_alternative<vector<DocBlockOrList>>(item)) {
            for (const auto& child : get<vector<DocBlockOrList>>(item)) {
                flatten(child);
            }
        }
    };

    for (const auto& input : inputs) {
        flatten(input);
    }

    return result;
}
```

### Section Implementation

```cpp
ListValue DocSection(string title, int level, vector<Value> children) {
    vector<Value> result;

    // Add heading
    result.push_back(CreateHeadingBlock(title, level));

    // Flatten and add children
    for (const auto& child : children) {
        if (child.type() == DocBlockType()) {
            result.push_back(child);
        } else if (child.type() == ListType(DocBlockType())) {
            auto list = ListValue::GetChildren(child);
            result.insert(result.end(), list.begin(), list.end());
        }
    }

    return ListValue::Create(result);
}
```

---

## See Also

- [API Reference](api.md) - All function signatures
- [Design Document](design.md) - Architecture overview
- [Document Block Spec](https://github.com/teaguesterling/duckdb_markdown/blob/main/docs/duck_block_spec.md) - Block schema
