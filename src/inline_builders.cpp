#include "inline_builders.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"

namespace duckdb {

// Helper to create an attributes MAP from a std::map
static Value CreateAttributesMap(const map<string, string> &attrs) {
	vector<Value> keys;
	vector<Value> values;
	for (auto &entry : attrs) {
		keys.push_back(Value(entry.first));
		values.push_back(Value(entry.second));
	}
	return Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values);
}

// Helper to flatten LIST(LIST(duck_block)) to LIST(duck_block)
static Value FlattenNestedList(const Value &nested_list) {
	vector<Value> result;
	if (!nested_list.IsNull()) {
		auto &outer_children = ListValue::GetChildren(nested_list);
		for (auto &inner_list : outer_children) {
			if (!inner_list.IsNull()) {
				auto &inner_children = ListValue::GetChildren(inner_list);
				for (auto &item : inner_children) {
					if (!item.IsNull()) {
						result.push_back(item);
					}
				}
			}
		}
	}
	return Value::LIST(BlockTypes::DuckBlockType(), std::move(result));
}

// Helper to set all struct fields for an inline element
static void SetInlineFields(vector<unique_ptr<Vector>> &entries, idx_t i, const char *element_type,
                            const Value &content, const char *encoding, const map<string, string> &attrs,
                            int32_t level = 1, int32_t element_order = 0) {
	entries[BlockTypes::KIND_IDX]->SetValue(i, Value(BlockTypes::KIND_INLINE));
	entries[BlockTypes::ELEMENT_TYPE_IDX]->SetValue(i, Value(element_type));
	entries[BlockTypes::CONTENT_IDX]->SetValue(i, content);
	entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value(level));
	entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(encoding));
	entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, CreateAttributesMap(attrs));
	entries[BlockTypes::ELEMENT_ORDER_IDX]->SetValue(i, Value(element_order));
}

Value InlineBuilderFunctions::CreateInline(const string &inline_type, const string &content,
                                           const map<string, string> &attributes, int32_t level, int32_t inline_order) {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_INLINE)));
	struct_values.push_back(make_pair("element_type", Value(inline_type)));
	struct_values.push_back(make_pair("content", Value(content)));
	struct_values.push_back(make_pair("level", Value(level)));
	struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_TEXT)));
	struct_values.push_back(make_pair("attributes", CreateAttributesMap(attributes)));
	struct_values.push_back(make_pair("element_order", Value(inline_order)));

	return Value::STRUCT(std::move(struct_values));
}

Value InlineBuilderFunctions::CreateInlineWithNullContent(const string &inline_type,
                                                          const map<string, string> &attributes, int32_t level,
                                                          int32_t inline_order) {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_INLINE)));
	struct_values.push_back(make_pair("element_type", Value(inline_type)));
	struct_values.push_back(make_pair("content", Value(LogicalType::VARCHAR))); // Typed NULL
	struct_values.push_back(make_pair("level", Value(level)));
	struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_TEXT)));
	struct_values.push_back(make_pair("attributes", CreateAttributesMap(attributes)));
	struct_values.push_back(make_pair("element_order", Value(inline_order)));

	return Value::STRUCT(std::move(struct_values));
}

// ============================================================================
// Text and whitespace
// ============================================================================

void InlineBuilderFunctions::DbTextFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_TEXT, content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DbSpaceFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_SPACE, Value(""), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DbSoftBreakFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_SOFTBREAK, Value(""), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DbLineBreakFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_LINEBREAK, Value(""), BlockTypes::ENCODING_TEXT, {});
	}
}

// ============================================================================
// Formatting (container types)
// ============================================================================

void InlineBuilderFunctions::DbBoldFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_BOLD, content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DbItalicFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_ITALIC, content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DbStrikethroughFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_STRIKETHROUGH, content_vec.GetValue(i),
		                BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DbSuperscriptFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_SUPERSCRIPT, content_vec.GetValue(i), BlockTypes::ENCODING_TEXT,
		                {});
	}
}

void InlineBuilderFunctions::DbSubscriptFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_SUBSCRIPT, content_vec.GetValue(i), BlockTypes::ENCODING_TEXT,
		                {});
	}
}

void InlineBuilderFunctions::DbSmallCapsFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_SMALLCAPS, content_vec.GetValue(i), BlockTypes::ENCODING_TEXT,
		                {});
	}
}

void InlineBuilderFunctions::DbUnderlineFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_UNDERLINE, content_vec.GetValue(i), BlockTypes::ENCODING_TEXT,
		                {});
	}
}

// ============================================================================
// Semantic elements
// ============================================================================

void InlineBuilderFunctions::DbInlineCodeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_CODE, content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DbMathFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);
	bool has_display = args.ColumnCount() > 1;

	for (idx_t i = 0; i < count; i++) {
		map<string, string> attrs;
		if (has_display) {
			auto display = args.data[1].GetValue(i);
			attrs["display"] = (!display.IsNull() && display.GetValue<bool>()) ? "block" : "inline";
		} else {
			attrs["display"] = "inline";
		}
		SetInlineFields(entries, i, BlockTypes::INLINE_MATH, content_vec.GetValue(i), BlockTypes::ENCODING_LATEX,
		                attrs);
	}
}

