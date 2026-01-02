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

## Core Type: duck_block

```sql
STRUCT(
    kind VARCHAR,                          -- 'block' or 'inline'
    element_type VARCHAR,                  -- 'heading', 'paragraph', 'code', 'text', 'link', etc.
    content VARCHAR,                       -- Primary content
    level INTEGER,                         -- Hierarchy level (NULL for blocks, >=1 for inlines)
    encoding VARCHAR,                      -- 'text', 'json', 'yaml', 'html', 'xml', 'markdown'
    attributes MAP(VARCHAR, VARCHAR),      -- Type-specific metadata (e.g., heading_level, href)
    element_order INTEGER                  -- Position in sequence (0-indexed)
)
```

**Block types (kind='block'):** `heading`, `paragraph`, `code`, `blockquote`, `list`, `table`, `hr`, `metadata`, `image`, `raw`

**Inline types (kind='inline'):** `text`, `space`, `softbreak`, `linebreak`, `bold`, `italic`, `strikethrough`, `link`, `code`, `image`, `quoted`, `span`, `note`, etc.

---

## Implementation Phases

### Phase 1: Foundation (Start Here)

#### 1.1 Type Registration

**File:** `src/block_types.cpp`

```cpp
#include "block_types.hpp"

namespace duckdb {

LogicalType BlockTypes::DuckBlockType() {
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("kind", LogicalType::VARCHAR));
    struct_children.push_back(make_pair("element_type", LogicalType::VARCHAR));
    struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
    struct_children.push_back(make_pair("level", LogicalType::INTEGER));
    struct_children.push_back(make_pair("encoding", LogicalType::VARCHAR));
    struct_children.push_back(make_pair("attributes",
        LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)));
    struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));

    auto duck_block_type = LogicalType::STRUCT(std::move(struct_children));
    duck_block_type.SetAlias("duck_block");
    return duck_block_type;
}

void BlockTypes::Register(ExtensionLoader &loader) {
    loader.RegisterType("duck_block", DuckBlockType());
    loader.RegisterType("duck_block_ext", DuckBlockExtType());  // With source_format, file_path
}

} // namespace duckdb
```

**Tasks:**
- [x] Create `src/include/block_types.hpp` header
- [x] Create `src/block_types.cpp` implementation
- [x] Register types in extension loader
- [x] Write basic type tests

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
- [x] Update extension entry point
- [x] Add include guards and headers
- [x] Verify extension loads cleanly

#### 1.3 Basic Manipulation Functions

**File:** `src/manipulation.cpp`

**Functions to implement:**

| Function | Signature | Priority |
|----------|-----------|----------|
| `db_blocks_filter` | `(LIST(duck_block), VARCHAR[]) → LIST(duck_block)` | HIGH |
| `db_blocks_exclude` | `(LIST(duck_block), VARCHAR[]) → LIST(duck_block)` | HIGH |
| `db_blocks_merge` | `(LIST(duck_block), LIST(duck_block)) → LIST(duck_block)` | HIGH |
| `db_blocks_reorder` | `(LIST(duck_block)) → LIST(duck_block)` | MEDIUM |
| `db_blocks_slice` | `(LIST(duck_block), INTEGER, INTEGER) → LIST(duck_block)` | MEDIUM |

**Pattern for LIST-returning scalar:**

```cpp
void DbBlocksFilterFun(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &blocks_vec = args.data[0];  // LIST(duck_block)
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
            auto block_type = StructValue::GetChildren(block)[1].GetValue<string>();
            if (type_set.count(block_type)) {
                filtered.push_back(block);
            }
        }

        result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), filtered));
    }
}
```

**Tasks:**
- [x] Create `src/include/manipulation.hpp`
- [x] Implement `db_blocks_filter`
- [x] Implement `db_blocks_exclude`
- [x] Implement `db_blocks_merge`
- [x] Implement `db_blocks_reorder`
- [x] Implement `db_blocks_slice`
- [x] Write tests for each function

---

### Phase 2: Atomic Builder Functions

**File:** `src/builders.cpp`

**Functions to implement:**

