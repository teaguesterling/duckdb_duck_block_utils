# Block Builder Functions

Declarative functions for constructing doc_element structures. These enable:

1. **Programmatic document generation** from query results
2. **Nested/hierarchical authoring** that flattens automatically
3. **Template-based document construction**

## Design Philosophy

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Nested/Declarative Construction          Flat doc_elements Output        │
│                                                                         │
│  doc_document(                            element_type │ content │ level  │
│    doc_heading('Title', 1),                heading   │ Title   │ 1      │
│    doc_section('Intro', 2,                 heading   │ Intro   │ 2      │
│      doc_paragraph('Hello'),               paragraph │ Hello   │ NULL   │
│      doc_code('x=1', 'py')                 code      │ x=1     │ NULL   │
│    )                                                                    │
│  )                                                                      │
└─────────────────────────────────────────────────────────────────────────┘
```

## Atomic Constructors

These return a single `doc_element` struct.

### doc_heading

```sql
doc_heading(
    content VARCHAR,
    level INTEGER DEFAULT 1,
    id VARCHAR DEFAULT NULL
) → doc_element
```

**Example:**
```sql
SELECT doc_heading('Introduction', 2, 'intro');
-- Returns: {element_type: 'heading', content: 'Introduction', level: 2,
--           encoding: 'text', attributes: {'id': 'intro'}, element_order: 0}
```

### doc_paragraph

```sql
doc_paragraph(
    content VARCHAR
) → doc_element
```

**Example:**
```sql
SELECT doc_paragraph('This is some **markdown** text.');
```

### doc_code

```sql
doc_code(
    content VARCHAR,
    language VARCHAR DEFAULT NULL
) → doc_element
```

**Example:**
```sql
SELECT doc_code('def hello():\n    print("hi")', 'python');
```

### doc_elementquote

```sql
doc_elementquote(
    content VARCHAR,
    level INTEGER DEFAULT 1
) → doc_element
```

### doc_list_block

```sql
doc_list_block(
    items VARCHAR[],
    ordered BOOLEAN DEFAULT false,
    start INTEGER DEFAULT 1
) → doc_element
```

**Example:**
```sql
SELECT doc_list_block(['First', 'Second', 'Third'], ordered := true);
-- Returns: {element_type: 'list', content: '["First","Second","Third"]',
--           level: 1, encoding: 'json', attributes: {'ordered': 'true', 'start': '1'}}
```

### doc_table_block

```sql
doc_table_block(
    headers VARCHAR[],
    rows VARCHAR[][]
) → doc_element
```

**Example:**
```sql
SELECT doc_table_block(
    ['Name', 'Age'],
    [['Alice', '30'], ['Bob', '25']]
);
```

### doc_hr

```sql
doc_hr() → doc_element
```

### doc_metadata

```sql
doc_metadata(
    yaml_content VARCHAR
) → doc_element

-- Or from key-value pairs
doc_metadata_map(
    meta MAP(VARCHAR, VARCHAR)
) → doc_element
```

**Example:**
```sql
SELECT doc_metadata('title: My Document\nauthor: Jane Doe');

SELECT doc_metadata_map(MAP{'title': 'My Document', 'author': 'Jane Doe'});
```

### doc_image

```sql
doc_image(
    src VARCHAR,
    alt VARCHAR DEFAULT '',
    title VARCHAR DEFAULT NULL
) → doc_element
```

### doc_raw

```sql
doc_raw(
    content VARCHAR,
    format VARCHAR DEFAULT 'html'
) → doc_element
```

---

## Nested Constructors

These accept children and flatten automatically.

### doc_section

Create a section with a heading and nested content.

```sql
doc_section(
    title VARCHAR,
    level INTEGER,
    children VARIADIC doc_element_or_list...
) → LIST(doc_element)
```

**Example:**
```sql
SELECT doc_section('Getting Started', 2,
    doc_paragraph('Welcome to the guide.'),
    doc_code('npm install mypackage', 'bash'),
    doc_paragraph('Now you are ready.')
);
-- Returns LIST with 4 blocks:
-- 1. heading 'Getting Started' level 2
-- 2. paragraph 'Welcome...'
-- 3. code 'npm install...'
-- 4. paragraph 'Now you...'
```

### doc_document

Assemble a complete document with automatic element_order assignment.

```sql
doc_document(
    children VARIADIC doc_element_or_list...
) → LIST(doc_element)
```

**Example:**
```sql
SELECT doc_document(
    doc_metadata('title: User Guide'),
    doc_heading('Introduction', 1),
    doc_paragraph('This guide covers...'),
    doc_section('Installation', 2,
        doc_paragraph('First, install dependencies:'),
        doc_code('pip install -r requirements.txt', 'bash')
    ),
    doc_section('Usage', 2,
        doc_paragraph('Import and use:'),
        doc_code('import mylib', 'python')
    )
);
```

### doc_nested_list

Create nested list structures.

```sql
doc_nested_list(
    items LIST(STRUCT(text VARCHAR, children LIST(...))),
    ordered BOOLEAN DEFAULT false
) → doc_element
```

**Example:**
```sql
SELECT doc_nested_list([
    {'text': 'Chapter 1', 'children': ['Section 1.1', 'Section 1.2']},
    {'text': 'Chapter 2', 'children': ['Section 2.1']}
]);
```

---

## Query Transformers

Transform SQL query results into document blocks.

### doc_table_from_query

Convert any query result to a table block.

```sql
doc_table_from_query(
    query_result ANY,
    include_types BOOLEAN DEFAULT false
) → doc_element
```

**Example:**
```sql
-- Direct from subquery
SELECT doc_table_from_query(
    (SELECT name, age, city FROM users WHERE active)
);

