# Duck Block Utils - API Reference

Complete reference for all functions provided by the `duck_block_utils` extension.

## Type Definitions

### doc_element (Unified Type)

The core document element type used throughout this extension. Both block-level and inline elements use the same type, distinguished by the `kind` field:

```sql
STRUCT(
    kind VARCHAR,                       -- 'block' or 'inline'
    element_type VARCHAR,               -- Element type identifier
    content VARCHAR,                    -- Primary content
    level INTEGER,                      -- Hierarchy level (NULL if not applicable)
    encoding VARCHAR,                   -- Content encoding: 'text', 'json', 'yaml', 'html', 'xml'
    attributes MAP(VARCHAR, VARCHAR),   -- Type-specific metadata
    element_order INTEGER               -- Position in document (0-indexed)
)
```

### Kind Values

| Kind | Description |
|------|-------------|
| `block` | Block-level elements (heading, paragraph, code, list, etc.) |
| `inline` | Inline elements (text, bold, italic, link, etc.) |

### doc_element_ext

Extended element type with provenance tracking.

```sql
STRUCT(
    kind VARCHAR,
    element_type VARCHAR,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR,
    attributes MAP(VARCHAR, VARCHAR),
    element_order INTEGER,
    source_format VARCHAR,              -- Origin format: 'markdown', 'html', etc.
    file_path VARCHAR                   -- Source file path
)
```

---

## Block Manipulation Functions

### doc_blocks_filter

Filter elements to include only specified types.

**Signature:**
```sql
doc_blocks_filter(
    blocks LIST(doc_element),
    types VARCHAR[]
) → LIST(doc_element)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |
| `types` | VARCHAR[] | Element types to include |

**Returns:** Filtered list containing only elements with matching types.

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

Filter elements to exclude specified types.

**Signature:**
```sql
doc_blocks_exclude(
    blocks LIST(doc_element),
    types VARCHAR[]
) → LIST(doc_element)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |
| `types` | VARCHAR[] | Element types to exclude |

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

Combine two element sequences into one.

**Signature:**
```sql
doc_blocks_merge(
    blocks1 LIST(doc_element),
    blocks2 LIST(doc_element)
) → LIST(doc_element)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks1` | LIST(doc_element) | First element sequence |
| `blocks2` | LIST(doc_element) | Second element sequence (appended) |

**Returns:** Combined list with `element_order` values adjusted for continuity.

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

Renumber element_order values sequentially from 0.

**Signature:**
```sql
doc_blocks_reorder(
    blocks LIST(doc_element)
) → LIST(doc_element)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |

**Returns:** Elements with element_order renumbered as 0, 1, 2, ...

**Example:**
```sql
-- Fix gaps in element_order after filtering
SELECT doc_blocks_reorder(
    doc_blocks_filter(blocks, ['heading', 'paragraph'])
);
```

---

### doc_blocks_slice

Extract a contiguous range of elements.

**Signature:**
```sql
doc_blocks_slice(
    blocks LIST(doc_element),
    start_order INTEGER,
    end_order INTEGER
) → LIST(doc_element)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |
| `start_order` | INTEGER | Starting element_order (inclusive) |
| `end_order` | INTEGER | Ending element_order (inclusive) |

**Returns:** Elements within the specified range.

**Example:**
```sql
-- Extract elements 5 through 10
SELECT doc_blocks_slice(blocks, 5, 10);
```

---

### doc_blocks_transform

Apply transformations to element types and content.

**Signature:**
```sql
doc_blocks_transform(
    blocks LIST(doc_element),
    type_mapping MAP(VARCHAR, VARCHAR),
    content_fn VARCHAR DEFAULT NULL
) → LIST(doc_element)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |
| `type_mapping` | MAP(VARCHAR, VARCHAR) | Map of old_type → new_type |
| `content_fn` | VARCHAR | Optional: SQL expression for content transformation |

**Returns:** Elements with transformed types and optionally transformed content.

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

Extract plain text content from elements.

**Signature:**
```sql
doc_blocks_to_text(
    blocks LIST(doc_element),
    separator VARCHAR DEFAULT '\n\n'
) → VARCHAR
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |
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
    blocks LIST(doc_element)
) → TABLE(level INTEGER, title VARCHAR, id VARCHAR, element_order INTEGER)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |

**Returns:** Table of headings with level, title, id attribute, and position.

**Example:**
```sql
-- Get all headings
SELECT * FROM doc_blocks_headings(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b)
);
```