| Function | Signature | Output |
|----------|-----------|--------|
| `db_heading` | `(VARCHAR, INTEGER) → duck_block` | `{element_type: 'heading', ...}` |
| `db_paragraph` | `(VARCHAR) → duck_block` | `{element_type: 'paragraph', ...}` |
| `db_code` | `(VARCHAR, VARCHAR?) → duck_block` | Language in attributes |
| `db_blockquote` | `(VARCHAR) → duck_block` | |
| `db_list_block` | `(VARCHAR[], BOOLEAN?) → duck_block` | JSON-encoded content |
| `db_table_block` | `(VARCHAR[], VARCHAR[][]) → duck_block` | JSON-encoded content |
| `db_hr` | `() → duck_block` | Empty content |
| `db_metadata` | `(VARCHAR) → duck_block` | YAML content |
| `db_image` | `(VARCHAR, VARCHAR, VARCHAR?) → duck_block` | url, alt, title |
| `db_raw` | `(VARCHAR, VARCHAR) → duck_block` | format, content |

**Pattern for atomic constructor:**

```cpp
void DbHeadingFun(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &content_vec = args.data[0];
    auto &level_vec = args.data[1];

    auto count = args.size();
    auto &result_entries = StructVector::GetEntries(result);

    for (idx_t i = 0; i < count; i++) {
        auto content = content_vec.GetValue(i);
        auto level = level_vec.GetValue(i);

        // Set struct fields (7 fields: kind, element_type, content, level, encoding, attributes, element_order)
        result_entries[0]->SetValue(i, Value("block"));        // kind
        result_entries[1]->SetValue(i, Value("heading"));      // element_type
        result_entries[2]->SetValue(i, content);               // content
        result_entries[3]->SetValue(i, Value());               // level (NULL for blocks)
        result_entries[4]->SetValue(i, Value("text"));         // encoding
        result_entries[5]->SetValue(i, Value::MAP(LogicalType::MAP(
            LogicalType::VARCHAR, LogicalType::VARCHAR),
            {Value("heading_level")}, {level.ToString()}));    // attributes with heading_level
        result_entries[6]->SetValue(i, Value(0));              // element_order
    }
}
```

**Tasks:**
- [x] Create `src/include/builders.hpp`
- [x] Implement each atomic constructor
- [x] Handle optional parameters (default values)
- [x] Implement JSON encoding for list/table content
- [x] Write tests
- [x] Implement inline builders (`db_text`, `db_bold`, `db_italic`, etc.)
- [x] Implement flattening builder overloads

---

### Phase 3: Assembly Functions

**Key insight:** DuckDB doesn't support variadic functions. Use LIST parameters with SQL array syntax.

**API Pattern:**
```sql
-- User writes:
SELECT db_assemble([
    db_heading('Title', 1),
    db_paragraph('Content'),
    db_section('Intro', 2, [db_paragraph('Nested')])
]);

-- db_section returns LIST(duck_block), db_assemble flattens everything
```

**Functions:**

| Function | Signature | Description |
|----------|-----------|-------------|
| `db_assemble` | `(LIST(duck_block OR LIST)) → LIST(duck_block)` | Flatten + reorder |
| `db_section` | `(VARCHAR, INTEGER, LIST(duck_block)) → LIST(duck_block)` | Heading + children |
| `db_document` | `(LIST(duck_block)) → LIST(duck_block)` | Alias for assemble |
| `db_rebase_levels` | `(LIST(duck_block), INTEGER) → LIST(duck_block)` | Adjust all levels |

**Flattening logic:**

