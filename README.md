# DuckDB Document Block Utilities

A DuckDB extension providing format-agnostic utilities for working with document elements.

## Overview

This extension complements format-specific document extensions (markdown, HTML, XML, YAML) by providing common utilities that work with any document represented using the [Duck Blocks Specification](docs/duck_blocks_spec.md).

## Features

- **Unified duck_block type**: Single type for both block and inline elements with `kind` discriminator
- **Block builders**: Declarative document construction from SQL queries
- **Inline builders**: Rich text formatting with links, bold, italic, code, and more
- **Block manipulation**: Filter, transform, merge, and reorder elements
- **Content extraction**: Extract plain text, headings, and generate TOCs
- **Validation**: Check schema compliance and lint for common issues
- **Pandoc AST conversion**: Bidirectional JSON AST ↔ duck_blocks (no Pandoc required)
- **Conversion helpers**: Normalize blocks and track provenance
- **ANSI terminal rendering**: `PRAGMA duck_block_render` — render documents and query results as styled terminal output, glow-style ([docs](docs/rendering.md))

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
SELECT * FROM db_blocks_toc(
    (SELECT list(b) FROM read_markdown_blocks('README.md') b)
);

-- Get plain text content
SELECT db_blocks_to_text(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b)
);

-- Filter to specific block types
SELECT unnest(db_blocks_filter(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b),
    ['heading', 'code']
));

-- Validate block schema
SELECT db_blocks_validate(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b)
);
```

## The Unified duck_block Type

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
| `div` | Generic container | NULL | `id`, `class` |

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
| `db_blocks_filter(blocks, types[])` | Filter blocks to specific types |
| `db_blocks_exclude(blocks, types[])` | Exclude specific block types |
| `db_blocks_transform(blocks, mappings)` | Apply type/content transformations |
| `db_blocks_merge(blocks1, blocks2)` | Merge two block sequences |
| `db_blocks_reorder(blocks)` | Renumber element_order sequentially |
| `db_blocks_slice(blocks, start, end)` | Extract block range |

### Content Extraction

| Function | Description |
|----------|-------------|
| `db_blocks_to_text(blocks)` | Extract plain text content |
| `db_blocks_headings(blocks)` | Extract heading hierarchy |
| `db_blocks_toc(blocks)` | Generate table of contents |
| `db_blocks_code_blocks(blocks)` | Extract code blocks with metadata |
| `db_blocks_links(blocks)` | Extract all links from content |

### Validation & Analysis

| Function | Description |
|----------|-------------|
| `db_blocks_validate(blocks)` | Check schema compliance |
| `db_blocks_lint(blocks)` | Check for common issues |
| `db_blocks_stats(blocks)` | Block type statistics |
| `db_blocks_structure(blocks)` | Analyze document structure |

### Conversion Helpers

| Function | Description |
|----------|-------------|
| `db_blocks_set_source(blocks, format)` | Set source_format on all blocks |
| `db_blocks_normalize(blocks)` | Convert to core types only |
| `db_blocks_map_types(blocks, mapping)` | Remap block types |

### Block Builders (Declarative Construction)

| Function | Description |
|----------|-------------|
| `db_heading(content, level)` | Create heading block |
| `db_paragraph(content)` | Create paragraph block |
| `db_code(content, language)` | Create code block |
| `db_blockquote(content, level)` | Create blockquote block |
| `db_list_block(items[], ordered)` | Create list block (strings or rich items) |
| `db_div(children, id, class)` | Create generic container |
| `db_hr()` | Create horizontal rule |
| `db_metadata(yaml_content)` | Create metadata block |
| `db_image(src, alt, title)` | Create image block |
| `db_raw(content, format)` | Create raw content block |

### Flattening Builder Overloads

These overloads accept a list of children and return a flattened list with parent at level 1 and children at level 2:

| Function | Description |
|----------|-------------|
| `db_paragraph(children[])` | Paragraph with inline children |
| `db_heading(level, children[])` | Heading with inline children |
| `db_blockquote(level, children[])` | Blockquote with children |
| `db_code(language, children[])` | Code block with children |
| `db_bold(children[])` | Bold with inline children |
| `db_italic(children[])` | Italic with inline children |
| `db_link(href, children[], title)` | Link with inline children |

### Inline Builders

| Function | Description |
|----------|-------------|
| `db_text(content)` | Create plain text inline |
| `db_space()` | Create space inline |
| `db_softbreak()` | Create soft break |
| `db_linebreak()` | Create line break |
| `db_bold(content)` | Create bold text |
| `db_italic(content)` | Create italic text |
| `db_strikethrough(content)` | Create strikethrough text |
| `db_superscript(content)` | Create superscript |
| `db_subscript(content)` | Create subscript |
| `db_smallcaps(content)` | Create small caps |
| `db_underline(content)` | Create underlined text |
| `db_inline_code(content)` | Create inline code |
| `db_math(content, display)` | Create math expression |
| `db_link(href, text, title)` | Create hyperlink |
| `db_inline_image(src, alt, title)` | Create inline image |
| `db_quoted(content, quote_type)` | Create quoted text |
| `db_cite(key, prefix, suffix)` | Create citation |
| `db_note(content)` | Create footnote |
| `db_span(content, id, classes)` | Create span |
| `db_raw_inline(content, format)` | Create raw inline |

### Pandoc AST Conversion

| Function | Description |
|----------|-------------|
| `read_pandoc_ast(file_path)` | Read Pandoc JSON file and convert to duck_blocks |
| `pandoc_ast_to_blocks(json)` | Convert Pandoc JSON AST string to duck_blocks |
| `duck_blocks_to_pandoc_blocks(blocks)` | Convert duck_blocks to Pandoc JSON blocks array |
| `duck_blocks_to_pandoc_ast(blocks)` | Convert to complete Pandoc AST struct |
| `pandoc_ast(blocks, meta, api_version)` | Table function for JSON file output |
| `write_pandoc_ast(path, blocks)` | Write duck_blocks directly to Pandoc JSON file |
| `pandoc_inlines_to_text(inlines)` | Convert inline elements to text |
| `pandoc_inlines_to_db_inlines(inlines)` | Convert Pandoc inlines to duck_block inlines |
| `db_inlines_to_pandoc(inlines)` | Convert duck_block inlines to Pandoc JSON |

## Examples

### Generate Table of Contents

```sql
SELECT
    repeat('  ', level - 1) || '- ' || title as toc_line,
    id
