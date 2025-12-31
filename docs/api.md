# Duck Block Utils - API Reference

Complete reference for all functions provided by the `duck_block_utils` extension.

## Type Definitions

### doc_block

The core document block type used throughout this extension.

```sql
STRUCT(
    block_type VARCHAR,                    -- Block type identifier
    content VARCHAR,                       -- Primary content
    level INTEGER,                         -- Hierarchy level (NULL if not applicable)
    encoding VARCHAR,                      -- Content encoding: 'text', 'json', 'yaml', 'html', 'xml'
    attributes MAP(VARCHAR, VARCHAR),      -- Type-specific metadata
    block_order INTEGER                    -- Position in document (0-indexed)
)
```

### doc_block_ext

Extended block type with provenance tracking.

```sql
STRUCT(
    block_type VARCHAR,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR,
    attributes MAP(VARCHAR, VARCHAR),
    block_order INTEGER,
    source_format VARCHAR,                 -- Origin format: 'markdown', 'html', etc.
    file_path VARCHAR                      -- Source file path
)
```

---

## Block Manipulation Functions

### doc_blocks_filter

Filter blocks to include only specified types.

**Signature:**
```sql
doc_blocks_filter(
    blocks LIST(doc_block),
    types VARCHAR[]
) → LIST(doc_block)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |
| `types` | VARCHAR[] | Block types to include |

**Returns:** Filtered list containing only blocks with matching types.

**Example:**
```sql
-- Keep only headings and code blocks
SELECT doc_blocks_filter(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b),
    ['heading', 'code']
);
```

---

### doc_blocks_exclude

Filter blocks to exclude specified types.

**Signature:**
```sql
doc_blocks_exclude(
    blocks LIST(doc_block),
    types VARCHAR[]
) → LIST(doc_block)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |
| `types` | VARCHAR[] | Block types to exclude |

**Returns:** Filtered list with specified types removed.

**Example:**
```sql
-- Remove all horizontal rules and metadata
SELECT doc_blocks_exclude(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b),
    ['hr', 'metadata']
);
```

---

### doc_blocks_merge

Combine two block sequences into one.

**Signature:**
```sql
doc_blocks_merge(
    blocks1 LIST(doc_block),
    blocks2 LIST(doc_block)
) → LIST(doc_block)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks1` | LIST(doc_block) | First block sequence |
| `blocks2` | LIST(doc_block) | Second block sequence (appended) |

**Returns:** Combined list with `block_order` values adjusted for continuity.

**Example:**
```sql
-- Merge two documents
SELECT doc_blocks_merge(
    (SELECT list(b) FROM read_markdown_blocks('intro.md') b),
    (SELECT list(b) FROM read_markdown_blocks('main.md') b)
);
```

---

### doc_blocks_reorder

Renumber block_order values sequentially from 0.

**Signature:**
```sql
doc_blocks_reorder(
    blocks LIST(doc_block)
) → LIST(doc_block)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |

**Returns:** Blocks with block_order renumbered as 0, 1, 2, ...

**Example:**
```sql
-- Fix gaps in block_order after filtering
SELECT doc_blocks_reorder(
    doc_blocks_filter(blocks, ['heading', 'paragraph'])
);
```

---

### doc_blocks_slice

Extract a contiguous range of blocks.

**Signature:**
```sql
doc_blocks_slice(
    blocks LIST(doc_block),
    start_order INTEGER,
    end_order INTEGER
) → LIST(doc_block)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |
| `start_order` | INTEGER | Starting block_order (inclusive) |
| `end_order` | INTEGER | Ending block_order (inclusive) |

**Returns:** Blocks within the specified range.

**Example:**
```sql
-- Extract blocks 5 through 10
SELECT doc_blocks_slice(blocks, 5, 10);
```

---

### doc_blocks_transform

Apply transformations to block types and content.

