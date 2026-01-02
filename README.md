# DuckDB Document Block Utilities

A DuckDB extension providing format-agnostic utilities for working with document elements.

## Overview

This extension complements format-specific document extensions (markdown, HTML, XML, YAML) by providing common utilities that work with any document represented using the [Duck Blocks Specification](docs/duck_blocks_spec.md).

## Features

- **Unified doc_element type**: Single type for both block and inline elements with `kind` discriminator
- **Block builders**: Declarative document construction from SQL queries
- **Inline builders**: Rich text formatting with links, bold, italic, code, and more
- **Block manipulation**: Filter, transform, merge, and reorder elements
- **Content extraction**: Extract plain text, headings, and generate TOCs
- **Validation**: Check schema compliance and lint for common issues
- **Pandoc AST conversion**: Bidirectional JSON AST ↔ doc_elements (no Pandoc required)
- **Conversion helpers**: Normalize blocks and track provenance

## Installation

```sql
INSTALL duck_block_utils FROM community;
LOAD duck_block_utils;
```

## Quick Start

```sql
-- Load markdown extension for reading
LOAD markdown;
LOAD duck_block_utils;

-- Extract all headings as a table of contents
SELECT * FROM doc_blocks_toc(
    (SELECT list(b) FROM read_markdown_blocks('README.md') b)
);

-- Get plain text content
SELECT doc_blocks_to_text(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b)
);

-- Filter to specific block types
SELECT unnest(doc_blocks_filter(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b),
    ['heading', 'code']
));

-- Validate block schema
SELECT doc_blocks_validate(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b)
);
```

## The Unified doc_element Type

Both block-level and inline elements use the same unified type, distinguished by the `kind` field:

```sql
STRUCT(
    kind VARCHAR,                       -- 'block' or 'inline'
    element_type VARCHAR,               -- 'heading', 'paragraph', 'text', 'link', etc.
    content VARCHAR,                    -- Primary content
    level INTEGER,                      -- Structural nesting depth (NOT heading level)
    encoding VARCHAR,                   -- 'text', 'json', 'yaml', 'html', 'xml'
    attributes MAP(VARCHAR, VARCHAR),   -- Type-specific metadata (heading_level, href, etc.)
    element_order INTEGER               -- Position in document (0-indexed)
)
```

### Block Types (kind='block')

| Type | Description | level Field | Key Attributes |
|------|-------------|-------------|----------------|
| `heading` | Section headings (h1-h6) | NULL | `heading_level` (1-6) |
| `paragraph` | Body text | NULL | |
| `code` | Code blocks | NULL | `language` |
| `blockquote` | Quoted text | Nesting depth | |
| `list` | Ordered/unordered lists | NULL | `ordered` |
| `table` | Tabular data | NULL | |
| `hr` | Horizontal rule | NULL | |
| `metadata` | YAML frontmatter | NULL | |
| `image` | Image references | NULL | `src`, `alt`, `title` |
| `raw` | Raw HTML/XML | NULL | `format` |

**Note:** For headings, the semantic level (h1-h6) is stored in `attributes['heading_level']`, not the `level` field. This separates heading semantics from structural nesting depth.

### Inline Types (kind='inline')

| Type | Description | Attributes |
|------|-------------|------------|
| `text` | Plain text | none |
| `link` | Hyperlink | `href`, `title` |
| `image` | Inline image | `src`, `alt`, `title` |
| `bold` | Bold/strong text | none |
| `italic` | Italic/emphasis | none |
| `code` | Inline code | none |
| `strikethrough` | Strikethrough | none |

## Functions

### Block Manipulation

| Function | Description |
|----------|-------------|
| `doc_blocks_filter(blocks, types[])` | Filter blocks to specific types |
| `doc_blocks_exclude(blocks, types[])` | Exclude specific block types |
| `doc_blocks_transform(blocks, mappings)` | Apply type/content transformations |
| `doc_blocks_merge(blocks1, blocks2)` | Merge two block sequences |
| `doc_blocks_reorder(blocks)` | Renumber element_order sequentially |
| `doc_blocks_slice(blocks, start, end)` | Extract block range |

### Content Extraction

| Function | Description |
|----------|-------------|
| `doc_blocks_to_text(blocks)` | Extract plain text content |
| `doc_blocks_headings(blocks)` | Extract heading hierarchy |
| `doc_blocks_toc(blocks)` | Generate table of contents |
| `doc_blocks_code_blocks(blocks)` | Extract code blocks with metadata |
| `doc_blocks_links(blocks)` | Extract all links from content |

### Validation & Analysis

| Function | Description |
|----------|-------------|
| `doc_blocks_validate(blocks)` | Check schema compliance |
| `doc_blocks_lint(blocks)` | Check for common issues |
| `doc_blocks_stats(blocks)` | Block type statistics |
| `doc_blocks_structure(blocks)` | Analyze document structure |

### Conversion Helpers

| Function | Description |
|----------|-------------|
| `doc_blocks_set_source(blocks, format)` | Set source_format on all blocks |
| `doc_blocks_normalize(blocks)` | Convert to core types only |
| `doc_blocks_map_types(blocks, mapping)` | Remap block types |

### Block Builders (Declarative Construction)

| Function | Description |
|----------|-------------|
| `doc_heading(content, level, id)` | Create heading block |
| `doc_paragraph(content)` | Create paragraph block |
| `doc_code(content, language)` | Create code block |
| `doc_list_block(items[], ordered)` | Create list block |
| `doc_table_block(headers[], rows[][])` | Create table block |
| `doc_section(title, level, children...)` | Create section with nested content |
| `doc_document(children...)` | Assemble complete document |
| `doc_table_from_query(query)` | Convert query result to table block |
| `doc_from_rows(query, transformer)` | Generate blocks from each row |

