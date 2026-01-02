# V5 Implementation Checklist

## Phase 1: Core Infrastructure
- [ ] Create `BuildWithContent` utility in `src/builders.cpp`
- [ ] Handle VARCHAR input case
- [ ] Handle duck_block input case
- [ ] Handle LIST(duck_block) input case
- [ ] Unit test the utility with all input types

## Phase 2: Block Builders

### Simple builders (no config params)
- [ ] `db_paragraph(content) → LIST`
- [ ] `db_hr() → LIST`
- [ ] `db_metadata(yaml) → LIST`

### Builders with config params (reorder)
- [ ] `db_heading(level, content) → LIST`
- [ ] `db_code(lang?, content) → LIST`
- [ ] `db_blockquote(level?, content) → LIST`
- [ ] `db_list_block(ordered?, items) → LIST`
- [ ] `db_raw(format?, content) → LIST`

### Builders with multiple optional params
- [ ] `db_image(src, alt?, title?) → LIST`

### New builders
- [ ] `db_list_item(ordered?, content) → LIST`

## Phase 3: Inline Builders

### Simple inline builders
- [ ] `db_text(content) → LIST`
- [ ] `db_space() → LIST`
- [ ] `db_softbreak() → LIST`
- [ ] `db_linebreak() → LIST`
- [ ] `db_inline_code(content) → LIST`

### Formatting builders
- [ ] `db_bold(content) → LIST`
- [ ] `db_italic(content) → LIST`
- [ ] `db_strikethrough(content) → LIST`
- [ ] `db_superscript(content) → LIST`
- [ ] `db_subscript(content) → LIST`
- [ ] `db_smallcaps(content) → LIST`
- [ ] `db_underline(content) → LIST`

### Semantic builders with config params
- [ ] `db_link(href, title?, content) → LIST`
- [ ] `db_inline_image(src, alt?, title?) → LIST`
- [ ] `db_math(display?, content) → LIST`
- [ ] `db_quoted(quote_type?, content) → LIST`
- [ ] `db_cite(key, prefix?, suffix?) → LIST`
- [ ] `db_note(content) → LIST`
- [ ] `db_span(id?, class?, content) → LIST`
- [ ] `db_raw_inline(format?, content) → LIST`

## Phase 4: Assembly Functions
- [ ] `db_assemble(LIST(LIST(duck_block))) → LIST(duck_block)`
- [ ] `db_document(LIST(LIST(duck_block))) → LIST(duck_block)`
- [ ] `db_section(level, title, children?) → LIST(duck_block)`
- [ ] `db_rebase_levels` - verify works with new format
- [ ] `db_concat` - verify works with new format

## Phase 5: Validation
- [ ] Run `test/sql/builders_v2.test`
- [ ] Run `test/sql/inline_builders_v2.test`
- [ ] Run `test/sql/assembly_v2.test`
- [ ] Run `test/sql/extraction_v2.test`
- [ ] Run `test/sql/validation_v2.test`
- [ ] Run `test/sql/pandoc_blocks_v2.test`

## Phase 6: Cleanup
- [ ] Remove old function overloads (or keep for backwards compatibility?)
- [ ] Update old test files or remove them
- [ ] Final documentation review

## Notes

### BuildWithContent Implementation Sketch

```cpp
static vector<Value> BuildWithContent(
    const Value &parent_block,
    const Value &content_input,
    int32_t base_level = 1
) {
    vector<Value> result;

    // Get parent block children for modification
    auto parent_children = StructValue::GetChildren(parent_block);
    parent_children[BlockTypes::LEVEL_IDX] = Value(base_level);
    parent_children[BlockTypes::ELEMENT_ORDER_IDX] = Value(0);

    if (content_input.IsNull()) {
        // NULL content - keep parent content as-is
        result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));
    }
    else if (content_input.type().id() == LogicalTypeId::VARCHAR) {
        // VARCHAR content - set content field
        parent_children[BlockTypes::CONTENT_IDX] = content_input;
        result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));
    }
    else if (content_input.type().id() == LogicalTypeId::STRUCT) {
        // Single duck_block child
        parent_children[BlockTypes::CONTENT_IDX] = Value();  // NULL
        result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));

        // Add child at level+1
        auto child_children = StructValue::GetChildren(content_input);
        child_children[BlockTypes::LEVEL_IDX] = Value(base_level + 1);
        child_children[BlockTypes::ELEMENT_ORDER_IDX] = Value(0);
        result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(child_children)));
    }
    else if (content_input.type().id() == LogicalTypeId::LIST) {
        // LIST(duck_block) children
        parent_children[BlockTypes::CONTENT_IDX] = Value();  // NULL
        result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));

        // Add children at level+1 with sequential order
        auto &children = ListValue::GetChildren(content_input);
        int32_t child_order = 0;
        for (auto &child : children) {
            if (!child.IsNull()) {
                auto child_children = StructValue::GetChildren(child);
                child_children[BlockTypes::LEVEL_IDX] = Value(base_level + 1);
                child_children[BlockTypes::ELEMENT_ORDER_IDX] = Value(child_order++);
                result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(child_children)));
            }
        }
    }

    return result;
}
```

### Handling duck_block_content Union Type

DuckDB doesn't have a true union type, but we can use function overloading:

1. `db_heading(INTEGER, VARCHAR) → LIST(duck_block)` - text content
2. `db_heading(INTEGER, duck_block) → LIST(duck_block)` - single child
3. `db_heading(INTEGER, LIST(duck_block)) → LIST(duck_block)` - multiple children

The `BuildWithContent` helper inspects the actual type at runtime and handles appropriately.