**Output:**
| level | title | id | element_order |
|-------|-------|-----|---------------|
| 1 | Introduction | introduction | 0 |
| 2 | Getting Started | getting-started | 3 |
| 2 | Examples | examples | 8 |

---

### doc_blocks_toc

Generate a table of contents from headings.

**Signature:**
```sql
doc_blocks_toc(
    blocks LIST(doc_element),
    min_level INTEGER DEFAULT 1,
    max_level INTEGER DEFAULT 6
) → TABLE(level INTEGER, title VARCHAR, id VARCHAR, indent VARCHAR, element_order INTEGER)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |
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
    blocks LIST(doc_element)
) → TABLE(language VARCHAR, content VARCHAR, info_string VARCHAR, element_order INTEGER)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |

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
    blocks LIST(doc_element)
) → TABLE(text VARCHAR, url VARCHAR, title VARCHAR, element_order INTEGER)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |

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

Check elements for schema compliance.

**Signature:**
```sql
doc_blocks_validate(
    blocks LIST(doc_element)
) → STRUCT(valid BOOLEAN, errors LIST(VARCHAR))
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |

**Returns:** Struct with validation result and list of errors.

**Validation checks:**
- kind is 'block' or 'inline'
- element_type is non-empty
- encoding is valid ('text', 'json', 'yaml', 'html', 'xml')
- JSON content is valid when encoding='json'
- YAML content is valid when encoding='yaml'
- element_order is non-negative
- level is NULL or non-negative (for blocks), >= 1 (for inlines)

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
    blocks LIST(doc_element)
) → TABLE(severity VARCHAR, message VARCHAR, element_order INTEGER, suggestion VARCHAR)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |

**Returns:** Table of issues with severity, message, location, and suggestions.

**Severity levels:**
- `error` - Critical issues that may cause problems
- `warning` - Potential issues or non-standard patterns
- `info` - Suggestions for improvement

**Checks performed:**
| Severity | Issue |
|----------|-------|
| error | Duplicate element_order values |
| warning | Heading level skipped (e.g., h1 → h3) |
| warning | Empty content in non-hr/non-image blocks |
| warning | Unknown element_type (not core, not namespaced) |
| warning | Code block without language |
| info | Large gaps in element_order |

**Example:**
```sql
-- Show all errors and warnings
SELECT * FROM doc_blocks_lint(blocks)
WHERE severity IN ('error', 'warning');
```

---

### doc_blocks_stats

Get element type statistics.

**Signature:**
```sql
doc_blocks_stats(
    blocks LIST(doc_element)
) → TABLE(element_type VARCHAR, count INTEGER, avg_content_length DOUBLE, total_content_length BIGINT)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |

**Returns:** Aggregated statistics per element type.

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
    blocks LIST(doc_element)
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
| `blocks` | LIST(doc_element) | Input element sequence |

**Returns:** Struct summarizing document structure.

**Example:**
```sql
SELECT (doc_blocks_structure(blocks)).*;
```

---

## Conversion Helper Functions

### doc_blocks_set_source

Add or update source_format on all elements.

**Signature:**
```sql
doc_blocks_set_source(
    blocks LIST(doc_element),
    format VARCHAR
) → LIST(doc_element_ext)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |
| `format` | VARCHAR | Source format identifier |

**Returns:** Extended elements with source_format set.

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

Convert all elements to core types only.

**Signature:**
```sql
doc_blocks_normalize(
    blocks LIST(doc_element)
) → LIST(doc_element)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |

**Returns:** Elements with namespaced types converted to nearest core type.

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
    blocks LIST(doc_element),
    mapping MAP(VARCHAR, VARCHAR)
) → LIST(doc_element)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_element) | Input element sequence |
| `mapping` | MAP(VARCHAR, VARCHAR) | Type transformation map |

**Returns:** Elements with mapped types (unmapped types unchanged).

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

Aggregate rows into an element list (for use with GROUP BY).

**Signature:**
```sql
doc_blocks_agg(
    element_type VARCHAR,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR,
    attributes MAP(VARCHAR, VARCHAR),
    element_order INTEGER
) → LIST(doc_element)
```

**Example:**
```sql
-- Collect elements by file
SELECT
    file_path,
    doc_blocks_agg(element_type, content, level, encoding, attributes, element_order) as blocks
FROM read_markdown_blocks('docs/**/*.md', include_filepath := true)
GROUP BY file_path;
```

---

## See Also

- [Design Document](design.md) - Architecture and implementation details
- [Duck Blocks Specification](duck_blocks_spec.md) - Unified doc_element type schema
- [DuckDB Markdown Extension](https://github.com/teaguesterling/duckdb_markdown) - Reference implementation
