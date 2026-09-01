# Implementation Notes

Practical guidance for implementing duck_block_utils, bridging the design documents with DuckDB extension patterns.

## Extension Structure (from template)

The extension template provides the scaffolding:

```cpp
// src/duck_block_utils_extension.cpp
#define DUCKDB_EXTENSION_MAIN
#include "duck_block_utils_extension.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
    // Register types
    BlockTypes::Register(loader);

    // Register functions by category
    RegisterManipulationFunctions(loader);
    RegisterExtractionFunctions(loader);
    RegisterValidationFunctions(loader);
    RegisterBuilderFunctions(loader);
    RegisterPandocASTFunctions(loader);
}

void DuckBlockUtilsExtension::Load(ExtensionLoader &loader) {
    LoadInternal(loader);
}

} // namespace duckdb
```

## Type Registration

Following the pattern from duckdb_markdown:

```cpp
// src/block_types.cpp
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
    // Register doc_block type
    loader.RegisterType("doc_block", DocBlockType());

    // Register extended version with provenance
    loader.RegisterType("doc_block_ext", DocBlockExtType());
}

} // namespace duckdb
```

### Open Question: Type Conflicts with duckdb_markdown

**Issue**: duckdb_markdown registers `markdown_doc_block`. If both extensions are loaded, should they share a type?

**Options**:
1. **Different names**: `doc_block` (utils) vs `markdown_doc_block` (markdown) - compatible but distinct
2. **Check if exists**: Skip registration if type already exists
3. **Shared definition**: Both extensions use identical struct, just different aliases

**Recommendation**: Use `doc_block` as the generic name. The schema is identical to `markdown_doc_block`, so they're structurally compatible even if registered separately.

## Scalar Function Patterns

### Simple Scalar (single block → single block)

```cpp
// Pattern for atomic constructors like duck_block_heading()
void DocHeadingFun(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &content_vec = args.data[0];
    auto &level_vec = args.data[1];

    // Result is STRUCT type
    auto &result_children = StructVector::GetEntries(result);

    BinaryExecutor::Execute<string_t, int32_t, bool>(
        content_vec, level_vec, result, args.size(),
        [&](string_t content, int32_t level) {
            // Build struct values
            result_children[0]->SetValue(i, Value("heading"));        // block_type
            result_children[1]->SetValue(i, Value(content));          // content
            result_children[2]->SetValue(i, Value(level));            // level
            result_children[3]->SetValue(i, Value("text"));           // encoding
            result_children[4]->SetValue(i, Value::MAP(...));         // attributes
            result_children[5]->SetValue(i, Value(0));                // block_order
            return true;
        });
}

void RegisterBuilderFunctions(ExtensionLoader &loader) {
    auto doc_block_type = BlockTypes::DocBlockType();

    // duck_block_heading(content, level) -> doc_block
    auto heading_func = ScalarFunction(
        "duck_block_heading",
        {LogicalType::VARCHAR, LogicalType::INTEGER},
        doc_block_type,
        DocHeadingFun
    );
    loader.RegisterFunction(heading_func);
}
```

### LIST-returning Scalar (for manipulation functions)

```cpp
// Pattern for functions like duck_blocks_filter
void DocBlocksFilterFun(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &blocks_vec = args.data[0];  // LIST(doc_block)
    auto &types_vec = args.data[1];   // LIST(VARCHAR)

    // Process each row
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

        result.SetValue(i, Value::LIST(filtered));
    }
}
```

## Handling "VARIADIC" Functions

**Issue**: DuckDB doesn't support true variadic functions. The API shows:
```sql
duck_blocks_document(child1, child2, child3, ...)  -- Not directly possible
```

**Solution**: Use LIST parameter with SQL-side array construction:

```sql
-- Actual API
duck_blocks_document([
    duck_block_heading('Title', 1),
    duck_block_paragraph('Content'),
    duck_block_section('Intro', 2, [...])
])

-- Or use helper that flattens nested lists
duck_blocks_assemble([
    duck_block_heading('Title', 1),
    duck_block_section('Intro', 2, [...])  -- Returns LIST, gets flattened
])
```

**Implementation**:
```cpp
// duck_blocks_assemble takes LIST(doc_block | LIST(doc_block)) and flattens
void DocAssembleFun(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input_vec = args.data[0];  // LIST of blocks or nested lists

    for (idx_t i = 0; i < args.size(); i++) {
        vector<Value> flattened;
        int order = 0;

        FlattenBlocks(input_vec.GetValue(i), flattened, order);

        result.SetValue(i, Value::LIST(flattened));
    }
}

void FlattenBlocks(const Value &input, vector<Value> &output, int &order) {
    if (input.type().id() == LogicalTypeId::STRUCT) {
        // Single block - add with updated order
        auto block = input;
        // Update block_order field
        output.push_back(block);
        order++;
    } else if (input.type().id() == LogicalTypeId::LIST) {
        // List of blocks or nested lists - recurse
        for (auto &child : ListValue::GetChildren(input)) {
            FlattenBlocks(child, output, order);
        }
    }
}
```

## Handling Lambda/Row Transformers

**Issue**: `db_from_rows(query, row -> ...)` uses lambdas, but DuckDB lambda support is limited.

**Options**:

### Option A: Table Function with Template String
```sql
-- Use format strings instead of lambdas
SELECT * FROM db_from_rows(
    (SELECT name, bio FROM people),
    template := 'duck_block_section({name}, 2, duck_block_paragraph({bio}))'
);
```
**Drawback**: Complex to parse/evaluate safely

