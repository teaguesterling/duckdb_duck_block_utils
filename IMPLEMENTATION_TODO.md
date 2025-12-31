# Duck Block Utils - Implementation Guide

Comprehensive guide for implementing the `duck_block_utils` DuckDB extension. This file consolidates all design decisions, patterns, and tasks.

## Quick Reference

**Related Documentation:**
- `docs/api.md` - Complete function signatures
- `docs/design.md` - Architecture overview
- `docs/implementation_notes.md` - DuckDB-specific patterns
- `docs/block_builders.md` - Builder function design
- `docs/pandoc_ast_spec.md` - Pandoc AST conversion

**Reference Implementation:**
- `../duckdb_markdown/` - Working extension with similar patterns
- Key files: `markdown_types.cpp`, `markdown_copy.cpp`, `markdown_scalar_functions.cpp`

---

## Core Type: doc_block

```sql
STRUCT(
    block_type VARCHAR,                    -- 'heading', 'paragraph', 'code', etc.
    content VARCHAR,                       -- Primary content
    level INTEGER,                         -- Hierarchy level (NULL if N/A)
    encoding VARCHAR,                      -- 'text', 'json', 'yaml', 'html', 'xml'
    attributes MAP(VARCHAR, VARCHAR),      -- Type-specific metadata
    block_order INTEGER                    -- Position in document (0-indexed)
)
```

**Core block types:** `heading`, `paragraph`, `code`, `blockquote`, `list`, `table`, `hr`, `metadata`, `image`, `raw`

---

## Implementation Phases

### Phase 1: Foundation (Start Here)

#### 1.1 Type Registration

**File:** `src/block_types.cpp`

```cpp
#include "block_types.hpp"

namespace duckdb {

LogicalType BlockTypes::DocBlockType() {
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("block_type", LogicalType::VARCHAR));
    struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
    struct_children.push_back(make_pair("level", LogicalType::INTEGER));
    struct_children.push_back(make_pair("encoding", LogicalType::VARCHAR));
    struct_children.push_back(make_pair("attributes",
        LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)));
    struct_children.push_back(make_pair("block_order", LogicalType::INTEGER));

    auto block_type = LogicalType::STRUCT(std::move(struct_children));
    block_type.SetAlias("doc_block");
    return block_type;
}

void BlockTypes::Register(ExtensionLoader &loader) {
    loader.RegisterType("doc_block", DocBlockType());
    loader.RegisterType("doc_block_ext", DocBlockExtType());  // With source_format, file_path
}

} // namespace duckdb
```

**Tasks:**
- [ ] Create `src/include/block_types.hpp` header
- [ ] Create `src/block_types.cpp` implementation
- [ ] Register types in extension loader
- [ ] Write basic type tests

#### 1.2 Extension Entry Point

**File:** `src/duck_block_utils_extension.cpp`

```cpp
#define DUCKDB_EXTENSION_MAIN
#include "duck_block_utils_extension.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
    // Phase 1
    BlockTypes::Register(loader);
    RegisterManipulationFunctions(loader);

    // Phase 2+
    // RegisterBuilderFunctions(loader);
    // RegisterExtractionFunctions(loader);
    // RegisterValidationFunctions(loader);
    // RegisterPandocASTFunctions(loader);
}

void DuckBlockUtilsExtension::Load(ExtensionLoader &loader) {
    LoadInternal(loader);
}

} // namespace duckdb
```

**Tasks:**
- [ ] Update extension entry point
- [ ] Add include guards and headers
- [ ] Verify extension loads cleanly

#### 1.3 Basic Manipulation Functions

**File:** `src/manipulation.cpp`

**Functions to implement:**

| Function | Signature | Priority |
|----------|-----------|----------|
| `doc_blocks_filter` | `(LIST(doc_block), VARCHAR[]) → LIST(doc_block)` | HIGH |
| `doc_blocks_exclude` | `(LIST(doc_block), VARCHAR[]) → LIST(doc_block)` | HIGH |
| `doc_blocks_merge` | `(LIST(doc_block), LIST(doc_block)) → LIST(doc_block)` | HIGH |
| `doc_blocks_reorder` | `(LIST(doc_block)) → LIST(doc_block)` | MEDIUM |
| `doc_blocks_slice` | `(LIST(doc_block), INTEGER, INTEGER) → LIST(doc_block)` | MEDIUM |