```cpp
void FlattenBlocks(const Value &input, vector<Value> &output, int &order) {
    if (input.type().id() == LogicalTypeId::STRUCT) {
        // Single block - update element_order and add
        auto children = StructValue::GetChildren(input);
        children[6] = Value(order++);  // Update element_order (index 6)
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
- [x] Implement `db_assemble` with flattening
- [x] Implement `db_section` (returns [heading, ...children])
- [x] Implement `db_document` (alias or wrapper)
- [x] Implement `db_rebase_levels`
- [x] Implement `db_concat`
- [x] Write tests for nested structures

---

### Phase 4: Extraction Functions

**File:** `src/extraction.cpp`

| Function | Return Type | Description |
|----------|-------------|-------------|
| `db_blocks_to_text` | `VARCHAR` | Plain text concatenation |
| `db_blocks_headings` | `LIST(STRUCT)` | Extract heading info |
| `db_blocks_toc` | `LIST(STRUCT)` | TOC with indentation |
| `db_blocks_code_blocks` | `LIST(STRUCT)` | Code with language |
| `db_blocks_links` | `LIST(STRUCT)` | Links from content |
| `db_blocks_stats` | `LIST(STRUCT)` | Per-type statistics |

**Tasks:**
- [x] Create `src/include/extraction.hpp`
- [x] Implement `db_blocks_to_text`
- [x] Implement `db_blocks_headings`
- [x] Implement `db_blocks_toc`
- [x] Implement `db_blocks_code_blocks`
- [x] Implement `db_blocks_links`
- [x] Implement `db_blocks_stats`
- [x] Write tests

---

### Phase 5: Validation Functions

**File:** `src/validation.cpp`

| Function | Return Type | Description |
|----------|-------------|-------------|
| `db_blocks_validate` | `STRUCT(valid, errors[])` | Schema compliance |
| `db_blocks_lint` | `LIST(STRUCT)` | Best practice checks |
| `db_blocks_structure` | `STRUCT` | Document summary |

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
- [x] Create `src/include/validation.hpp`
- [x] Implement `db_blocks_validate`
- [x] Implement `db_blocks_lint`
- [x] Implement `db_blocks_structure`
- [x] Write tests

---

### Phase 6: Pandoc AST Conversion

**File:** `src/pandoc_ast.cpp`

**Requires:** JSON parsing (yyjson recommended, or DuckDB's JSON extension)

| Function | Description |
|----------|-------------|
| `pandoc_ast_to_blocks` | Convert Pandoc JSON AST → LIST(duck_block) |
| `pandoc_blocks_to_ast` | Convert LIST(duck_block) → Pandoc JSON AST |
| `pandoc_inlines_to_text` | Convert inline array to plain text |
| `pandoc_inlines_to_db_inlines` | Convert Pandoc inline JSON → LIST(duck_block) |
| `db_inlines_to_pandoc` | Convert LIST(duck_block) → Pandoc inline JSON |

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
- [x] Decide on JSON library (using yyjson)
- [x] Create `src/include/pandoc_inline_convert.hpp`
- [x] Implement `pandoc_ast_to_blocks` (block-level conversion)
- [x] Implement `pandoc_blocks_to_ast` (block-level conversion)
- [x] Implement `pandoc_inlines_to_text`
- [x] Implement `pandoc_inlines_to_db_inlines`
- [x] Implement `db_inlines_to_pandoc`
- [x] Write inline conversion tests
- [x] Write block-level round-trip tests

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
**Issue:** duckdb_markdown has `markdown_duck_block` with same schema.

**Resolution:** Use `duck_block` as the generic name. They're structurally compatible.

### 3. Lambda Functions
**Issue:** `db_from_rows(query, row -> ...)` needs lambdas, but DuckDB support is limited.

**Resolution:** Skip for now. Users can use SQL patterns:
```sql
SELECT db_assemble(list(db_section(name, 2, [db_paragraph(bio)])))
FROM people;
```

### 4. Error Handling
**Pattern:** Provide both strict and permissive versions:
- `db_blocks_validate()` - returns struct with errors
- `db_blocks_filter()` - NULL on invalid input
- Consider `try_*` variants later

---

## Testing Strategy

```sql
-- test/sql/manipulation.test
require duck_block_utils

# Test filter
query I
SELECT len(db_blocks_filter(
    [
        {'kind': 'block', 'element_type': 'heading', 'content': 'Title', 'level': NULL,
         'encoding': 'text', 'attributes': MAP{'heading_level': '1'}, 'element_order': 0}::duck_block,
        {'kind': 'block', 'element_type': 'paragraph', 'content': 'Text', 'level': NULL,
         'encoding': 'text', 'attributes': MAP{}, 'element_order': 1}::duck_block
    ],
    ['heading']
));
----
1

# Test merge preserves order
query I
SELECT (db_blocks_merge(
    [{'kind': 'block', 'element_type': 'heading', 'content': 'A', 'level': NULL,
      'encoding': 'text', 'attributes': MAP{'heading_level': '1'}, 'element_order': 0}::duck_block],
    [{'kind': 'block', 'element_type': 'paragraph', 'content': 'B', 'level': NULL,
      'encoding': 'text', 'attributes': MAP{}, 'element_order': 0}::duck_block]
)[2]).element_order;
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
2. Verify types work: `SELECT db_heading('Test', 1);`
3. Implement `db_blocks_filter` as first function
4. Build incrementally, testing each function

**Reference:** Look at `../duckdb_markdown/src/markdown_types.cpp` for type registration patterns.
