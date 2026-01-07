# Duck Block Test Fixtures

This directory contains test fixtures for validating `duck_block` implementations across the DuckDB document ecosystem.

## Purpose

These fixtures provide canonical test cases that extensions implementing `duck_block` support can use to verify their implementations:

- **duckdb_markdown** - Markdown reading/writing with duck_block
- **duckdb_webbed** - HTML generation from duck_block
- **Other extensions** - Any extension working with the duck_block type

## Directory Structure

```
test_fixtures/
├── sql/
│   ├── 01_block_types.sql      # Individual block type fixtures
│   ├── 02_lists.sql            # List structures (nested, ordered, etc.)
│   ├── 03_documents.sql        # Complete multi-block documents
│   ├── 04_pandoc_expectations.sql  # Expected Pandoc JSON output
│   ├── load_fixtures.sql       # Master loader script
│   └── run_tests.sql           # Example test runner
└── README.md
```

## Fixture Tables

After running `load_fixtures.sql`, you'll have access to:

| Table | Description |
|-------|-------------|
| `block_type_fixtures` | Individual block types (headings, paragraphs, code, etc.) |
| `list_fixtures` | List structures including nested lists |
| `document_fixtures` | Complete multi-block documents |
| `pandoc_block_expectations` | Expected Pandoc JSON for block elements |
| `pandoc_inline_expectations` | Expected Pandoc JSON for inline elements |
| `pandoc_list_expectations` | Expected Pandoc JSON for list structures |
| `all_fixtures` | Unified view of all fixtures with categories |

## Schema

Each fixture table has this structure:

```sql
name VARCHAR,              -- Unique test case identifier
blocks LIST(duck_block),   -- The duck_block structure to test
expected_markdown VARCHAR, -- Expected markdown output (NULL if N/A)
expected_html VARCHAR      -- Expected HTML output (NULL if N/A)
```

Pandoc expectation tables use:

```sql
name VARCHAR,              -- Test case identifier
blocks LIST(duck_block),   -- The duck_block structure
expected_pandoc_json VARCHAR -- Expected Pandoc JSON AST fragment
```

## Usage

### Loading Fixtures

```sql
-- Load the duck_block_utils extension
LOAD duck_block_utils;

-- Load all fixtures (from the sql/ directory)
.read load_fixtures.sql

-- Or load individual fixture files
.read 01_block_types.sql
```

### Testing Your Implementation

```sql
-- Test markdown output (replace with your function)
SELECT
    name,
    CASE WHEN your_markdown_function(blocks) = expected_markdown
         THEN 'PASS' ELSE 'FAIL' END as result
FROM block_type_fixtures
WHERE expected_markdown IS NOT NULL;

-- Test HTML output
SELECT
    name,
    CASE WHEN your_html_function(blocks) = expected_html
         THEN 'PASS' ELSE 'FAIL' END as result
FROM block_type_fixtures
WHERE expected_html IS NOT NULL;

-- Test Pandoc JSON output
SELECT
    name,
    CASE WHEN duck_blocks_to_pandoc_ast(blocks) LIKE '%' || expected_pandoc_json || '%'
         THEN 'PASS' ELSE 'FAIL' END as result
FROM pandoc_block_expectations;
```

### Browsing Fixtures

```sql
-- See all available test cases
SELECT category, name FROM all_fixtures ORDER BY category, name;

-- Examine a specific fixture
SELECT * FROM block_type_fixtures WHERE name = 'paragraph_with_bold';

-- See all list test cases
SELECT name, expected_markdown FROM list_fixtures;
```

## Test Categories

### Block Types (`01_block_types.sql`)

- `heading_h1`, `heading_h2`, `heading_h3` - Different heading levels
- `paragraph_simple`, `paragraph_with_bold`, etc. - Paragraphs with formatting
- `code_python`, `code_sql`, `code_no_language` - Fenced code blocks
- `blockquote_simple`, `blockquote_with_formatting` - Block quotes
- `hr` - Horizontal rules
- `image_simple`, `image_with_title` - Images
- `raw_html`, `raw_latex` - Raw content blocks

### Lists (`02_lists.sql`)

- `bullet_list_simple` - Basic bullet list
- `ordered_list_simple` - Basic numbered list
- `bullet_list_formatted` - List items with inline formatting
- `bullet_list_nested` - Nested bullet lists
- `mixed_nested_list` - Ordered with unordered children
- `deeply_nested_list` - Multiple nesting levels

### Documents (`03_documents.sql`)

- `doc_heading_paragraph` - Simple heading + paragraph
- `doc_heading_para_code` - With code block
- `doc_quote_attribution` - Blockquote with attribution
- `doc_heading_hierarchy` - Multiple heading levels
- `doc_readme_style` - Full README-like document
- `doc_mixed_elements` - All element types combined

## Contributing

When adding new fixtures:

1. Add test cases to the appropriate fixture file
2. Include both `expected_markdown` and `expected_html` where applicable
3. Use `NULL` for expectations that don't apply (e.g., LaTeX raw blocks have no HTML equivalent)
4. Use descriptive names: `{category}_{variant}` (e.g., `heading_with_link`)
5. Test the fixtures load correctly: `duckdb < load_fixtures.sql`

## Version Compatibility

These fixtures are designed for duck_block_utils v1.2.0 and later. The duck_block type schema:

```sql
STRUCT(
    kind VARCHAR,
    element_type VARCHAR,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR,
    attributes MAP(VARCHAR, VARCHAR),
    element_order INTEGER
)
```