void InlineBuilderFunctions::DbLinkFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &text_vec = args.data[0];
	auto &href_vec = args.data[1];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);
	bool has_title = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		auto text = text_vec.GetValue(i);
		auto href = href_vec.GetValue(i);

		map<string, string> attrs;
		if (!href.IsNull())
			attrs["href"] = href.GetValue<string>();
		if (has_title) {
			auto title = args.data[2].GetValue(i);
			if (!title.IsNull())
				attrs["title"] = title.GetValue<string>();
		}

		string content = text.IsNull() ? "" : text.GetValue<string>();
		SetInlineFields(entries, i, BlockTypes::INLINE_LINK, Value(content), BlockTypes::ENCODING_TEXT, attrs);
	}
}

void InlineBuilderFunctions::DbInlineImageFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &src_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);
	bool has_alt = args.ColumnCount() > 1;
	bool has_title = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		auto src = src_vec.GetValue(i);

		map<string, string> attrs;
		if (!src.IsNull())
			attrs["src"] = src.GetValue<string>();

		string content = "";
		if (has_alt) {
			auto alt = args.data[1].GetValue(i);
			if (!alt.IsNull()) {
				attrs["alt"] = alt.GetValue<string>();
				content = alt.GetValue<string>();
			}
		}
		if (has_title) {
			auto title = args.data[2].GetValue(i);
			if (!title.IsNull())
				attrs["title"] = title.GetValue<string>();
		}

		SetInlineFields(entries, i, BlockTypes::INLINE_IMAGE, Value(content), BlockTypes::ENCODING_TEXT, attrs);
	}
}

void InlineBuilderFunctions::DbQuotedFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);
	bool has_type = args.ColumnCount() > 1;

	for (idx_t i = 0; i < count; i++) {
		map<string, string> attrs;
		if (has_type) {
			auto quote_type = args.data[1].GetValue(i);
			attrs["quote_type"] = quote_type.IsNull() ? "double" : quote_type.GetValue<string>();
		} else {
			attrs["quote_type"] = "double";
		}
		SetInlineFields(entries, i, BlockTypes::INLINE_QUOTED, content_vec.GetValue(i), BlockTypes::ENCODING_TEXT,
		                attrs);
	}
}

void InlineBuilderFunctions::DbCiteFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &key_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);
	bool has_prefix = args.ColumnCount() > 1;
	bool has_suffix = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		auto key = key_vec.GetValue(i);

		map<string, string> attrs;
		if (!key.IsNull())
			attrs["key"] = key.GetValue<string>();
		if (has_prefix) {
			auto prefix = args.data[1].GetValue(i);
			if (!prefix.IsNull())
				attrs["prefix"] = prefix.GetValue<string>();
		}
		if (has_suffix) {
			auto suffix = args.data[2].GetValue(i);
			if (!suffix.IsNull())
				attrs["suffix"] = suffix.GetValue<string>();
		}

		string content = key.IsNull() ? "" : key.GetValue<string>();
		SetInlineFields(entries, i, BlockTypes::INLINE_CITE, Value(content), BlockTypes::ENCODING_TEXT, attrs);
	}
}

void InlineBuilderFunctions::DbNoteFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_NOTE, content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DbSpanFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);
	bool has_id = args.ColumnCount() > 1;
	bool has_classes = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		map<string, string> attrs;
		if (has_id) {
			auto id = args.data[1].GetValue(i);
			if (!id.IsNull())
				attrs["id"] = id.GetValue<string>();
		}
		if (has_classes) {
			auto classes = args.data[2].GetValue(i);
			if (!classes.IsNull())
				attrs["class"] = classes.GetValue<string>();
		}
		SetInlineFields(entries, i, BlockTypes::INLINE_SPAN, content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, attrs);
	}
}

void InlineBuilderFunctions::DbRawInlineFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);
	bool has_format = args.ColumnCount() > 1;

	for (idx_t i = 0; i < count; i++) {
		string format_str = "html";
		if (has_format) {
			auto format = args.data[1].GetValue(i);
			if (!format.IsNull())
				format_str = format.GetValue<string>();
		}

		map<string, string> attrs;
		attrs["format"] = format_str;

		const char *encoding = BlockTypes::ENCODING_HTML;
		if (format_str == "latex")
			encoding = BlockTypes::ENCODING_LATEX;
		else if (format_str == "xml")
			encoding = BlockTypes::ENCODING_XML;

		SetInlineFields(entries, i, BlockTypes::INLINE_RAW, content_vec.GetValue(i), encoding, attrs);
	}
}

// ============================================================================
// Flattening helpers
// ============================================================================

// Helper to create a child element with adjusted level and order
static Value CreateChildWithLevelAndOrder(const Value &element, int32_t new_level, int32_t new_order) {
	auto children = StructValue::GetChildren(element);
	children[BlockTypes::LEVEL_IDX] = Value(new_level);
	children[BlockTypes::ELEMENT_ORDER_IDX] = Value(new_order);
	return Value::STRUCT(BlockTypes::DuckBlockType(), std::move(children));
}