### Option B: Macro-based Approach
```sql
-- User defines a macro, we call it per row
CREATE MACRO person_to_doc(name, bio) AS
    duck_block_section(name, 2, duck_block_paragraph(bio));

SELECT * FROM db_from_rows(
    (SELECT name, bio FROM people),
    macro := 'person_to_doc'
);
```
**Drawback**: Requires pre-defined macro

### Option C: SQL Expression Evaluation (Complex)
```cpp
// Evaluate SQL expression per row using DuckDB's expression evaluator
// This is complex but most flexible
```

### Option D: Skip Lambda API, Use SQL Patterns
```sql
-- Instead of db_from_rows, users write:
SELECT duck_blocks_assemble(list(
    duck_block_section(name, 2, [duck_block_paragraph(bio)])
)) FROM people;
```
**Recommendation**: Start with Option D (pure SQL patterns). Add Option B (macro support) if there's demand.

## Open Questions

### 1. JSON Parsing Dependency
**Question**: Should we bundle a JSON parser (yyjson, simdjson) or use DuckDB's JSON extension?

**Context**: Pandoc AST conversion and list/table content parsing require JSON handling.

**Options**:
- Require `json` extension to be loaded
- Bundle yyjson (lightweight, already used by some extensions)
- Use DuckDB's internal JSON utilities if available

### 2. Error Handling Strategy
**Question**: How should validation failures be reported?

**Options**:
- Return NULL on invalid input
- Throw exception (stops query)
- Return error struct `{valid: false, error: 'message'}`
- Use TRY_* variants for non-throwing versions

**Recommendation**: Provide both - strict versions that throw, and `try_*` versions that return NULL/error struct.

### 3. Table Function vs Scalar for Extraction
**Question**: Should `duck_blocks_headings()` be a table function or scalar returning LIST?

**Scalar (current design)**:
```sql
SELECT unnest(duck_blocks_headings(blocks)) FROM docs;
```

**Table function**:
```sql
SELECT * FROM duck_blocks_headings((SELECT blocks FROM docs WHERE id = 1));
```

**Recommendation**: Scalar returning LIST is more composable. Users can `unnest()` when needed.

### 4. Performance: Materialization vs Streaming
**Question**: Should LIST-returning functions materialize all blocks, or can we stream?

**Context**: Large documents might have thousands of blocks.

**Current approach**: Materialize (simpler, LIST semantics require it)

**Future optimization**: For table functions, could stream via standard table function state.

## File Organization

```
src/
├── duck_block_utils_extension.cpp    # Main entry point
├── include/
│   ├── duck_block_utils_extension.hpp
│   ├── block_types.hpp               # Type definitions
│   ├── manipulation.hpp              # Filter, merge, etc.
│   ├── extraction.hpp                # to_text, headings, etc.
│   ├── validation.hpp                # validate, lint
│   ├── builders.hpp                  # Atomic + nested constructors
│   └── pandoc_ast.hpp                # AST conversion
├── block_types.cpp
├── manipulation.cpp
├── extraction.cpp
├── validation.cpp
├── builders.cpp
└── pandoc_ast.cpp
```

## Implementation Phases (Updated)

### Phase 1: Foundation
1. Type registration (`doc_block`, `doc_block_ext`)
2. Basic manipulation: `filter`, `exclude`, `merge`, `reorder`
3. Core tests

### Phase 2: Builders (Atomic)
1. `duck_block_heading`, `duck_block_paragraph`, `duck_block_code`, `duck_block_list_block`, `db_table_block`
2. `duck_block_hr`, `duck_block_metadata`, `duck_block_image`, `duck_block_raw`

### Phase 3: Builders (Assembly)
1. `duck_blocks_assemble` with flattening logic
2. `duck_block_section` (heading + children)
3. `duck_blocks_rebase_levels`, `db_with_toc`

### Phase 4: Extraction
1. `duck_blocks_to_text`
2. `duck_blocks_headings`, `duck_blocks_toc`
3. `duck_blocks_code_blocks`, `duck_blocks_links`

### Phase 5: Validation
1. `duck_blocks_validate`
2. `duck_blocks_lint`
3. `duck_blocks_stats`

### Phase 6: Pandoc AST
1. `pandoc_ast_to_blocks` (JSON parsing)
2. `duck_blocks_to_pandoc_blocks` (JSON generation)
3. `pandoc_inlines_to_text`

### Phase 7: Query Transformers
1. `db_table_from_query` (requires introspection)
2. `db_list_from_column`
3. Consider macro-based row transformation

## Testing Strategy

```sql
-- test/sql/manipulation.test
require duck_block_utils

# Test filter
query I
SELECT len(duck_blocks_filter(
    [
        {'block_type': 'heading', 'content': 'Title', 'level': 1, 'encoding': 'text', 'attributes': MAP{}, 'block_order': 0},
        {'block_type': 'paragraph', 'content': 'Text', 'level': NULL, 'encoding': 'text', 'attributes': MAP{}, 'block_order': 1}
    ]::doc_block[],
    ['heading']
));
----
1
```

## Dependencies

**Required**:
- DuckDB >= 1.0.0

**Recommended**:
- yyjson or similar for JSON parsing (Pandoc AST functions)

**Optional**:
- yaml-cpp for YAML content validation (can defer to runtime check)