**Pattern for LIST-returning scalar:**

```cpp
void DocBlocksFilterFun(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &blocks_vec = args.data[0];  // LIST(doc_block)
    auto &types_vec = args.data[1];   // LIST(VARCHAR)

    for (idx_t i = 0; i < args.size(); i++) {
        auto blocks_list = ListValue::GetChildren(blocks_vec.GetValue(i));
        auto types_list = ListValue::GetChildren(types_vec.GetValue(i));

        // Convert types to set for O(1) lookup
        unordered_set<string> type_set;
        for (auto &t : types_list) {
            type_set.insert(t.GetValue<string>());
        }

        // Filter blocks
        vector<Value> filtered;
        for (auto &block : blocks_list) {
            auto block_type = StructValue::GetChildren(block)[0].GetValue<string>();
            if (type_set.count(block_type)) {
                filtered.push_back(block);
            }
        }

        result.SetValue(i, Value::LIST(BlockTypes::DocBlockType(), filtered));
    }
}
```

**Tasks:**
- [ ] Create `src/include/manipulation.hpp`
- [ ] Implement `doc_blocks_filter`
- [ ] Implement `doc_blocks_exclude`
- [ ] Implement `doc_blocks_merge`
- [ ] Implement `doc_blocks_reorder`
- [ ] Implement `doc_blocks_slice`
- [ ] Write tests for each function

---

### Phase 2: Atomic Builder Functions

**File:** `src/builders.cpp`

**Functions to implement:**

| Function | Signature | Output |
|----------|-----------|--------|
| `doc_heading` | `(VARCHAR, INTEGER) → doc_block` | `{block_type: 'heading', ...}` |
| `doc_paragraph` | `(VARCHAR) → doc_block` | `{block_type: 'paragraph', ...}` |
| `doc_code` | `(VARCHAR, VARCHAR?) → doc_block` | Language in attributes |
| `doc_blockquote` | `(VARCHAR) → doc_block` | |
| `doc_list_block` | `(VARCHAR[], BOOLEAN?) → doc_block` | JSON-encoded content |
| `doc_table_block` | `(VARCHAR[], VARCHAR[][]) → doc_block` | JSON-encoded content |
| `doc_hr` | `() → doc_block` | Empty content |
| `doc_metadata` | `(VARCHAR) → doc_block` | YAML content |
| `doc_image` | `(VARCHAR, VARCHAR, VARCHAR?) → doc_block` | url, alt, title |
| `doc_raw` | `(VARCHAR, VARCHAR) → doc_block` | format, content |

**Pattern for atomic constructor:**

```cpp
void DocHeadingFun(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &content_vec = args.data[0];
    auto &level_vec = args.data[1];

    auto count = args.size();
    auto &result_entries = StructVector::GetEntries(result);

    for (idx_t i = 0; i < count; i++) {
        auto content = content_vec.GetValue(i);
        auto level = level_vec.GetValue(i);

        // Set struct fields
        result_entries[0]->SetValue(i, Value("heading"));     // block_type
        result_entries[1]->SetValue(i, content);               // content
        result_entries[2]->SetValue(i, level);                 // level
        result_entries[3]->SetValue(i, Value("text"));         // encoding
        result_entries[4]->SetValue(i, Value::MAP(LogicalType::MAP(
            LogicalType::VARCHAR, LogicalType::VARCHAR), {})); // attributes
        result_entries[5]->SetValue(i, Value(0));              // block_order
    }
}
```

**Tasks:**
- [ ] Create `src/include/builders.hpp`
- [ ] Implement each atomic constructor
- [ ] Handle optional parameters (default values)
- [ ] Implement JSON encoding for list/table content
- [ ] Write tests

---

### Phase 3: Assembly Functions

**Key insight:** DuckDB doesn't support variadic functions. Use LIST parameters with SQL array syntax.

**API Pattern:**
```sql
-- User writes:
SELECT doc_assemble([
    doc_heading('Title', 1),
    doc_paragraph('Content'),
    doc_section('Intro', 2, [doc_paragraph('Nested')])
]);

-- doc_section returns LIST(doc_block), doc_assemble flattens everything
```