**Signature:**
```sql
doc_blocks_transform(
    blocks LIST(doc_block),
    type_mapping MAP(VARCHAR, VARCHAR),
    content_fn VARCHAR DEFAULT NULL
) → LIST(doc_block)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |
| `type_mapping` | MAP(VARCHAR, VARCHAR) | Map of old_type → new_type |
| `content_fn` | VARCHAR | Optional: SQL expression for content transformation |

**Returns:** Blocks with transformed types and optionally transformed content.

**Example:**
```sql
-- Convert all blockquotes to paragraphs
SELECT doc_blocks_transform(
    blocks,
    MAP{'blockquote': 'paragraph'}
);
```

---

## Content Extraction Functions

### doc_blocks_to_text

Extract plain text content from blocks.

**Signature:**
```sql
doc_blocks_to_text(
    blocks LIST(doc_block),
    separator VARCHAR DEFAULT '\n\n'
) → VARCHAR
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |
| `separator` | VARCHAR | Text between blocks (default: double newline) |

**Returns:** Concatenated plain text content.

**Example:**
```sql
-- Get document as plain text
SELECT doc_blocks_to_text(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b)
);
```

---

### doc_blocks_headings

Extract heading information as a table.

**Signature:**
```sql
doc_blocks_headings(
    blocks LIST(doc_block)
) → TABLE(level INTEGER, title VARCHAR, id VARCHAR, block_order INTEGER)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |

**Returns:** Table of headings with level, title, id attribute, and position.

**Example:**
```sql
-- Get all headings
SELECT * FROM doc_blocks_headings(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b)
);
```

**Output:**
| level | title | id | block_order |
|-------|-------|-----|-------------|
| 1 | Introduction | introduction | 0 |
| 2 | Getting Started | getting-started | 3 |
| 2 | Examples | examples | 8 |

---

### doc_blocks_toc

Generate a table of contents from headings.

**Signature:**
```sql
doc_blocks_toc(
    blocks LIST(doc_block),
    min_level INTEGER DEFAULT 1,
    max_level INTEGER DEFAULT 6
) → TABLE(level INTEGER, title VARCHAR, id VARCHAR, indent VARCHAR, block_order INTEGER)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |
| `min_level` | INTEGER | Minimum heading level to include |
| `max_level` | INTEGER | Maximum heading level to include |

**Returns:** Table with heading info plus indentation string.

**Example:**
```sql
-- Generate indented TOC
SELECT indent || '- [' || title || '](#' || id || ')' as toc_line
FROM doc_blocks_toc(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b)
);
```

---

### doc_blocks_code_blocks

Extract code blocks with metadata.

**Signature:**
```sql
doc_blocks_code_blocks(
    blocks LIST(doc_block)
) → TABLE(language VARCHAR, content VARCHAR, info_string VARCHAR, block_order INTEGER)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |

**Returns:** Table of code blocks with language and content.

**Example:**
```sql
-- Get all Python code blocks
SELECT content
FROM doc_blocks_code_blocks(blocks)
WHERE language = 'python';
```

---

### doc_blocks_links

Extract links from block content.

**Signature:**
```sql
doc_blocks_links(
    blocks LIST(doc_block)
) → TABLE(text VARCHAR, url VARCHAR, title VARCHAR, block_order INTEGER)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |

**Returns:** Table of links extracted from text content.

**Example:**
```sql
-- Find all external links
SELECT * FROM doc_blocks_links(blocks)
WHERE url LIKE 'http%';
```

---

## Validation Functions

### doc_blocks_validate

Check blocks for schema compliance.

**Signature:**
```sql
doc_blocks_validate(
    blocks LIST(doc_block)
) → STRUCT(valid BOOLEAN, errors LIST(VARCHAR))
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |

**Returns:** Struct with validation result and list of errors.

**Validation checks:**
- block_type is non-empty
- encoding is valid ('text', 'json', 'yaml', 'html', 'xml')
- JSON content is valid when encoding='json'
- YAML content is valid when encoding='yaml'
- block_order is non-negative
- level is NULL or non-negative

**Example:**
```sql
SELECT (doc_blocks_validate(blocks)).valid as is_valid;
```

---

### doc_blocks_lint

Check for common issues and best practices.

**Signature:**
```sql
doc_blocks_lint(
    blocks LIST(doc_block)
) → TABLE(severity VARCHAR, message VARCHAR, block_order INTEGER, suggestion VARCHAR)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |

**Returns:** Table of issues with severity, message, location, and suggestions.

**Severity levels:**
- `error` - Critical issues that may cause problems
- `warning` - Potential issues or non-standard patterns
- `info` - Suggestions for improvement

**Checks performed:**
| Severity | Issue |
|----------|-------|
| error | Duplicate block_order values |
| warning | Heading level skipped (e.g., h1 → h3) |
| warning | Empty content in non-hr/non-image blocks |
| warning | Unknown block_type (not core, not namespaced) |
| warning | Code block without language |
| info | Large gaps in block_order |