// Helper to flatten a parent inline with children into a list
// Returns: [parent at level n, child1 at level n+1 order 0, child2 at level n+1 order 1, ...]
// Preserves relative nesting: if children already have levels, they're adjusted relative to the new base
static vector<Value> FlattenInlineWithChildren(const Value &parent_inline, const Value &children_list,
                                               int32_t base_level) {
	vector<Value> result;

	// Add parent inline with the specified level
	auto parent_children = StructValue::GetChildren(parent_inline);
	parent_children[BlockTypes::LEVEL_IDX] = Value(base_level);
	parent_children[BlockTypes::ELEMENT_ORDER_IDX] = Value(0);
	result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));

	// Add flattened children with adjusted levels to preserve nesting
	if (!children_list.IsNull()) {
		auto &child_blocks = ListValue::GetChildren(children_list);
		if (!child_blocks.empty()) {
			// Find the minimum level among children to calculate the offset
			int32_t min_child_level = INT32_MAX;
			for (auto &child : child_blocks) {
				if (!child.IsNull()) {
					auto child_fields = StructValue::GetChildren(child);
					int32_t child_level = child_fields[BlockTypes::LEVEL_IDX].IsNull()
					                          ? 1
					                          : child_fields[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
					if (child_level < min_child_level) {
						min_child_level = child_level;
					}
				}
			}

			// Calculate level offset: children should start at base_level + 1
			int32_t level_offset = (base_level + 1) - min_child_level;

			int32_t child_order = 0;
			for (auto &child : child_blocks) {
				if (!child.IsNull()) {
					auto child_fields = StructValue::GetChildren(child);
					int32_t child_level = child_fields[BlockTypes::LEVEL_IDX].IsNull()
					                          ? 1
					                          : child_fields[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
					// Adjust level by offset to preserve relative nesting
					result.push_back(CreateChildWithLevelAndOrder(child, child_level + level_offset, child_order++));
				}
			}
		}
	}

	return result;
}

// ============================================================================
// Flattening builder overloads - return LIST(duck_block) with parent + children
// ============================================================================

void InlineBuilderFunctions::DbBoldFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &children_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto children_list = children_vec.GetValue(i);
		auto parent = CreateInline(BlockTypes::INLINE_BOLD, "", {}, 1, 0);
		auto flattened = FlattenInlineWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void InlineBuilderFunctions::DbItalicFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &children_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto children_list = children_vec.GetValue(i);
		auto parent = CreateInline(BlockTypes::INLINE_ITALIC, "", {}, 1, 0);
		auto flattened = FlattenInlineWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void InlineBuilderFunctions::DbStrikethroughFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &children_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto children_list = children_vec.GetValue(i);
		auto parent = CreateInline(BlockTypes::INLINE_STRIKETHROUGH, "", {}, 1, 0);
		auto flattened = FlattenInlineWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void InlineBuilderFunctions::DbSuperscriptFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &children_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto children_list = children_vec.GetValue(i);
		auto parent = CreateInline(BlockTypes::INLINE_SUPERSCRIPT, "", {}, 1, 0);
		auto flattened = FlattenInlineWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void InlineBuilderFunctions::DbSubscriptFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &children_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto children_list = children_vec.GetValue(i);
		auto parent = CreateInline(BlockTypes::INLINE_SUBSCRIPT, "", {}, 1, 0);
		auto flattened = FlattenInlineWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void InlineBuilderFunctions::DbSmallCapsFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &children_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto children_list = children_vec.GetValue(i);
		auto parent = CreateInline(BlockTypes::INLINE_SMALLCAPS, "", {}, 1, 0);
		auto flattened = FlattenInlineWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void InlineBuilderFunctions::DbUnderlineFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &children_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto children_list = children_vec.GetValue(i);
		auto parent = CreateInline(BlockTypes::INLINE_UNDERLINE, "", {}, 1, 0);
		auto flattened = FlattenInlineWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void InlineBuilderFunctions::DbLinkFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &href_vec = args.data[0];
	auto &children_vec = args.data[1];
	auto count = args.size();
	bool has_title = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		auto href = href_vec.GetValue(i);
		auto children_list = children_vec.GetValue(i);

		map<string, string> attrs;
		if (!href.IsNull())
			attrs["href"] = href.GetValue<string>();
		if (has_title) {
			auto title = args.data[2].GetValue(i);
			if (!title.IsNull())
				attrs["title"] = title.GetValue<string>();
		}

		auto parent = CreateInline(BlockTypes::INLINE_LINK, "", attrs, 1, 0);
		auto flattened = FlattenInlineWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void InlineBuilderFunctions::DbQuotedFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &children_vec = args.data[0];
	auto count = args.size();
	bool has_type = args.ColumnCount() > 1;

	for (idx_t i = 0; i < count; i++) {
		auto children_list = children_vec.GetValue(i);

		map<string, string> attrs;
		if (has_type) {
			auto quote_type = args.data[1].GetValue(i);
			attrs["quote_type"] = quote_type.IsNull() ? "double" : quote_type.GetValue<string>();
		} else {
			attrs["quote_type"] = "double";
		}

		auto parent = CreateInline(BlockTypes::INLINE_QUOTED, "", attrs, 1, 0);
		auto flattened = FlattenInlineWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void InlineBuilderFunctions::DbSpanFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &children_vec = args.data[0];
	auto count = args.size();
	bool has_id = args.ColumnCount() > 1;
	bool has_classes = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		auto children_list = children_vec.GetValue(i);

		map<string, string> attrs;
		if (has_id) {
			auto id = args.data[1].GetValue(i);
			if (!id.IsNull())
				attrs["id"] = id.GetValue<string>();
		}
		if (has_classes) {
			auto classes = args.data[2].GetValue(i);
			if (!classes.IsNull())
				attrs["class"] = classes.GetValue<string>();
		}

		auto parent = CreateInline(BlockTypes::INLINE_SPAN, "", attrs, 1, 0);
		auto flattened = FlattenInlineWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void InlineBuilderFunctions::DbNoteFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &children_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto children_list = children_vec.GetValue(i);
		auto parent = CreateInline(BlockTypes::INLINE_NOTE, "", {}, 1, 0);
		auto flattened = FlattenInlineWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

// ============================================================================
// Registration
// ============================================================================

void InlineBuilderFunctions::Register(ExtensionLoader &loader) {
	auto duck_block_type = BlockTypes::DuckBlockType();

	// ========================================================================
	// Legacy V1 API - Only register overloads that DON'T conflict with V2
	// V2 versions with same input signature but different return type win
	// ========================================================================

	// Text, whitespace, formatting - REMOVED: V2 versions exist with same signatures
	// db_text(VARCHAR), db_space(), db_softbreak(), db_linebreak()
	// db_bold(VARCHAR), db_italic(VARCHAR), db_strikethrough(VARCHAR)
	// db_superscript(VARCHAR), db_subscript(VARCHAR), db_smallcaps(VARCHAR), db_underline(VARCHAR)
	// db_inline_code(VARCHAR), db_math(VARCHAR)

	// db_math(VARCHAR, BOOLEAN) - LEGACY ONLY (V2 uses BOOLEAN, VARCHAR order)
	loader.RegisterFunction(
	    ScalarFunction("db_math", {LogicalType::VARCHAR, LogicalType::BOOLEAN}, duck_block_type, DbMathFun));

	// db_link(VARCHAR, VARCHAR) - REMOVED: V2 version exists with same signature
	// db_link(VARCHAR, VARCHAR, VARCHAR) - LEGACY ONLY (different param meaning than V2)

	// Semantic - inline image: REMOVED - V2 versions exist with same signatures

	// Semantic - quoted: REMOVED - V2 versions exist with same signatures

	// Semantic - citation
	// db_cite(VARCHAR) - REMOVED: V2 version exists with same signature
	// db_cite(VARCHAR, VARCHAR) - LEGACY ONLY (V2 doesn't have this signature)
	loader.RegisterFunction(
	    ScalarFunction("db_cite", {LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_type, DbCiteFun));
	// db_cite(VARCHAR, VARCHAR, VARCHAR) - LEGACY ONLY (V2 doesn't have this signature)
	loader.RegisterFunction(ScalarFunction(
	    "db_cite", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_type, DbCiteFun));

	// Semantic - footnote: REMOVED - V2 version exists with same signature

	// Semantic - span
	// db_span(VARCHAR) - REMOVED: V2 version exists with same signature
	// db_span(VARCHAR, VARCHAR) - REMOVED: V2 version below returns LIST
	// db_span(VARCHAR, VARCHAR, VARCHAR) - REMOVED: V2 version below returns LIST

	// Semantic - raw inline: REMOVED - V2 versions exist with same signatures

	// ========================================================================
	// Flattening overloads - take children list, return flattened list
	// ========================================================================
	auto duck_block_list_type = BlockTypes::DuckBlockListType();

	// Formatting with children
	loader.RegisterFunction(ScalarFunction("db_bold", {duck_block_list_type}, duck_block_list_type, DbBoldFlattenFun));
	loader.RegisterFunction(
	    ScalarFunction("db_italic", {duck_block_list_type}, duck_block_list_type, DbItalicFlattenFun));
	loader.RegisterFunction(
	    ScalarFunction("db_strikethrough", {duck_block_list_type}, duck_block_list_type, DbStrikethroughFlattenFun));
	loader.RegisterFunction(
	    ScalarFunction("db_superscript", {duck_block_list_type}, duck_block_list_type, DbSuperscriptFlattenFun));
	loader.RegisterFunction(
	    ScalarFunction("db_subscript", {duck_block_list_type}, duck_block_list_type, DbSubscriptFlattenFun));
	loader.RegisterFunction(
	    ScalarFunction("db_smallcaps", {duck_block_list_type}, duck_block_list_type, DbSmallCapsFlattenFun));
	loader.RegisterFunction(
	    ScalarFunction("db_underline", {duck_block_list_type}, duck_block_list_type, DbUnderlineFlattenFun));

	// Link with children: db_link(href, children) and db_link(href, children, title)
	loader.RegisterFunction(ScalarFunction("db_link", {LogicalType::VARCHAR, duck_block_list_type},
	                                       duck_block_list_type, DbLinkFlattenFun));
	loader.RegisterFunction(ScalarFunction("db_link",
	                                       {LogicalType::VARCHAR, duck_block_list_type, LogicalType::VARCHAR},
	                                       duck_block_list_type, DbLinkFlattenFun));

	// Quoted with children: db_quoted(children)
	// NOTE: db_quoted(children, quote_type) REMOVED - conflicts with V2 API (quote_type, content) order
	loader.RegisterFunction(
	    ScalarFunction("db_quoted", {duck_block_list_type}, duck_block_list_type, DbQuotedFlattenFun));

	// Span with children: db_span(children)
	// NOTE: db_span(children, id) and db_span(children, id, classes) REMOVED - conflicts with V2 API (id, content)
	// order
	loader.RegisterFunction(ScalarFunction("db_span", {duck_block_list_type}, duck_block_list_type, DbSpanFlattenFun));

	// Note with children: db_note(children)
	loader.RegisterFunction(ScalarFunction("db_note", {duck_block_list_type}, duck_block_list_type, DbNoteFlattenFun));

	// ========================================================================
	// Nested list overloads - accept LIST(LIST(duck_block)) and flatten
	// This enables: db_bold([db_italic('hi')]) where db_italic returns LIST
	// ========================================================================
	auto duck_block_nested_list_type = LogicalType::LIST(duck_block_list_type);

	// Helper lambda to create a nested list flattening function
	// Preserves relative nesting levels among children
	auto make_nested_flatten = [](const char *element_type) {
		return [element_type](DataChunk &args, ExpressionState &state, Vector &result) {
			auto &nested_vec = args.data[0];
			auto count = args.size();
			for (idx_t i = 0; i < count; i++) {
				auto nested_list = nested_vec.GetValue(i);
				auto flat_children = FlattenNestedList(nested_list);

				// Create parent inline element with NULL content at level 1
				auto parent = InlineBuilderFunctions::CreateInlineWithNullContent(element_type, {}, 1, 0);

				// Flatten: parent + children with preserved relative nesting
				vector<Value> flattened;
				flattened.push_back(parent);
				if (!flat_children.IsNull()) {
					auto &children = ListValue::GetChildren(flat_children);
					if (!children.empty()) {
						// Find minimum level among children to calculate offset
						int32_t min_child_level = INT32_MAX;
						for (auto &child : children) {
							if (!child.IsNull()) {
								auto child_fields = StructValue::GetChildren(child);
								int32_t child_level = child_fields[BlockTypes::LEVEL_IDX].IsNull()
								                          ? 1
								                          : child_fields[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
								if (child_level < min_child_level) {
									min_child_level = child_level;
								}
							}
						}
						// Calculate offset: children should start at level 2 (parent is 1)
						int32_t level_offset = 2 - min_child_level;
						int32_t child_order = 0;
						for (auto &child : children) {
							if (!child.IsNull()) {
								auto child_fields = StructValue::GetChildren(child);
								int32_t child_level = child_fields[BlockTypes::LEVEL_IDX].IsNull()
								                          ? 1
								                          : child_fields[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
								// Apply offset to preserve relative nesting
								child_fields[BlockTypes::LEVEL_IDX] = Value(child_level + level_offset);
								child_fields[BlockTypes::ELEMENT_ORDER_IDX] = Value(child_order++);
								flattened.push_back(
								    Value::STRUCT(BlockTypes::DuckBlockType(), std::move(child_fields)));
							}
						}
					}
				}
				result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
			}
		};
	};

	// Formatting with nested list children
	loader.RegisterFunction(ScalarFunction("db_bold", {duck_block_nested_list_type}, duck_block_list_type,
	                                       make_nested_flatten(BlockTypes::INLINE_BOLD)));
	loader.RegisterFunction(ScalarFunction("db_italic", {duck_block_nested_list_type}, duck_block_list_type,
	                                       make_nested_flatten(BlockTypes::INLINE_ITALIC)));
	loader.RegisterFunction(ScalarFunction("db_strikethrough", {duck_block_nested_list_type}, duck_block_list_type,
	                                       make_nested_flatten(BlockTypes::INLINE_STRIKETHROUGH)));
	loader.RegisterFunction(ScalarFunction("db_superscript", {duck_block_nested_list_type}, duck_block_list_type,
	                                       make_nested_flatten(BlockTypes::INLINE_SUPERSCRIPT)));
	loader.RegisterFunction(ScalarFunction("db_subscript", {duck_block_nested_list_type}, duck_block_list_type,
	                                       make_nested_flatten(BlockTypes::INLINE_SUBSCRIPT)));
	loader.RegisterFunction(ScalarFunction("db_smallcaps", {duck_block_nested_list_type}, duck_block_list_type,
	                                       make_nested_flatten(BlockTypes::INLINE_SMALLCAPS)));
	loader.RegisterFunction(ScalarFunction("db_underline", {duck_block_nested_list_type}, duck_block_list_type,
	                                       make_nested_flatten(BlockTypes::INLINE_UNDERLINE)));

	// db_link with nested list children: db_link(href, LIST(LIST(duck_block)))
	loader.RegisterFunction(ScalarFunction(
	    "db_link", {LogicalType::VARCHAR, duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &href_vec = args.data[0];
		    auto &nested_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto href = href_vec.GetValue(i);
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);

			    map<string, string> attrs;
			    if (!href.IsNull())
				    attrs["href"] = href.GetValue<string>();
			    auto parent = InlineBuilderFunctions::CreateInlineWithNullContent(BlockTypes::INLINE_LINK, attrs, 1, 0);

			    vector<Value> flattened;
			    flattened.push_back(parent);
			    if (!flat_children.IsNull()) {
				    auto &children = ListValue::GetChildren(flat_children);
				    if (!children.empty()) {
					    // Find minimum level among children to calculate offset
					    int32_t min_child_level = INT32_MAX;
					    for (auto &child : children) {
						    if (!child.IsNull()) {
							    auto child_fields = StructValue::GetChildren(child);
							    int32_t child_level = child_fields[BlockTypes::LEVEL_IDX].IsNull()
							                              ? 1
							                              : child_fields[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
							    if (child_level < min_child_level) {
								    min_child_level = child_level;
							    }
						    }
					    }
					    // Calculate offset: children should start at level 2 (parent is 1)
					    int32_t level_offset = 2 - min_child_level;
					    int32_t child_order = 0;
					    for (auto &child : children) {
						    if (!child.IsNull()) {
							    auto child_fields = StructValue::GetChildren(child);
							    int32_t child_level = child_fields[BlockTypes::LEVEL_IDX].IsNull()
							                              ? 1
							                              : child_fields[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
							    child_fields[BlockTypes::LEVEL_IDX] = Value(child_level + level_offset);
							    child_fields[BlockTypes::ELEMENT_ORDER_IDX] = Value(child_order++);
							    flattened.push_back(
							        Value::STRUCT(BlockTypes::DuckBlockType(), std::move(child_fields)));
						    }
					    }
				    }
			    }
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// db_quoted with nested list children
	loader.RegisterFunction(ScalarFunction("db_quoted", {duck_block_nested_list_type}, duck_block_list_type,
	                                       make_nested_flatten(BlockTypes::INLINE_QUOTED)));

	// db_span with nested list children
	loader.RegisterFunction(ScalarFunction("db_span", {duck_block_nested_list_type}, duck_block_list_type,
	                                       make_nested_flatten(BlockTypes::INLINE_SPAN)));

	// db_span with id and nested list children: db_span(id, nested_children)
	loader.RegisterFunction(ScalarFunction(
	    "db_span", {LogicalType::VARCHAR, duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &id_vec = args.data[0];
		    auto &nested_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto id = id_vec.GetValue(i);
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);

			    map<string, string> attrs;
			    if (!id.IsNull())
				    attrs["id"] = id.GetValue<string>();
			    auto parent = InlineBuilderFunctions::CreateInlineWithNullContent(BlockTypes::INLINE_SPAN, attrs, 1, 0);

			    vector<Value> flattened;
			    flattened.push_back(parent);
			    if (!flat_children.IsNull()) {
				    auto &children = ListValue::GetChildren(flat_children);
				    if (!children.empty()) {
					    // Find minimum level among children to calculate offset
					    int32_t min_child_level = INT32_MAX;
					    for (auto &child : children) {
						    if (!child.IsNull()) {
							    auto child_fields = StructValue::GetChildren(child);
							    int32_t child_level = child_fields[BlockTypes::LEVEL_IDX].IsNull()
							                              ? 1
							                              : child_fields[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
							    if (child_level < min_child_level) {
								    min_child_level = child_level;
							    }
						    }
					    }
					    // Calculate offset: children should start at level 2 (parent is 1)
					    int32_t level_offset = 2 - min_child_level;
					    int32_t child_order = 0;
					    for (auto &child : children) {
						    if (!child.IsNull()) {
							    auto child_fields = StructValue::GetChildren(child);
							    int32_t child_level = child_fields[BlockTypes::LEVEL_IDX].IsNull()
							                              ? 1
							                              : child_fields[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
							    child_fields[BlockTypes::LEVEL_IDX] = Value(child_level + level_offset);
							    child_fields[BlockTypes::ELEMENT_ORDER_IDX] = Value(child_order++);
							    flattened.push_back(
							        Value::STRUCT(BlockTypes::DuckBlockType(), std::move(child_fields)));
						    }
					    }
				    }
			    }
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// db_note with nested list children
	loader.RegisterFunction(ScalarFunction("db_note", {duck_block_nested_list_type}, duck_block_list_type,
	                                       make_nested_flatten(BlockTypes::INLINE_NOTE)));

	// ========================================================================
	// V2 API: All builders return LIST(duck_block)
	// These wrap single elements in a list for uniform composition
	// ========================================================================

	// Helper lambda to wrap single inline element in a list
	auto wrap_in_list = [](const char *element_type, const char *encoding = BlockTypes::ENCODING_TEXT) {
		return [element_type, encoding](DataChunk &args, ExpressionState &state, Vector &result) {
			auto &content_vec = args.data[0];
			auto count = args.size();
			for (idx_t i = 0; i < count; i++) {
				auto content = content_vec.GetValue(i);
				auto element = InlineBuilderFunctions::CreateInline(
				    element_type, content.IsNull() ? "" : content.GetValue<string>(), {}, 1, 0);
				vector<Value> list_result;
				list_result.push_back(element);
				result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
			}
		};
	};

	// Helper lambda for zero-arg whitespace elements
	auto wrap_whitespace_in_list = [](const char *element_type) {
		return [element_type](DataChunk &args, ExpressionState &state, Vector &result) {
			auto count = args.size();
			for (idx_t i = 0; i < count; i++) {
				auto element = InlineBuilderFunctions::CreateInline(element_type, "", {}, 1, 0);
				vector<Value> list_result;
				list_result.push_back(element);
				result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
			}
		};
	};

	// V2: Text and whitespace returning LIST
	loader.RegisterFunction(
	    ScalarFunction("db_text", {LogicalType::VARCHAR}, duck_block_list_type, wrap_in_list(BlockTypes::INLINE_TEXT)));
	loader.RegisterFunction(
	    ScalarFunction("db_space", {}, duck_block_list_type, wrap_whitespace_in_list(BlockTypes::INLINE_SPACE)));
	loader.RegisterFunction(ScalarFunction("db_softbreak", {}, duck_block_list_type,
	                                       wrap_whitespace_in_list(BlockTypes::INLINE_SOFTBREAK)));
	loader.RegisterFunction(ScalarFunction("db_linebreak", {}, duck_block_list_type,
	                                       wrap_whitespace_in_list(BlockTypes::INLINE_LINEBREAK)));

	// V2: Formatting returning LIST
	loader.RegisterFunction(
	    ScalarFunction("db_bold", {LogicalType::VARCHAR}, duck_block_list_type, wrap_in_list(BlockTypes::INLINE_BOLD)));
	loader.RegisterFunction(ScalarFunction("db_italic", {LogicalType::VARCHAR}, duck_block_list_type,
	                                       wrap_in_list(BlockTypes::INLINE_ITALIC)));
	loader.RegisterFunction(ScalarFunction("db_strikethrough", {LogicalType::VARCHAR}, duck_block_list_type,
	                                       wrap_in_list(BlockTypes::INLINE_STRIKETHROUGH)));
	loader.RegisterFunction(ScalarFunction("db_superscript", {LogicalType::VARCHAR}, duck_block_list_type,
	                                       wrap_in_list(BlockTypes::INLINE_SUPERSCRIPT)));
	loader.RegisterFunction(ScalarFunction("db_subscript", {LogicalType::VARCHAR}, duck_block_list_type,
	                                       wrap_in_list(BlockTypes::INLINE_SUBSCRIPT)));
	loader.RegisterFunction(ScalarFunction("db_smallcaps", {LogicalType::VARCHAR}, duck_block_list_type,
	                                       wrap_in_list(BlockTypes::INLINE_SMALLCAPS)));
	loader.RegisterFunction(ScalarFunction("db_underline", {LogicalType::VARCHAR}, duck_block_list_type,
	                                       wrap_in_list(BlockTypes::INLINE_UNDERLINE)));

	// V2: Semantic inline code returning LIST
	loader.RegisterFunction(ScalarFunction("db_inline_code", {LogicalType::VARCHAR}, duck_block_list_type,
	                                       wrap_in_list(BlockTypes::INLINE_CODE)));

	// V2: Math returning LIST
	loader.RegisterFunction(ScalarFunction(
	    "db_math", {LogicalType::VARCHAR}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &content_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto content = content_vec.GetValue(i);
			    map<string, string> attrs;
			    attrs["display"] = "inline";
			    auto element = InlineBuilderFunctions::CreateInline(
			        BlockTypes::INLINE_MATH, content.IsNull() ? "" : content.GetValue<string>(), attrs, 1, 0);
			    vector<Value> list_result;
			    list_result.push_back(element);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
		    }
	    }));

	loader.RegisterFunction(ScalarFunction(
	    "db_math", {LogicalType::BOOLEAN, LogicalType::VARCHAR}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &display_vec = args.data[0];
		    auto &content_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto display = display_vec.GetValue(i);
			    auto content = content_vec.GetValue(i);
			    map<string, string> attrs;
			    attrs["display"] = (!display.IsNull() && display.GetValue<bool>()) ? "block" : "inline";
			    auto element = InlineBuilderFunctions::CreateInline(
			        BlockTypes::INLINE_MATH, content.IsNull() ? "" : content.GetValue<string>(), attrs, 1, 0);
			    vector<Value> list_result;
			    list_result.push_back(element);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
		    }
	    }));

	// V2: Link returning LIST (href, content)
	loader.RegisterFunction(ScalarFunction(
	    "db_link", {LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &href_vec = args.data[0];
		    auto &content_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto href = href_vec.GetValue(i);
			    auto content = content_vec.GetValue(i);
			    map<string, string> attrs;
			    if (!href.IsNull())
				    attrs["href"] = href.GetValue<string>();
			    auto element = InlineBuilderFunctions::CreateInline(
			        BlockTypes::INLINE_LINK, content.IsNull() ? "" : content.GetValue<string>(), attrs, 1, 0);
			    vector<Value> list_result;
			    list_result.push_back(element);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
		    }
	    }));

	// V2: Link returning LIST (href, title, content)
	loader.RegisterFunction(ScalarFunction(
	    "db_link", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &href_vec = args.data[0];
		    auto &title_vec = args.data[1];
		    auto &content_vec = args.data[2];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto href = href_vec.GetValue(i);
			    auto title = title_vec.GetValue(i);
			    auto content = content_vec.GetValue(i);
			    map<string, string> attrs;
			    if (!href.IsNull())
				    attrs["href"] = href.GetValue<string>();
			    if (!title.IsNull())
				    attrs["title"] = title.GetValue<string>();
			    auto element = InlineBuilderFunctions::CreateInline(
			        BlockTypes::INLINE_LINK, content.IsNull() ? "" : content.GetValue<string>(), attrs, 1, 0);
			    vector<Value> list_result;
			    list_result.push_back(element);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
		    }
	    }));

	// V2: Inline image returning LIST
	loader.RegisterFunction(ScalarFunction(
	    "db_inline_image", {LogicalType::VARCHAR}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &src_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto src = src_vec.GetValue(i);
			    map<string, string> attrs;
			    if (!src.IsNull())
				    attrs["src"] = src.GetValue<string>();
			    auto element = InlineBuilderFunctions::CreateInline(BlockTypes::INLINE_IMAGE, "", attrs, 1, 0);
			    vector<Value> list_result;
			    list_result.push_back(element);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
		    }
	    }));

	loader.RegisterFunction(ScalarFunction(
	    "db_inline_image", {LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &src_vec = args.data[0];
		    auto &alt_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto src = src_vec.GetValue(i);
			    auto alt = alt_vec.GetValue(i);
			    map<string, string> attrs;
			    if (!src.IsNull())
				    attrs["src"] = src.GetValue<string>();
			    if (!alt.IsNull())
				    attrs["alt"] = alt.GetValue<string>();
			    string content = alt.IsNull() ? "" : alt.GetValue<string>();
			    auto element = InlineBuilderFunctions::CreateInline(BlockTypes::INLINE_IMAGE, content, attrs, 1, 0);
			    vector<Value> list_result;
			    list_result.push_back(element);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
		    }
	    }));

	loader.RegisterFunction(ScalarFunction(
	    "db_inline_image", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &src_vec = args.data[0];
		    auto &alt_vec = args.data[1];
		    auto &title_vec = args.data[2];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto src = src_vec.GetValue(i);
			    auto alt = alt_vec.GetValue(i);
			    auto title = title_vec.GetValue(i);
			    map<string, string> attrs;
			    if (!src.IsNull())
				    attrs["src"] = src.GetValue<string>();
			    if (!alt.IsNull())
				    attrs["alt"] = alt.GetValue<string>();
			    if (!title.IsNull())
				    attrs["title"] = title.GetValue<string>();
			    string content = alt.IsNull() ? "" : alt.GetValue<string>();
			    auto element = InlineBuilderFunctions::CreateInline(BlockTypes::INLINE_IMAGE, content, attrs, 1, 0);
			    vector<Value> list_result;
			    list_result.push_back(element);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
		    }
	    }));

	// V2: Quoted returning LIST
	loader.RegisterFunction(ScalarFunction(
	    "db_quoted", {LogicalType::VARCHAR}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &content_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto content = content_vec.GetValue(i);
			    map<string, string> attrs;
			    attrs["quote_type"] = "double";
			    auto element = InlineBuilderFunctions::CreateInline(
			        BlockTypes::INLINE_QUOTED, content.IsNull() ? "" : content.GetValue<string>(), attrs, 1, 0);
			    vector<Value> list_result;
			    list_result.push_back(element);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
		    }
	    }));

	loader.RegisterFunction(ScalarFunction(
	    "db_quoted", {LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &quote_type_vec = args.data[0];
		    auto &content_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto quote_type = quote_type_vec.GetValue(i);
			    auto content = content_vec.GetValue(i);
			    map<string, string> attrs;
			    attrs["quote_type"] = quote_type.IsNull() ? "double" : quote_type.GetValue<string>();
			    auto element = InlineBuilderFunctions::CreateInline(
			        BlockTypes::INLINE_QUOTED, content.IsNull() ? "" : content.GetValue<string>(), attrs, 1, 0);
			    vector<Value> list_result;
			    list_result.push_back(element);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
		    }
	    }));

	// V2: Cite returning LIST
	loader.RegisterFunction(ScalarFunction(
	    "db_cite", {LogicalType::VARCHAR}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &key_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto key = key_vec.GetValue(i);
			    map<string, string> attrs;
			    if (!key.IsNull())
				    attrs["key"] = key.GetValue<string>();
			    string content = key.IsNull() ? "" : key.GetValue<string>();
			    auto element = InlineBuilderFunctions::CreateInline(BlockTypes::INLINE_CITE, content, attrs, 1, 0);
			    vector<Value> list_result;
			    list_result.push_back(element);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
		    }
	    }));

	// V2: Note returning LIST
	loader.RegisterFunction(
	    ScalarFunction("db_note", {LogicalType::VARCHAR}, duck_block_list_type, wrap_in_list(BlockTypes::INLINE_NOTE)));

	// V2: Span returning LIST
	loader.RegisterFunction(
	    ScalarFunction("db_span", {LogicalType::VARCHAR}, duck_block_list_type, wrap_in_list(BlockTypes::INLINE_SPAN)));

	// V2: Span with id and content
	loader.RegisterFunction(ScalarFunction(
	    "db_span", {LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &id_vec = args.data[0];
		    auto &content_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto id = id_vec.GetValue(i);
			    auto content = content_vec.GetValue(i);
			    map<string, string> attrs;
			    if (!id.IsNull())
				    attrs["id"] = id.GetValue<string>();
			    auto element = InlineBuilderFunctions::CreateInline(
			        BlockTypes::INLINE_SPAN, content.IsNull() ? "" : content.GetValue<string>(), attrs, 1, 0);
			    vector<Value> list_result;
			    list_result.push_back(element);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
		    }
	    }));

	// V2: Span with id, class, and content
	loader.RegisterFunction(ScalarFunction(
	    "db_span", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &id_vec = args.data[0];
		    auto &class_vec = args.data[1];
		    auto &content_vec = args.data[2];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto id = id_vec.GetValue(i);
			    auto cls = class_vec.GetValue(i);
			    auto content = content_vec.GetValue(i);
			    map<string, string> attrs;
			    if (!id.IsNull())
				    attrs["id"] = id.GetValue<string>();
			    if (!cls.IsNull())
				    attrs["class"] = cls.GetValue<string>();
			    auto element = InlineBuilderFunctions::CreateInline(
			        BlockTypes::INLINE_SPAN, content.IsNull() ? "" : content.GetValue<string>(), attrs, 1, 0);
			    vector<Value> list_result;
			    list_result.push_back(element);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
		    }
	    }));

	// V2: Raw inline returning LIST
	loader.RegisterFunction(ScalarFunction(
	    "db_raw_inline", {LogicalType::VARCHAR}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &content_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto content = content_vec.GetValue(i);
			    map<string, string> attrs;
			    attrs["format"] = "html";
			    auto element = InlineBuilderFunctions::CreateInline(
			        BlockTypes::INLINE_RAW, content.IsNull() ? "" : content.GetValue<string>(), attrs, 1, 0);
			    vector<Value> list_result;
			    list_result.push_back(element);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
		    }
	    }));

	loader.RegisterFunction(ScalarFunction(
	    "db_raw_inline", {LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &format_vec = args.data[0];
		    auto &content_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto format = format_vec.GetValue(i);
			    auto content = content_vec.GetValue(i);
			    string format_str = format.IsNull() ? "html" : format.GetValue<string>();
			    map<string, string> attrs;
			    attrs["format"] = format_str;
			    auto element = InlineBuilderFunctions::CreateInline(
			        BlockTypes::INLINE_RAW, content.IsNull() ? "" : content.GetValue<string>(), attrs, 1, 0);
			    vector<Value> list_result;
			    list_result.push_back(element);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(list_result)));
		    }
	    }));
}

} // namespace duckdb