**Functions:**

| Function | Signature | Description |
|----------|-----------|-------------|
| `doc_assemble` | `(LIST(doc_block OR LIST)) → LIST(doc_block)` | Flatten + reorder |
| `doc_section` | `(VARCHAR, INTEGER, LIST(doc_block)) → LIST(doc_block)` | Heading + children |
| `doc_document` | `(LIST(doc_block)) → LIST(doc_block)` | Alias for assemble |
| `doc_rebase_levels` | `(LIST(doc_block), INTEGER) → LIST(doc_block)` | Adjust all levels |

**Flattening logic:**

```cpp
void FlattenBlocks(const Value &input, vector<Value> &output, int &order) {
    if (input.type().id() == LogicalTypeId::STRUCT) {
        // Single block - update block_order and add
        auto children = StructValue::GetChildren(input);
        children[5] = Value(order++);  // Update block_order
        output.push_back(Value::STRUCT(children));
    } else if (input.type().id() == LogicalTypeId::LIST) {
        // List - recurse into children
        for (auto &child : ListValue::GetChildren(input)) {
            FlattenBlocks(child, output, order);
        }
    }
}
```

**Tasks:**
- [ ] Implement `doc_assemble` with flattening
- [ ] Implement `doc_section` (returns [heading, ...children])
- [ ] Implement `doc_document` (alias or wrapper)
- [ ] Implement `doc_rebase_levels`
- [ ] Write tests for nested structures

---

### Phase 4: Extraction Functions

**File:** `src/extraction.cpp`

| Function | Return Type | Description |
|----------|-------------|-------------|
| `doc_blocks_to_text` | `VARCHAR` | Plain text concatenation |
| `doc_blocks_headings` | `LIST(STRUCT)` | Extract heading info |
| `doc_blocks_toc` | `LIST(STRUCT)` | TOC with indentation |
| `doc_blocks_code_blocks` | `LIST(STRUCT)` | Code with language |
| `doc_blocks_links` | `LIST(STRUCT)` | Links from content |

**Tasks:**
- [ ] Create `src/include/extraction.hpp`
- [ ] Implement `doc_blocks_to_text`
- [ ] Implement `doc_blocks_headings`
- [ ] Implement `doc_blocks_toc`
- [ ] Implement `doc_blocks_code_blocks`
- [ ] Implement `doc_blocks_links` (requires parsing markdown inline syntax)
- [ ] Write tests

---

### Phase 5: Validation Functions

**File:** `src/validation.cpp`

| Function | Return Type | Description |
|----------|-------------|-------------|
| `doc_blocks_validate` | `STRUCT(valid, errors[])` | Schema compliance |
| `doc_blocks_lint` | `LIST(STRUCT)` | Best practice checks |
| `doc_blocks_stats` | `LIST(STRUCT)` | Per-type statistics |
| `doc_blocks_structure` | `STRUCT` | Document summary |

**Validation checks:**
- block_type is non-empty
- encoding is valid ('text', 'json', 'yaml', 'html', 'xml')
- JSON content parses when encoding='json'
- block_order is non-negative
- No duplicate block_order values

**Lint checks:**
- Heading level skips (h1 → h3)
- Empty content (except hr/image)
- Code blocks without language
- Large gaps in block_order

**Tasks:**
- [ ] Create `src/include/validation.hpp`
- [ ] Implement `doc_blocks_validate`
- [ ] Implement `doc_blocks_lint`
- [ ] Implement `doc_blocks_stats`
- [ ] Implement `doc_blocks_structure`
- [ ] Write tests

---

### Phase 6: Pandoc AST Conversion

**File:** `src/pandoc_ast.cpp`

