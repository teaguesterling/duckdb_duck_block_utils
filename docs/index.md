# Duck Block Utils

A DuckDB extension for structured document block manipulation.

## Overview

`duck_block_utils` provides a comprehensive toolkit for working with document blocks in DuckDB. It implements a flat, depth-first tree representation of documents (similar to Pandoc's AST), enabling powerful document manipulation, assembly, and extraction operations directly in SQL.

## Key Features

- **Structured Document Types**: The `doc_block` struct for document elements and `doc_inline` for rich text formatting
- **Block Builder Functions**: Create headings, paragraphs, code blocks, lists, and more with simple function calls
- **Inline Builder Functions**: Create links, bold, italic, code spans, and images for cross-format rich text
- **Assembly Functions**: Combine blocks into documents with automatic ordering and level management
- **Manipulation Functions**: Filter, merge, slice, and reorder block collections
- **Extraction Functions**: Extract text, headings, code blocks, and statistics from documents
- **Type Functions**: Standard constructors, validators, and accessors for integration with other extensions

## The doc_block Type

The core data type is a struct with six fields:

```sql
STRUCT(
    block_type VARCHAR,                    -- 'heading', 'paragraph', 'code', etc.
    content VARCHAR,                       -- Primary text content
    level INTEGER,                         -- Hierarchy level (NULL if N/A)
    encoding VARCHAR,                      -- 'text', 'json', 'yaml', 'html', 'xml'
    attributes MAP(VARCHAR, VARCHAR),      -- Type-specific metadata
    block_order INTEGER                    -- Position in document (0-indexed)
)
```

### Supported Block Types

| Block Type | Description | Level Used | Typical Encoding |
|------------|-------------|------------|------------------|
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

## The doc_inline Type

For rich text formatting within blocks, the `doc_inline` type represents inline elements:

```sql
STRUCT(
    inline_type VARCHAR,                   -- 'text', 'link', 'bold', etc.
    content VARCHAR,                       -- Text content or alt text
    attributes MAP(VARCHAR, VARCHAR)       -- Type-specific attributes
)
```

### Supported Inline Types

| Inline Type | Description | Attributes |
|-------------|-------------|------------|
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
SELECT doc_assemble([
    doc_heading('Introduction', 1),
    doc_paragraph('Welcome to the documentation.'),
    doc_heading('Getting Started', 2),
    doc_code('SELECT * FROM docs;', 'sql'),
    doc_paragraph('Run the query above to begin.')
]);

-- Extract just the headings
SELECT doc_blocks_headings(doc_assemble([
    doc_heading('Chapter 1', 1),
    doc_paragraph('Content...'),
    doc_heading('Section 1.1', 2)
]));

-- Convert to plain text
SELECT doc_blocks_to_text([
    doc_heading('Title', 1),
    doc_paragraph('Hello world')
]);
```

## Documentation

- [Getting Started](getting_started.md) - Installation and basic usage
- [API Reference](api_reference.md) - Complete function reference
- [Block Builder Functions](block_builders.md) - Creating document blocks
- [Inline Builder Functions](inline_builders.md) - Creating inline elements for rich text
- [Type Functions](type_functions.md) - Type constructors and validators
- [Examples](examples.md) - Common patterns and use cases
- [Integration Guide](integration.md) - Integrating with other extensions

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
