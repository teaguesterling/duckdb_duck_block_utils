# Duck Block Utils - Design Document

## Overview

`duck_block_utils` is a DuckDB extension providing format-agnostic utilities for manipulating document elements. It operates on data conforming to the [Document Block Specification](https://github.com/teaguesterling/duckdb_markdown/blob/main/docs/doc_block_spec.md).

## Goals

1. **Format Independence**: Work with elements from any source (markdown, HTML, XML, YAML, Pandoc)
2. **SQL-Native**: Integrate naturally with DuckDB's query patterns
3. **Composable**: Functions that chain together for complex transformations
4. **Lightweight**: Minimal dependencies, fast execution
5. **Interoperability**: Enable cross-format document workflows

## Non-Goals

- Format-specific parsing (handled by dedicated extensions)
- File I/O (use DuckDB's native capabilities)
- Complex document rendering (handled by format extensions)

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    User SQL Queries                          │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                  duck_block_utils Extension                  │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ Manipulation│  │  Extraction │  │    Validation       │  │
│  │  Functions  │  │  Functions  │  │    Functions        │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│              Pandoc AST Conversion Layer                     │
│     (JSON AST ↔ duck_blocks, no Pandoc dependency)         │
├─────────────────────────────────────────────────────────────┤
│                    Core Element Types                        │
│              (duck_block STRUCT handling)                   │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                Format-Specific Extensions                    │
│   ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│   │ markdown │  │  webbed  │  │   yaml   │  │  panduck │   │
│   └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### Key Design Decision: Pandoc AST in Utils

The Pandoc JSON AST conversion lives in `duck_block_utils` (not `panduck`) because:

1. **No external dependency**: Pure JSON ↔ struct transformation
2. **Enables interop**: Any tool producing Pandoc JSON can integrate
3. **Separation of concerns**: panduck focuses on Pandoc binary integration
4. **Reusability**: Other extensions can leverage the conversion

See [pandoc_ast_spec.md](pandoc_ast_spec.md) for detailed conversion rules.

## Data Model

### Unified duck_block Type

Both block-level and inline elements use the same unified `duck_block` type, distinguished by the `kind` field:

```sql
STRUCT(
    kind VARCHAR,                           -- 'block' or 'inline'
    element_type VARCHAR,                   -- Element type identifier
    content VARCHAR,                        -- Text content
    level INTEGER,                          -- Semantic level (heading level, nesting depth)
    encoding VARCHAR,                       -- Content encoding hint
    attributes MAP(VARCHAR, VARCHAR),       -- Key-value metadata
    element_order INTEGER                   -- Position in sequence
)
```

### Kind Values

- `'block'`: Block-level elements (heading, paragraph, code, list, etc.)
- `'inline'`: Inline elements (text, bold, italic, link, etc.)

### Input Handling

Functions accept elements in two forms:

1. **LIST of STRUCTs**: `LIST(duck_block)` - for aggregate operations
2. **Individual rows**: Via table functions that process row-by-row

### Type Registration

```sql
-- Register the duck_block type on extension load
CREATE TYPE duck_block AS STRUCT(
    kind VARCHAR,
    element_type VARCHAR,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR,
    attributes MAP(VARCHAR, VARCHAR),
    element_order INTEGER
);

-- Extended version with provenance
CREATE TYPE duck_block_ext AS STRUCT(
    kind VARCHAR,
    element_type VARCHAR,
    content VARCHAR,
    level INTEGER,
    encoding VARCHAR,
    attributes MAP(VARCHAR, VARCHAR),
    element_order INTEGER,
    source_format VARCHAR,
    file_path VARCHAR
);
```

## Function Categories

### 1. Manipulation Functions

Transform element sequences without parsing content.

#### duck_blocks_filter
```cpp
// Signature
LIST(duck_block) duck_blocks_filter(LIST(duck_block) blocks, VARCHAR[] types)

// Implementation
- Iterate through blocks
- Keep blocks where element_type IN types
- Preserve element_order values
```

#### duck_blocks_exclude
```cpp
// Signature
LIST(duck_block) duck_blocks_exclude(LIST(duck_block) blocks, VARCHAR[] types)

// Implementation
- Iterate through blocks
- Keep blocks where element_type NOT IN types
```

#### duck_blocks_merge
```cpp
// Signature
LIST(duck_block) duck_blocks_merge(LIST(duck_block) blocks1, LIST(duck_block) blocks2)

// Implementation
- Concatenate blocks2 after blocks1
- Renumber element_order: blocks2 orders += max(blocks1 orders) + 1
```

#### duck_blocks_reorder
```cpp
// Signature
LIST(duck_block) duck_blocks_reorder(LIST(duck_block) blocks)

// Implementation
- Sort by current element_order
- Reassign element_order as 0, 1, 2, ...
```

#### db_blocks_transform
```cpp
// Signature
LIST(duck_block) db_blocks_transform(
    LIST(duck_block) blocks,
    MAP(VARCHAR, VARCHAR) type_mapping,     -- old_type -> new_type
    MAP(VARCHAR, VARCHAR) content_mapping   -- optional content transforms
)

// Implementation
- For each block:
  - If element_type in type_mapping, replace with mapped value
  - Apply content transformations if specified
```

### 2. Extraction Functions

Extract specific information from elements.

#### duck_blocks_to_text
```cpp
// Signature
VARCHAR duck_blocks_to_text(LIST(duck_block) blocks)

// Implementation
- For each block:
  - If encoding = 'text': append content
  - If encoding = 'json': parse and extract text values
  - If encoding = 'yaml': parse and extract text values
- Join with newlines
- Skip 'hr', 'raw' blocks
```

#### duck_blocks_headings
```cpp
// Signature (returns table)
TABLE(level INT, title VARCHAR, id VARCHAR, element_order INT)
    duck_blocks_headings(LIST(duck_block) blocks)

// Implementation
- Filter to element_type = 'heading'
- Return level, content as title, attributes['id'], element_order
```

#### duck_blocks_toc
```cpp
// Signature (returns table)
TABLE(level INT, title VARCHAR, id VARCHAR, indent VARCHAR, element_order INT)
    duck_blocks_toc(LIST(duck_block) blocks)

// Implementation
- Call duck_blocks_headings
- Add indent column: repeat('  ', level - 1)
- Optionally generate IDs from titles if missing
```

#### duck_blocks_code_blocks
```cpp
// Signature (returns table)
TABLE(language VARCHAR, content VARCHAR, info_string VARCHAR, element_order INT, file_path VARCHAR)
    duck_blocks_code_blocks(LIST(duck_block) blocks)

// Implementation
- Filter to element_type = 'code'
- Extract language from attributes['language']
- Include file_path if present
```

#### duck_blocks_links
```cpp
// Signature (returns table)
TABLE(text VARCHAR, url VARCHAR, title VARCHAR, element_order INT)
    duck_blocks_links(LIST(duck_block) blocks)

// Implementation
- Scan content of 'paragraph', 'list' blocks for markdown links
- Parse [text](url "title") patterns
- Return extracted links
```

### 3. Validation Functions

Check conformance and quality.

#### duck_blocks_validate
```cpp
// Signature
STRUCT(valid BOOL, errors LIST(VARCHAR)) duck_blocks_validate(LIST(duck_block) blocks)

// Implementation
- Check each block:
  - kind is 'block' or 'inline'
  - element_type is non-empty VARCHAR
  - encoding is one of: 'text', 'json', 'yaml', 'html', 'xml', 'latex', 'markdown'
  - If encoding = 'json', content is valid JSON
  - If encoding = 'yaml', content is valid YAML
  - element_order is non-negative integer
  - level is NULL or positive integer (0 for metadata)
- Return aggregated results
```

#### duck_blocks_lint
```cpp
// Signature (returns table)
TABLE(severity VARCHAR, message VARCHAR, element_order INT, suggestion VARCHAR)
    duck_blocks_lint(LIST(duck_block) blocks)

// Implementation
Checks:
- 'warning': Heading levels skip (h1 -> h3)
- 'warning': Empty content in non-hr blocks
- 'warning': Unknown element_type (not core, not namespaced)
- 'error': Duplicate element_order values
- 'warning': Missing language on code blocks
- 'info': Large element_order gaps
```

#### duck_blocks_stats
```cpp
// Signature
TABLE(element_type VARCHAR, count INT, avg_content_length FLOAT)
    duck_blocks_stats(LIST(duck_block) blocks)

// Implementation
- Group by element_type
- Count occurrences
- Calculate average content length
```

### 4. Conversion Helpers

Facilitate format conversion workflows.

#### db_blocks_set_source
```cpp
// Signature
LIST(duck_block_ext) db_blocks_set_source(LIST(duck_block) blocks, VARCHAR format)

// Implementation
- Add/set source_format field on all blocks
- Return extended element type
```

#### db_blocks_normalize
```cpp
// Signature
LIST(duck_block) db_blocks_normalize(LIST(duck_block) blocks)

// Implementation
- Convert namespaced types to nearest core type:
  - 'md:footnote' -> 'paragraph' (with attribute marker)
  - 'html:div' -> 'raw' (with format='html')
  - 'html:h1' -> 'heading' (level=1)
  - etc.
- Preserve original type in attributes['original_type']
```

#### db_blocks_map_types
```cpp
// Signature
LIST(duck_block) db_blocks_map_types(
    LIST(duck_block) blocks,
    MAP(VARCHAR, VARCHAR) mapping
)

// Implementation
- Apply type mapping: if element_type in mapping keys, replace with mapped value
- Leave unmapped types unchanged
```

## Implementation Strategy

### Phase 1: Core Infrastructure
- Extension scaffolding
- Type registration (duck_block, duck_block_ext)
- Basic manipulation: filter, exclude, merge, reorder

### Phase 2: Extraction Functions
- duck_blocks_to_text
- duck_blocks_headings
- duck_blocks_toc
- duck_blocks_code_blocks

### Phase 3: Validation
- duck_blocks_validate
- duck_blocks_lint
- duck_blocks_stats

### Phase 4: Conversion Helpers
- db_blocks_set_source
- db_blocks_normalize
- db_blocks_map_types
- duck_blocks_links

### Phase 5: Advanced Features
- Content-aware transformations
- Cross-reference resolution
- Document diffing

## Dependencies

### Required
- DuckDB (>= 1.0.0)

### Optional
- yyjson (for JSON parsing in content extraction)
- yaml-cpp (for YAML parsing, if supporting YAML content)

## File Structure

```
duckdb_duck_block_utils/
├── CMakeLists.txt
├── Makefile
├── README.md
├── LICENSE
├── docs/
│   ├── design.md           # This document
│   └── api.md              # API reference
├── src/
│   ├── duck_block_utils_extension.cpp
│   ├── include/
│   │   ├── duck_block_utils.hpp
│   │   ├── block_types.hpp
│   │   ├── manipulation.hpp
│   │   ├── extraction.hpp
│   │   ├── validation.hpp
│   │   └── conversion.hpp
│   ├── block_types.cpp      # Type registration
│   ├── manipulation.cpp     # Filter, merge, etc.
│   ├── extraction.cpp       # to_text, headings, etc.
│   ├── validation.cpp       # validate, lint, stats
│   └── conversion.cpp       # normalize, map_types
└── test/
    └── sql/
        ├── manipulation.test
        ├── extraction.test
        ├── validation.test
        └── conversion.test
```

## Testing Strategy

### Unit Tests
- Each function tested in isolation
- Edge cases: empty lists, single blocks, malformed data

### Integration Tests
- Cross-extension workflows with duckdb_markdown
- Round-trip transformations

### Property-Based Tests
- merge(a, b) length = len(a) + len(b)
- filter then exclude = original (for complementary types)
- reorder preserves block count

## Performance Considerations

### Memory
- Avoid copying block content when possible
- Use string_view for content inspection
- Stream large block lists rather than materializing

### Parallelism
- Manipulation functions are embarrassingly parallel
- Validation can parallelize per-block checks
- Extraction may need sequential processing for some operations

## Future Considerations

### Potential Extensions
- **doc_blocks_diff**: Compare two block sequences
- **doc_blocks_patch**: Apply diff to blocks
- **doc_blocks_search**: Full-text search within blocks
- **doc_blocks_index**: Create searchable index

### Ecosystem Integration
- Shared type definitions across extensions
- Common test fixtures
- Unified documentation format
