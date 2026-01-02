# V4 Dashboard Pipeline

**Status:** Implemented

## Goal

Create a dashboard pipeline that uses duck_block_utils builders completely, without embedding markdown syntax in string templates.

## Architecture

V4 uses a 3-step pipeline with separate DuckDB binaries:

1. **Step 1 - yaml extension**: Load YAML data into tables
2. **Step 2 - duck_block_utils extension**: Create blocks and inline element arrays
3. **Step 3 - markdown extension**: Render blocks and inlines to markdown

## Key Design

- **Inline elements** (`doc_link`, `doc_text`, `doc_inline_image`, `doc_italic`) create structured content
- **Inline arrays** are stored alongside blocks in step 2
- **`doc_elements_to_md()`** renders both blocks and inline arrays in step 3
- **No markdown syntax** in SQL - all formatting is through structured elements

## Example

Instead of v3's embedded markdown templates:
```sql
-- v3: Markdown syntax in templates
SET VARIABLE link_github TO $$- [GitHub]({0}) • ![CI]({1})$$;
format(getvariable('link_github'), github_url, github_ci_url)
```

V4 uses structured inline elements:
```sql
-- v4 step 2: Create inline element array (duck_block_utils)
[doc_link('GitHub', github_url), doc_text(' '), doc_inline_image(github_ci_url, 'CI')] AS github_inlines

-- v4 step 3: Render to markdown (markdown extension)
'- ' || doc_elements_to_md(github_inlines)
```

## Benefits

- **Type-safe**: Can't make markdown syntax errors
- **Programmatic**: Use SQL expressions to build inline content dynamically
- **Separation of concerns**: Data + structure in step 2, rendering in step 3
- **Multi-binary**: Each extension does what it does best

## Running the Pipeline

```bash
# Step 1: Load YAML data
duckdb_yaml/build/release/duckdb /tmp/dashboard.duckdb -f 01_yaml_load_data.sql

# Step 2: Create page structure with blocks and inline arrays
duck_block_utils/build/release/duckdb /tmp/dashboard.duckdb -f 02_duck_block_utils_create_page_structure.sql

# Step 3: Render to markdown
duckdb_markdown/build/release/duckdb /tmp/dashboard.duckdb -f 03_markdown_render.sql
```

## Files

- `01_yaml_load_data.sql` - Loads projects.yaml into tables
- `02_duck_block_utils_create_page_structure.sql` - Creates blocks + inline arrays
- `03_markdown_render.sql` - Renders everything to markdown
- `projects_dashboard.md` - Generated output