### Inline Builders

| Function | Description |
|----------|-------------|
| `doc_text(content)` | Create plain text inline |
| `doc_link(text, href, title)` | Create hyperlink |
| `doc_bold(content)` | Create bold text |
| `doc_italic(content)` | Create italic text |
| `doc_inline_code(content)` | Create inline code |
| `doc_inline_image(src, alt, title)` | Create inline image |

### Pandoc AST Conversion

| Function | Description |
|----------|-------------|
| `pandoc_ast_to_blocks(ast, mode)` | Convert Pandoc JSON AST to doc_elements |
| `pandoc_blocks_to_ast(blocks, meta)` | Convert doc_elements to Pandoc JSON AST |
| `pandoc_inlines_to_text(inlines, mode)` | Convert inline elements to text |
| `pandoc_inlines_to_doc_inlines(inlines)` | Convert Pandoc inlines to doc_element inlines |
| `doc_inlines_to_pandoc(inlines)` | Convert doc_element inlines to Pandoc JSON |

## Examples

### Generate Table of Contents

```sql
SELECT
    repeat('  ', level - 1) || '- ' || title as toc_line,
    id
FROM doc_blocks_toc(
    (SELECT list(b) FROM read_markdown_blocks('docs/**/*.md') b)
)
ORDER BY doc_order, element_order;
```

### Extract Code Examples by Language

```sql
SELECT
    language,
    content as code,
    file_path
FROM doc_blocks_code_blocks(
    (SELECT list(b) FROM read_markdown_blocks('tutorial/*.md', include_filepath := true) b)
)
WHERE language = 'python';
```

### Merge Documents with Separator

```sql
SELECT unnest(doc_blocks_merge(
    doc_blocks_merge(
        (SELECT list(b) FROM read_markdown_blocks('intro.md') b),
        [{'kind': 'block', 'element_type': 'hr', 'content': '', 'level': NULL,
          'encoding': 'text', 'attributes': {}, 'element_order': 0}]
    ),
    (SELECT list(b) FROM read_markdown_blocks('main.md') b)
));
```

### Validate and Report Issues

```sql
SELECT * FROM doc_blocks_lint(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b)
)
WHERE severity = 'error';
```

### Generate Report from Query (Declarative Builders)

```sql
-- Build a complete document from SQL data
COPY (
    SELECT unnest(doc_document(
        -- Metadata
        doc_metadata('title: Sales Report\ndate: 2024-12-30'),

        -- Title
        doc_heading('Q4 Sales Report', 1),
        doc_paragraph('Auto-generated report from database.'),

        -- Dynamic table from query
        doc_section('Top Products', 2,
            doc_table_from_query((
                SELECT name, units_sold, revenue
                FROM products ORDER BY revenue DESC LIMIT 10
            ))
        ),

        -- Section per region using row transformer
        doc_grouped_sections(
            (SELECT region, total_sales, growth FROM regional_summary),
            'region', 2,
            row -> doc_paragraph('Sales: $' || row.total_sales ||
                                 ' (Growth: ' || row.growth || '%)')
        )
    ))
) TO 'report.md' (FORMAT MARKDOWN, markdown_mode 'blocks');
```

### Build Rich Text with Inline Elements

```sql
-- Create a paragraph with formatted inline content
SELECT [
    doc_text('Click '),
    doc_link('here', 'https://example.com'),
    doc_text(' to learn more about '),
    doc_bold('DuckDB'),
    doc_text('.')
];
```

### Convert Pandoc JSON to Blocks

```sql
-- Read Pandoc JSON (from any tool that exports it)
-- and convert to doc_elements without needing Pandoc installed
SELECT unnest(pandoc_ast_to_blocks(
    read_json_auto('exported_from_pandoc.json')
));

-- Convert blocks back to Pandoc JSON for other tools
COPY (
    SELECT pandoc_blocks_to_ast(
        (SELECT list(b) FROM read_markdown_blocks('doc.md') b),
        '{"title": {"t": "MetaString", "c": "My Doc"}}'::JSON
    )
) TO 'for_pandoc.json';
```

## Building

### Managing dependencies

DuckDB extensions use VCPKG for dependency management. Enabling VCPKG is simple:

```shell
git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
export VCPKG_TOOLCHAIN_PATH=`pwd`/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### Build steps

```sh
make
```

The main binaries that will be built are:
```sh
./build/release/duckdb
./build/release/test/unittest
./build/release/extension/duck_block_utils/duck_block_utils.duckdb_extension
```

### Running the tests

```sh
make test
```

## Ecosystem

This extension is part of the DuckDB document processing ecosystem:

| Extension | Purpose |
|-----------|---------|
| [duckdb_markdown](https://github.com/teaguesterling/duckdb_markdown) | Markdown reading/writing |
| [duckdb_webbed](https://github.com/teaguesterling/duckdb_webbed) | HTML/XML parsing (planned) |
| duckdb_yaml | YAML document parsing (planned) |
| [panduck](https://github.com/teaguesterling/panduck) | Pandoc integration (planned) |

## Documentation

- [Design Document](docs/design.md) - Architecture and implementation details
- [API Reference](docs/api.md) - Complete function reference
- [Block Builders](docs/block_builders.md) - Declarative document construction
- [Inline Builders](docs/inline_builders.md) - Rich text inline elements
- [Duck Blocks Spec](docs/duck_blocks_spec.md) - Unified doc_element type specification
- [Pandoc AST Spec](docs/pandoc_ast_spec.md) - Pandoc JSON conversion rules

## License

MIT License - see [LICENSE](LICENSE) for details.

## Contributing

Contributions welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.