-- From CTE
WITH summary AS (
    SELECT department, count(*) as count, avg(salary) as avg_salary
    FROM employees GROUP BY department
)
SELECT doc_table_from_query((SELECT * FROM summary));
```

### doc_list_from_column

Convert a column to a list block.

```sql
doc_list_from_column(
    values ANY[],
    ordered BOOLEAN DEFAULT false,
    format_template VARCHAR DEFAULT NULL
) → doc_element
```

**Example:**
```sql
-- Simple list from column
SELECT doc_list_from_column(
    (SELECT array_agg(name) FROM products)
);

-- Formatted list
SELECT doc_list_from_column(
    (SELECT array_agg(name || ' - $' || price) FROM products),
    ordered := true
);
```

### doc_heading_from_value

Create heading from a value (useful in row-by-row transforms).

```sql
doc_heading_from_value(
    value ANY,
    level INTEGER DEFAULT 2,
    format_template VARCHAR DEFAULT '{}'
) → doc_element
```

**Example:**
```sql
SELECT doc_heading_from_value(department_name, 2) FROM departments;
```

### doc_summary_stats

Generate a summary section from numeric columns.

```sql
doc_summary_stats(
    query_result ANY,
    title VARCHAR DEFAULT 'Summary Statistics'
) → LIST(doc_element)
```

**Example:**
```sql
SELECT doc_summary_stats(
    (SELECT price, quantity, total FROM orders),
    'Order Statistics'
);
-- Returns heading + table with min, max, avg, count for each numeric column
```

---

## Row-wise Document Generation

Generate document sections from each row of a query.

### doc_from_rows

Transform each row into document blocks using a template function.

```sql
doc_from_rows(
    query_result ANY,
    row_transformer LAMBDA
) → LIST(doc_element)
```

**Example:**
```sql
-- Generate a section for each product
SELECT doc_from_rows(
    (SELECT name, description, price, features FROM products),
    row -> doc_section(row.name, 2,
        doc_paragraph(row.description),
        doc_paragraph('**Price:** $' || row.price),
        doc_list_from_column(row.features)
    )
);
```

### doc_grouped_sections

Group rows and create sections per group.

```sql
doc_grouped_sections(
    query_result ANY,
    group_column VARCHAR,
    group_level INTEGER DEFAULT 2,
    row_transformer LAMBDA
) → LIST(doc_element)
```

**Example:**
```sql
-- Group employees by department
SELECT doc_grouped_sections(
    (SELECT * FROM employees ORDER BY department, name),
    'department',
    2,  -- h2 for department names
    row -> doc_paragraph('- ' || row.name || ' (' || row.title || ')')
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

### doc_assemble

Combine multiple block sources and assign sequential element_order.

```sql
doc_assemble(
    parts VARIADIC (doc_element | LIST(doc_element))...
) → LIST(doc_element)
```

**Example:**
```sql
SELECT doc_assemble(
    -- Static header
    doc_metadata('title: Monthly Report'),
    doc_heading('Monthly Report', 1),

    -- Dynamic content from query
    doc_section('Sales Summary', 2,
        doc_table_from_query((SELECT * FROM monthly_sales))
    ),

    -- More static content
    doc_section('Notes', 2,
        doc_paragraph('Data as of ' || current_date)
    )
);
```

### doc_concat

Concatenate multiple block lists.

```sql
doc_concat(
    lists VARIADIC LIST(doc_element)...
) → LIST(doc_element)
```

### doc_rebase_levels

Adjust all heading levels by an offset.

```sql
doc_rebase_levels(
    blocks LIST(doc_element),
    offset INTEGER
) → LIST(doc_element)
```

**Example:**
```sql
-- Include a subdocument with adjusted heading levels
SELECT doc_assemble(
    doc_heading('Main Document', 1),
    doc_rebase_levels(
        (SELECT list(b) FROM read_markdown_blocks('chapter.md') b),
        1  -- h1 becomes h2, h2 becomes h3, etc.
    )
);
```

### doc_with_toc

Insert a table of contents based on headings.

```sql
doc_with_toc(
    blocks LIST(doc_element),
    toc_title VARCHAR DEFAULT 'Table of Contents',
    min_level INTEGER DEFAULT 1,
    max_level INTEGER DEFAULT 3,
    position INTEGER DEFAULT 1  -- Insert after block N (0 = beginning)
) → LIST(doc_element)
```

**Example:**
```sql
SELECT doc_with_toc(
    doc_document(
        doc_heading('My Guide', 1),
        doc_section('Chapter 1', 2, ...),
        doc_section('Chapter 2', 2, ...)
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
    SELECT unnest(doc_assemble(
        -- Frontmatter
        doc_metadata_map(MAP{
            'title': 'Q4 Sales Report',
            'author': 'Analytics Team',
            'date': current_date::VARCHAR
        }),

        -- Title
        doc_heading('Q4 2024 Sales Report', 1),
        doc_paragraph('Generated on ' || current_date),

        -- Executive Summary
        doc_section('Executive Summary', 2,
            doc_paragraph((
                SELECT 'Total revenue: $' || sum(revenue)::VARCHAR ||
                       '. Units sold: ' || sum(units)::VARCHAR
                FROM quarterly_sales WHERE quarter = 'Q4'
            ))
        ),

        -- Sales by Region
        doc_section('Sales by Region', 2,
            doc_table_from_query((
                SELECT region, sum(revenue) as revenue, sum(units) as units
                FROM quarterly_sales WHERE quarter = 'Q4'
                GROUP BY region ORDER BY revenue DESC
            ))
        ),

        -- Top Products
        doc_section('Top 10 Products', 2,
            doc_table_from_query((
                SELECT product_name, units_sold, revenue
                FROM product_sales
                ORDER BY revenue DESC LIMIT 10
            ))
        ),

        -- Per-region breakdown
        doc_grouped_sections(
            (SELECT * FROM regional_details WHERE quarter = 'Q4'),
            'region',
            2,
            row -> doc_assemble(
                doc_paragraph('Manager: ' || row.manager),
                doc_paragraph('YoY Growth: ' || row.growth || '%')
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
SELECT unnest(doc_assemble(
    doc_metadata('title: API Reference'),
    doc_heading('API Reference', 1),
    doc_paragraph('Auto-generated from schema.'),

    doc_from_rows(
        (SELECT * FROM endpoints),
        ep -> doc_section(ep.method || ' ' || ep.path, 2,
            doc_paragraph(ep.description),
            doc_heading('Parameters', 3),
            doc_table_from_query((
                SELECT name, type, required, description
                FROM api_parameters WHERE endpoint_id = ep.id
            )),
            doc_heading('Response', 3),
            doc_code(ep.example_response, 'json')
        )
    )
));
```

### Data Dictionary

```sql
-- Generate data dictionary from information_schema
SELECT unnest(doc_assemble(
    doc_heading('Data Dictionary', 1),
    doc_paragraph('Database schema documentation.'),

    doc_grouped_sections(
        (SELECT table_name, column_name, data_type, is_nullable
         FROM information_schema.columns
         WHERE table_schema = 'main'
         ORDER BY table_name, ordinal_position),
        'table_name',
        2,
        row -> doc_paragraph('- **' || row.column_name || '**: ' ||
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
CREATE TYPE doc_element_or_list AS UNION(
    single doc_element,
    multiple LIST(doc_element)
);

-- Row transformer function type
CREATE TYPE doc_row_transformer AS LAMBDA(row STRUCT(...)) → doc_element_or_list;
```

### Flattening Rules

When assembling blocks:

1. **Single doc_element**: Added directly to output
2. **LIST(doc_element)**: Each element added in order
3. **Nested doc_section**: Heading added, then children flattened recursively
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
- [Document Block Spec](https://github.com/teaguesterling/duckdb_markdown/blob/main/docs/doc_element_spec.md) - Block schema