**Example:**
```sql
-- Show all errors and warnings
SELECT * FROM doc_blocks_lint(blocks)
WHERE severity IN ('error', 'warning');
```

---

### doc_blocks_stats

Get block type statistics.

**Signature:**
```sql
doc_blocks_stats(
    blocks LIST(doc_block)
) → TABLE(block_type VARCHAR, count INTEGER, avg_content_length DOUBLE, total_content_length BIGINT)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |

**Returns:** Aggregated statistics per block type.

**Example:**
```sql
-- Document composition
SELECT * FROM doc_blocks_stats(blocks)
ORDER BY count DESC;
```

---

### doc_blocks_structure

Analyze document structure.

**Signature:**
```sql
doc_blocks_structure(
    blocks LIST(doc_block)
) → STRUCT(
    block_count INTEGER,
    heading_count INTEGER,
    max_heading_level INTEGER,
    has_metadata BOOLEAN,
    has_code BOOLEAN,
    has_tables BOOLEAN,
    languages VARCHAR[]
)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |

**Returns:** Struct summarizing document structure.

**Example:**
```sql
SELECT (doc_blocks_structure(blocks)).*;
```

---

## Conversion Helper Functions

### doc_blocks_set_source

Add or update source_format on all blocks.

**Signature:**
```sql
doc_blocks_set_source(
    blocks LIST(doc_block),
    format VARCHAR
) → LIST(doc_block_ext)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |
| `format` | VARCHAR | Source format identifier |

**Returns:** Extended blocks with source_format set.

**Example:**
```sql
-- Track provenance
SELECT doc_blocks_set_source(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b),
    'markdown'
);
```

---

### doc_blocks_normalize

Convert all blocks to core types only.

**Signature:**
```sql
doc_blocks_normalize(
    blocks LIST(doc_block)
) → LIST(doc_block)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |

**Returns:** Blocks with namespaced types converted to nearest core type.

**Normalization rules:**
| Original Type | Normalized Type | Notes |
|---------------|-----------------|-------|
| `md:footnote` | `paragraph` | `attributes['original_type']` preserved |
| `html:div` | `raw` | `attributes['format']='html'` |
| `html:h1`-`html:h6` | `heading` | Level extracted from tag |
| `html:p` | `paragraph` | |
| `html:pre` | `code` | |
| `html:ul`, `html:ol` | `list` | |
| `html:table` | `table` | |
| `xml:*` | `raw` | `attributes['format']='xml'` |

**Example:**
```sql
-- Normalize before cross-format merge
SELECT doc_blocks_normalize(html_blocks);
```

---

### doc_blocks_map_types

Apply custom type mapping.

**Signature:**
```sql
doc_blocks_map_types(
    blocks LIST(doc_block),
    mapping MAP(VARCHAR, VARCHAR)
) → LIST(doc_block)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input block sequence |
| `mapping` | MAP(VARCHAR, VARCHAR) | Type transformation map |

**Returns:** Blocks with mapped types (unmapped types unchanged).

**Example:**
```sql
-- Custom format conversion
SELECT doc_blocks_map_types(
    blocks,
    MAP{
        'html:section': 'heading',
        'html:article': 'paragraph',
        'html:aside': 'blockquote'
    }
);
```

---

## Aggregate Functions

### doc_blocks_agg

Aggregate rows into a block list (for use with GROUP BY).

**Signature:**
```sql
doc_blocks_agg(
    block_type VARCHAR,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR,
    attributes MAP(VARCHAR, VARCHAR),
    block_order INTEGER
) → LIST(doc_block)
```

**Example:**
```sql
-- Collect blocks by file
SELECT
    file_path,
    doc_blocks_agg(block_type, content, level, encoding, attributes, block_order) as blocks
FROM read_markdown_blocks('docs/**/*.md', include_filepath := true)
GROUP BY file_path;
```

---

## See Also

- [Design Document](design.md) - Architecture and implementation details
- [Document Block Specification](https://github.com/teaguesterling/duckdb_markdown/blob/main/docs/doc_block_spec.md) - Core block schema
- [DuckDB Markdown Extension](https://github.com/teaguesterling/duckdb_markdown) - Reference implementation
