# Duck Block Utils

A DuckDB extension for structured document element manipulation.

## Overview

`duck_block_utils` provides a comprehensive toolkit for working with document elements in DuckDB. It implements a flat, depth-first tree representation of documents (similar to Pandoc's AST), enabling powerful document manipulation, assembly, and extraction operations directly in SQL.

## Key Features

- **Unified duck_block Type**: A single type for both block and inline elements with `kind` discriminator
- **Block Builder Functions**: Create headings, paragraphs, code blocks, lists, and more with simple function calls
- **Inline Builder Functions**: Create links, bold, italic, code spans, and images for cross-format rich text
- **Assembly Functions**: Combine blocks into documents with automatic ordering and level management
- **Manipulation Functions**: Filter, merge, slice, and reorder element collections
- **Extraction Functions**: Extract text, headings, code blocks, and statistics from documents
- **Type Functions**: Standard constructors, validators, and accessors for integration with other extensions
- **Terminal Rendering**: Render documents and query results as styled ANSI terminal output, with width-aware word wrapping

## The Unified duck_block Type

Both block-level and inline elements use the same type, distinguished by the `kind` field:

```sql
STRUCT(
    kind VARCHAR,                       -- 'block', 'inline' or 'value'
    element_type VARCHAR,               -- 'heading', 'paragraph', 'text', 'link', etc.
    content VARCHAR,                    -- Primary text content
    level INTEGER,                      -- Structural depth, ALWAYS explicit; top level is 1, never NULL
    encoding VARCHAR,                   -- 'text', 'json', 'yaml', 'html', 'xml'
    attributes MAP(VARCHAR, VARCHAR),   -- Type-specific metadata
    element_order INTEGER               -- Position in document (0-indexed)
)
```

### Block Types (kind='block')

| Type | Description | Level Used | Typical Encoding |
|------|-------------|------------|------------------|
| `heading` | Section headings (h1-h6) | Yes (1-6) | text |
| `paragraph` | Body text | No | text |
| `code` | Code blocks | No | text |
| `blockquote` | Quoted text | Optional | text |
| `list` | Ordered/unordered lists | Optional | json |
| `table` | Tabular data | No | json |
| `hr` | Horizontal rule | No | text |
| `metadata` | YAML frontmatter | No | yaml |
| `image` | Image references | No | text |
| `raw` | Raw HTML/XML | No | html/xml |

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

## Quick Start

```sql
-- Load the extension
LOAD duck_block_utils;

-- Create a simple document
SELECT duck_blocks_assemble([
    duck_block_heading('Introduction', 1),
    duck_block_paragraph('Welcome to the documentation.'),
    duck_block_heading('Getting Started', 2),
    duck_block_code('SELECT * FROM docs;', 'sql'),
    duck_block_paragraph('Run the query above to begin.')
]);

-- Extract just the headings
SELECT duck_blocks_headings(duck_blocks_assemble([
    duck_block_heading('Chapter 1', 1),
    duck_block_paragraph('Content...'),
    duck_block_heading('Section 1.1', 2)
]));

-- Convert to plain text
SELECT duck_blocks_to_text([
    duck_block_heading('Title', 1),
    duck_block_paragraph('Hello world')
]);

-- Create rich text with inline elements
SELECT [
    duck_block_text('Click '),
    duck_block_link('here', 'https://example.com'),
    duck_block_text(' to learn more.')
];
```

## Documentation

- [Duck Blocks Specification](duck_blocks_spec.md) - **the vocabulary**: kinds, element types, nesting, and the rules consumers must follow
- [Getting Started](getting_started.md) - Installation and basic usage
- [API Reference](api_reference.md) - Complete function reference
- [Block Builder Functions](block_builders.md) - Creating document blocks
- [Inline Builder Functions](inline_builders.md) - Creating inline elements for rich text
- [Type Functions](type_functions.md) - Type constructors and validators
- [ANSI Terminal Rendering](rendering.md) - Pretty-print documents and query results in the terminal
- [Examples](examples.md) - Common patterns and use cases
- [Integration Guide](integration.md) - Integrating with other extensions
- [Pandoc AST Specification](pandoc_ast_spec.md) - Constructor-by-constructor mapping

## Installation

```sql
INSTALL duck_block_utils FROM community;
LOAD duck_block_utils;
```

## Use Cases

- **Document Generation**: Build structured documents from query results
- **Content Analysis**: Extract metadata, headings, and code from documents
- **Document Transformation**: Filter, merge, and restructure document content
- **Multi-format Export**: Prepare documents for conversion to Markdown, HTML, etc.
- **Content Aggregation**: Combine documents from multiple sources with proper ordering

## License

MIT License