**Requires:** JSON parsing (yyjson recommended, or DuckDB's JSON extension)

| Function | Description |
|----------|-------------|
| `pandoc_ast_to_blocks` | Convert Pandoc JSON AST → LIST(doc_block) |
| `pandoc_blocks_to_ast` | Convert LIST(doc_block) → Pandoc JSON AST |
| `pandoc_inlines_to_text` | Convert inline array to plain text |

**Pandoc AST Structure:**
```json
{
  "pandoc-api-version": [1, 23],
  "meta": {},
  "blocks": [
    {"t": "Header", "c": [1, ["id", [], []], [{"t": "Str", "c": "Title"}]]},
    {"t": "Para", "c": [{"t": "Str", "c": "Content"}]}
  ]
}
```

**Block type mapping:**
- `Header` → `heading` (level from first element)
- `Para` → `paragraph`
- `CodeBlock` → `code` (language from attributes)
- `BlockQuote` → `blockquote`
- `BulletList` / `OrderedList` → `list` (JSON-encoded)
- `Table` → `table` (JSON-encoded)
- `HorizontalRule` → `hr`

See `docs/pandoc_ast_spec.md` for complete mapping.

**Tasks:**
- [ ] Decide on JSON library (yyjson vs DuckDB JSON)
- [ ] Create `src/include/pandoc_ast.hpp`
- [ ] Implement `pandoc_ast_to_blocks`
- [ ] Implement `pandoc_blocks_to_ast`
- [ ] Implement `pandoc_inlines_to_text`
- [ ] Write round-trip tests

---

## File Structure

```
src/
├── duck_block_utils_extension.cpp    # Entry point
├── include/
│   ├── duck_block_utils_extension.hpp
│   ├── block_types.hpp
│   ├── manipulation.hpp
│   ├── builders.hpp
│   ├── extraction.hpp
│   ├── validation.hpp
│   └── pandoc_ast.hpp
├── block_types.cpp
├── manipulation.cpp
├── builders.cpp
├── extraction.cpp
├── validation.cpp
└── pandoc_ast.cpp

test/sql/
├── types.test
├── manipulation.test
├── builders.test
├── extraction.test
├── validation.test
└── pandoc_ast.test
```

---

## Open Questions

### 1. JSON Parsing
**Decision needed:** Bundle yyjson, or require DuckDB's JSON extension?
- **yyjson**: Self-contained, ~50KB, fast
- **JSON extension**: External dependency, but consistent with DuckDB ecosystem

**Recommendation:** Bundle yyjson for Phase 6 (Pandoc), make it optional (feature flag) initially.

### 2. Type Conflicts
**Issue:** duckdb_markdown has `markdown_doc_block` with same schema.

**Resolution:** Use `doc_block` as the generic name. They're structurally compatible.

### 3. Lambda Functions
**Issue:** `doc_from_rows(query, row -> ...)` needs lambdas, but DuckDB support is limited.

**Resolution:** Skip for now. Users can use SQL patterns:
```sql
SELECT doc_assemble(list(doc_section(name, 2, [doc_paragraph(bio)])))
FROM people;
```

### 4. Error Handling
**Pattern:** Provide both strict and permissive versions:
- `doc_blocks_validate()` - returns struct with errors
- `doc_blocks_filter()` - NULL on invalid input
- Consider `try_*` variants later

---

## Testing Strategy

```sql
-- test/sql/manipulation.test
require duck_block_utils

# Test filter
query I
SELECT len(doc_blocks_filter(
    [
        {'block_type': 'heading', 'content': 'Title', 'level': 1,
         'encoding': 'text', 'attributes': MAP{}, 'block_order': 0}::doc_block,
        {'block_type': 'paragraph', 'content': 'Text', 'level': NULL,
         'encoding': 'text', 'attributes': MAP{}, 'block_order': 1}::doc_block
    ],
    ['heading']
));
----
1

# Test merge preserves order
query I
SELECT (doc_blocks_merge(
    [{'block_type': 'heading', 'content': 'A', 'level': 1,
      'encoding': 'text', 'attributes': MAP{}, 'block_order': 0}::doc_block],
    [{'block_type': 'paragraph', 'content': 'B', 'level': NULL,
      'encoding': 'text', 'attributes': MAP{}, 'block_order': 0}::doc_block]
)[1]).block_order;
----
1
```

---

## Dependencies

**Required:**
- DuckDB >= 1.0.0

**Optional (Phase 6):**
- yyjson (bundled) for Pandoc AST

---

## Getting Started

1. Start with Phase 1.1 (Type Registration)
2. Verify types work: `SELECT {'block_type': 'test', ...}::doc_block;`
3. Implement `doc_blocks_filter` as first function
4. Build incrementally, testing each function

**Reference:** Look at `../duckdb_markdown/src/markdown_types.cpp` for type registration patterns.