FROM db_blocks_toc(
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
FROM db_blocks_code_blocks(
    (SELECT list(b) FROM read_markdown_blocks('tutorial/*.md', include_filepath := true) b)
)
WHERE language = 'python';
```

### Merge Documents with Separator

```sql
SELECT unnest(db_blocks_merge(
    db_blocks_merge(
        (SELECT list(b) FROM read_markdown_blocks('intro.md') b),
        [db_hr()]
    ),
    (SELECT list(b) FROM read_markdown_blocks('main.md') b)
));
```

### Validate and Report Issues

```sql
SELECT * FROM db_blocks_lint(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b)
)
WHERE severity = 'error';
```

### Build Rich Text with Inline Elements

```sql
-- Create a paragraph with formatted inline content
SELECT [
    db_text('Click '),
    db_link('https://example.com', 'here'),
    db_text(' to learn more about '),
    db_bold('DuckDB'),
    db_text('.')
];

-- Using flattening overloads for nested structure
SELECT db_paragraph([
    db_text('This is '),
    db_bold([db_text('bold and '), db_italic([db_text('italic')])[1], db_italic([db_text('italic')])[2]])[1],
    db_bold([db_text('bold and '), db_italic([db_text('italic')])[1], db_italic([db_text('italic')])[2]])[2],
    db_text(' text.')
]);
```

### Convert Pandoc JSON to Blocks

```sql
-- Read Pandoc JSON file directly (simplest approach)
SELECT * FROM unnest(read_pandoc_ast('exported_from_pandoc.json'));

-- Or use pandoc_ast_to_blocks with a JSON string
SELECT unnest(pandoc_ast_to_blocks(
    (SELECT content FROM read_text('exported_from_pandoc.json'))
));
```

### Export to Pandoc JSON (for PDF, Word, etc.)

```sql
LOAD json;  -- Required for COPY FORMAT JSON

-- Basic export - creates Pandoc-compatible JSON
COPY (
    SELECT * FROM pandoc_ast(db_assemble([
        db_heading(1, 'My Document'),
        db_paragraph('Hello world.')
    ]))
) TO 'document.json' (FORMAT JSON);

-- With document metadata (title, author, date)
COPY (
    SELECT * FROM pandoc_ast(
        db_assemble([
            db_heading(1, 'Report'),
            db_paragraph('Introduction text...')
        ]),
        meta := {'title': 'Annual Report', 'author': 'Jane Doe', 'date': '2024-01-05'}
    )
) TO 'report.json' (FORMAT JSON);

-- Then convert with Pandoc CLI:
-- pandoc report.json -f json -o report.pdf
-- pandoc report.json -f json -o report.docx
```

## Short Aliases

For less verbose document composition, enable HTML-inspired short aliases:

```sql
PRAGMA duck_block_aliases;

-- Now you can use short names like h1, p, b, a, etc.
SELECT page([
    h1('DuckDB Search'),
    p([text('Found '), b('3'), text(' results.')]),
    pre('sql', 'LOAD extension;')
]);
```

Available aliases include: `h1`-`h6`, `p`, `pre`, `ul`, `ol`, `li`, `div`, `b`, `i`, `a`, `code`, and more. See [Block Builders](docs/block_builders.md) for the complete list.

## Terminal Rendering

Render documents — or any query result — as styled ANSI output for the terminal:

```sql
PRAGMA duck_block_render;

-- Documents: width-aware word wrap, styled headings/lists/tables
SELECT db_blocks_render_ansi(
    db_heading(1, 'Report') || db_paragraph('All systems **nominal**.')
);

-- Any query as a pretty ANSI table
SELECT rendered FROM db_render_query('SELECT * FROM my_table LIMIT 10');
```

Width is auto-detected from the terminal (even when piped to `less -R`); pass
it explicitly with `db_blocks_render_ansi(blocks, width)`. See
[Terminal Rendering](docs/rendering.md) for details, or try
`examples/duckglow.sql` for glow-style markdown file viewing:

```bash
duckdb -noheader -list \
  -c ".read examples/duckglow.sql" \
  -c "SELECT doc FROM glow('README.md');" | less -R
```

## Type Casting

Cast VARCHAR to duck_block to create text inline elements:

```sql
SELECT 'hello'::duck_block;  -- Creates text inline element
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
- [Duck Blocks Spec](docs/duck_blocks_spec.md) - Unified duck_block type specification
- [Pandoc AST Spec](docs/pandoc_ast_spec.md) - Pandoc JSON conversion rules
- [Terminal Rendering](docs/rendering.md) - ANSI output, word wrapping, pretty query tables

## License

MIT License - see [LICENSE](LICENSE) for details.

## Contributing

Contributions welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.
