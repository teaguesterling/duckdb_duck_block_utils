#include "builders.hpp"
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

// Helper to set all struct fields for a block element
static void SetBlockFields(vector<unique_ptr<Vector>> &entries, idx_t i,
                           const char *element_type, const Value &content, const Value &level,
                           const char *encoding, const Value &attributes) {
	entries[BlockTypes::KIND_IDX]->SetValue(i, Value(BlockTypes::KIND_BLOCK));
	entries[BlockTypes::ELEMENT_TYPE_IDX]->SetValue(i, Value(element_type));
	entries[BlockTypes::CONTENT_IDX]->SetValue(i, content);
	entries[BlockTypes::LEVEL_IDX]->SetValue(i, level);
	entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(encoding));
	entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, attributes);
	entries[BlockTypes::ELEMENT_ORDER_IDX]->SetValue(i, Value(0));
}

Value BuilderFunctions::CreateBlock(const string &block_type, const string &content,
                                    const Value &level, const string &encoding,
                                    const map<string, string> &attributes, int32_t block_order) {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
	struct_values.push_back(make_pair("element_type", Value(block_type)));
	struct_values.push_back(make_pair("content", Value(content)));
	struct_values.push_back(make_pair("level", level));
	struct_values.push_back(make_pair("encoding", Value(encoding)));
	struct_values.push_back(make_pair("attributes", CreateAttributesMap(attributes)));
	struct_values.push_back(make_pair("element_order", Value(block_order)));

	return Value::STRUCT(std::move(struct_values));
}

// ============================================================================
// V2 API: Core utilities
// ============================================================================

Value BuilderFunctions::CreateBlockWithNullContent(const string &block_type, const string &kind,
                                                   const Value &level, const string &encoding,
                                                   const map<string, string> &attributes) {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("kind", Value(kind)));
	struct_values.push_back(make_pair("element_type", Value(block_type)));
	struct_values.push_back(make_pair("content", Value(LogicalType::VARCHAR)));  // Typed NULL content
	struct_values.push_back(make_pair("level", level));
	struct_values.push_back(make_pair("encoding", Value(encoding)));
	struct_values.push_back(make_pair("attributes", CreateAttributesMap(attributes)));
	struct_values.push_back(make_pair("element_order", Value(0)));

	return Value::STRUCT(std::move(struct_values));
}

// Helper to create a child element with adjusted level and order
static Value CreateChildWithLevelAndOrder(const Value &element, int32_t new_level, int32_t new_order) {
	auto children = StructValue::GetChildren(element);
	children[BlockTypes::LEVEL_IDX] = Value(new_level);
	children[BlockTypes::ELEMENT_ORDER_IDX] = Value(new_order);
	return Value::STRUCT(BlockTypes::DuckBlockType(), std::move(children));
}

vector<Value> BuilderFunctions::BuildWithContent(const Value &parent_block, const Value &content_input,
                                                  int32_t base_level) {
	vector<Value> result;

	// Get parent block children for modification
	auto parent_children = StructValue::GetChildren(parent_block);
	parent_children[BlockTypes::LEVEL_IDX] = Value(base_level);
	parent_children[BlockTypes::ELEMENT_ORDER_IDX] = Value(0);

	if (content_input.IsNull()) {
		// NULL content - keep parent content as-is (already NULL from CreateBlockWithNullContent)
		result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));
	}
	else if (content_input.type().id() == LogicalTypeId::VARCHAR) {
		// VARCHAR content - set content field
		parent_children[BlockTypes::CONTENT_IDX] = content_input;
		result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));
	}
	else if (content_input.type().id() == LogicalTypeId::STRUCT) {
		// Single duck_block child - parent content stays NULL, add child at level+1
		result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));

		// Add child at level+1
		auto child_children = StructValue::GetChildren(content_input);
		child_children[BlockTypes::LEVEL_IDX] = Value(base_level + 1);
		child_children[BlockTypes::ELEMENT_ORDER_IDX] = Value(0);
		result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(child_children)));
	}
	else if (content_input.type().id() == LogicalTypeId::LIST) {
		// LIST(duck_block) children - parent content stays NULL, add children at level+1
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
	else {
		// Unknown type - just return parent as-is
		result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));
	}

	return result;
}

// Helper to flatten a parent block with children into a list (legacy)
// Returns: [parent at level n, child1 at level n+1 order 0, child2 at level n+1 order 1, ...]
static vector<Value> FlattenBlockWithChildren(const Value &parent_block, const Value &children_list, int32_t base_level) {
	vector<Value> result;

	// Add parent block with the specified level
	auto parent_children = StructValue::GetChildren(parent_block);
	parent_children[BlockTypes::LEVEL_IDX] = Value(base_level);
	parent_children[BlockTypes::ELEMENT_ORDER_IDX] = Value(0);
	result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));

	// Add flattened children at level+1 with sequential element_order
	if (!children_list.IsNull()) {
		auto &child_blocks = ListValue::GetChildren(children_list);
		int32_t child_order = 0;
		for (auto &child : child_blocks) {
			if (!child.IsNull()) {
				result.push_back(CreateChildWithLevelAndOrder(child, base_level + 1, child_order++));
			}
		}
	}

	return result;
}

// Helper to flatten LIST(LIST(duck_block)) to LIST(duck_block)
// Each inner list's elements are extracted and concatenated
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

void BuilderFunctions::DbHeadingFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto &level_vec = args.data[1];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto heading_level = level_vec.GetValue(i);
		map<string, string> attrs;
		if (!heading_level.IsNull()) {
			attrs["heading_level"] = std::to_string(heading_level.GetValue<int32_t>());
		}
		SetBlockFields(entries, i, BlockTypes::TYPE_HEADING,
		               content_vec.GetValue(i), Value(),
		               BlockTypes::ENCODING_TEXT, CreateAttributesMap(attrs));
	}
}

void BuilderFunctions::DbParagraphFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetBlockFields(entries, i, BlockTypes::TYPE_PARAGRAPH,
		               content_vec.GetValue(i), Value(),
		               BlockTypes::ENCODING_TEXT, CreateAttributesMap({}));
	}
}

void BuilderFunctions::DbCodeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto &language_vec = args.data[1];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto language = language_vec.GetValue(i);
		map<string, string> attrs;
		if (!language.IsNull()) {
			attrs["language"] = language.GetValue<string>();
		}
		SetBlockFields(entries, i, BlockTypes::TYPE_CODE,
		               content_vec.GetValue(i), Value(),
		               BlockTypes::ENCODING_TEXT, CreateAttributesMap(attrs));
	}
}

void BuilderFunctions::DbBlockquoteFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto &level_vec = args.data[1];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto level = level_vec.GetValue(i);
		if (level.IsNull()) level = Value(1);
		SetBlockFields(entries, i, BlockTypes::TYPE_BLOCKQUOTE,
		               content_vec.GetValue(i), level,
		               BlockTypes::ENCODING_TEXT, CreateAttributesMap({}));
	}
}

void BuilderFunctions::DbListBlockFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &items_vec = args.data[0];
	auto &ordered_vec = args.data[1];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto items = items_vec.GetValue(i);
		auto ordered = ordered_vec.GetValue(i);

		// Convert items list to JSON array string
		string json = "[";
		if (!items.IsNull()) {
			auto &items_list = ListValue::GetChildren(items);
			bool first = true;
			for (auto &item : items_list) {
				if (!first) json += ",";
				first = false;
				string s = item.IsNull() ? "" : item.GetValue<string>();
				json += "\"";
				for (char c : s) {
					if (c == '"') json += "\\\"";
					else if (c == '\\') json += "\\\\";
					else if (c == '\n') json += "\\n";
					else json += c;
				}
				json += "\"";
			}
		}
		json += "]";

		map<string, string> attrs;
		attrs["ordered"] = (!ordered.IsNull() && ordered.GetValue<bool>()) ? "true" : "false";

		SetBlockFields(entries, i, BlockTypes::TYPE_LIST,
		               Value(json), Value(1),
		               BlockTypes::ENCODING_JSON, CreateAttributesMap(attrs));
	}
}

void BuilderFunctions::DbHrFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetBlockFields(entries, i, BlockTypes::TYPE_HR,
		               Value(""), Value(),
		               BlockTypes::ENCODING_TEXT, CreateAttributesMap({}));
	}
}

void BuilderFunctions::DbMetadataFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetBlockFields(entries, i, BlockTypes::TYPE_METADATA,
		               content_vec.GetValue(i), Value(0),
		               BlockTypes::ENCODING_YAML, CreateAttributesMap({}));
	}
}

void BuilderFunctions::DbImageFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &src_vec = args.data[0];
	auto &alt_vec = args.data[1];
	auto &title_vec = args.data[2];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto src = src_vec.GetValue(i);
		auto alt = alt_vec.GetValue(i);
		auto title = title_vec.GetValue(i);

		map<string, string> attrs;
		attrs["src"] = src.IsNull() ? "" : src.GetValue<string>();
		if (!alt.IsNull()) attrs["alt"] = alt.GetValue<string>();
		if (!title.IsNull()) attrs["title"] = title.GetValue<string>();

		string content = alt.IsNull() ? "" : alt.GetValue<string>();
		SetBlockFields(entries, i, BlockTypes::TYPE_IMAGE,
		               Value(content), Value(),
		               BlockTypes::ENCODING_TEXT, CreateAttributesMap(attrs));
	}
}

void BuilderFunctions::DbRawFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto &format_vec = args.data[1];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto format = format_vec.GetValue(i);
		string format_str = format.IsNull() ? "html" : format.GetValue<string>();

		map<string, string> attrs;
		attrs["format"] = format_str;

		const char *encoding = BlockTypes::ENCODING_HTML;
		if (format_str == "xml") encoding = BlockTypes::ENCODING_XML;
		else if (format_str == "latex") encoding = BlockTypes::ENCODING_LATEX;

		SetBlockFields(entries, i, BlockTypes::TYPE_RAW,
		               content_vec.GetValue(i), Value(),
		               encoding, CreateAttributesMap(attrs));
	}
}

// ============================================================================
// Flattening builder overloads - return LIST(duck_block) with parent + children
// ============================================================================

void BuilderFunctions::DbParagraphFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &children_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto children_list = children_vec.GetValue(i);

		// Create parent paragraph block with NULL content
		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_PARAGRAPH, BlockTypes::KIND_BLOCK,
		                                         Value(1), BlockTypes::ENCODING_TEXT, {});

		// Flatten with parent at level 1
		auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void BuilderFunctions::DbHeadingFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &level_vec = args.data[0];
	auto &children_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto heading_level = level_vec.GetValue(i);
		auto children_list = children_vec.GetValue(i);

		map<string, string> attrs;
		if (!heading_level.IsNull()) {
			attrs["heading_level"] = std::to_string(heading_level.GetValue<int32_t>());
		}

		// Create parent heading block with NULL content
		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_HEADING, BlockTypes::KIND_BLOCK,
		                                         Value(1), BlockTypes::ENCODING_TEXT, attrs);

		// Flatten with parent at level 1
		auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void BuilderFunctions::DbBlockquoteFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &level_vec = args.data[0];
	auto &children_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto bq_level = level_vec.GetValue(i);
		auto children_list = children_vec.GetValue(i);

		int32_t level_val = bq_level.IsNull() ? 1 : bq_level.GetValue<int32_t>();

		// Create parent blockquote with NULL content
		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_BLOCKQUOTE, BlockTypes::KIND_BLOCK,
		                                         Value(level_val), BlockTypes::ENCODING_TEXT, {});

		// Flatten with parent at level 1
		auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void BuilderFunctions::DbCodeFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &language_vec = args.data[0];
	auto &children_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto language = language_vec.GetValue(i);
		auto children_list = children_vec.GetValue(i);

		map<string, string> attrs;
		if (!language.IsNull()) {
			attrs["language"] = language.GetValue<string>();
		}

		// Create parent code block with NULL content
		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_CODE, BlockTypes::KIND_BLOCK,
		                                         Value(1), BlockTypes::ENCODING_TEXT, attrs);

		// Flatten with parent at level 1
		auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

// ============================================================================
// V2 API: Block builders - all return LIST(duck_block)
// ============================================================================

void BuilderFunctions::DbHeadingV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &level_vec = args.data[0];
	auto &content_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto heading_level = level_vec.GetValue(i);
		auto content = content_vec.GetValue(i);

		map<string, string> attrs;
		if (!heading_level.IsNull()) {
			attrs["heading_level"] = std::to_string(heading_level.GetValue<int32_t>());
		}

		// Create parent heading block with NULL content
		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_HEADING, BlockTypes::KIND_BLOCK,
		                                         Value(1), BlockTypes::ENCODING_TEXT, attrs);

		// Build result using content handling utility
		auto result_list = BuildWithContent(parent, content, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbParagraphV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		// Create parent paragraph block with NULL content
		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_PARAGRAPH, BlockTypes::KIND_BLOCK,
		                                         Value(1), BlockTypes::ENCODING_TEXT, {});

		// Build result using content handling utility
		auto result_list = BuildWithContent(parent, content, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbCodeV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &language_vec = args.data[0];
	auto &content_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto language = language_vec.GetValue(i);
		auto content = content_vec.GetValue(i);

		map<string, string> attrs;
		if (!language.IsNull()) {
			attrs["language"] = language.GetValue<string>();
		}

		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_CODE, BlockTypes::KIND_BLOCK,
		                                         Value(1), BlockTypes::ENCODING_TEXT, attrs);

		auto result_list = BuildWithContent(parent, content, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbCodeV2NoLangFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_CODE, BlockTypes::KIND_BLOCK,
		                                         Value(1), BlockTypes::ENCODING_TEXT, {});

		auto result_list = BuildWithContent(parent, content, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbBlockquoteV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &level_vec = args.data[0];
	auto &content_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto bq_level = level_vec.GetValue(i);
		auto content = content_vec.GetValue(i);

		int32_t level_val = bq_level.IsNull() ? 1 : bq_level.GetValue<int32_t>();

		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_BLOCKQUOTE, BlockTypes::KIND_BLOCK,
		                                         Value(level_val), BlockTypes::ENCODING_TEXT, {});

		// Use level_val as base_level so blockquote's level reflects quote nesting
		auto result_list = BuildWithContent(parent, content, level_val);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbBlockquoteV2NoLevelFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_BLOCKQUOTE, BlockTypes::KIND_BLOCK,
		                                         Value(1), BlockTypes::ENCODING_TEXT, {});

		auto result_list = BuildWithContent(parent, content, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

// Helper to convert VARCHAR[] items to JSON string
static string ItemsToJson(const Value &items) {
	string json = "[";
	if (!items.IsNull()) {
		auto &items_list = ListValue::GetChildren(items);
		bool first = true;
		for (auto &item : items_list) {
			if (!first) json += ",";
			first = false;
			string s = item.IsNull() ? "" : item.GetValue<string>();
			json += "\"";
			for (char c : s) {
				if (c == '"') json += "\\\"";
				else if (c == '\\') json += "\\\\";
				else if (c == '\n') json += "\\n";
				else json += c;
			}
			json += "\"";
		}
	}
	json += "]";
	return json;
}

void BuilderFunctions::DbListBlockV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &ordered_vec = args.data[0];
	auto &items_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto ordered = ordered_vec.GetValue(i);
		auto items = items_vec.GetValue(i);

		string json = ItemsToJson(items);

		map<string, string> attrs;
		attrs["ordered"] = (!ordered.IsNull() && ordered.GetValue<bool>()) ? "true" : "false";

		// List blocks store items as JSON in content, so we create the block directly
		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
		struct_values.push_back(make_pair("element_type", Value(BlockTypes::TYPE_LIST)));
		struct_values.push_back(make_pair("content", Value(json)));
		struct_values.push_back(make_pair("level", Value(1)));
		struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_JSON)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap(attrs)));
		struct_values.push_back(make_pair("element_order", Value(0)));

		vector<Value> result_list;
		result_list.push_back(Value::STRUCT(std::move(struct_values)));
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbListBlockV2NoOrderFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &items_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto items = items_vec.GetValue(i);
		string json = ItemsToJson(items);

		map<string, string> attrs;
		attrs["ordered"] = "false";

		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
		struct_values.push_back(make_pair("element_type", Value(BlockTypes::TYPE_LIST)));
		struct_values.push_back(make_pair("content", Value(json)));
		struct_values.push_back(make_pair("level", Value(1)));
		struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_JSON)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap(attrs)));
		struct_values.push_back(make_pair("element_order", Value(0)));

		vector<Value> result_list;
		result_list.push_back(Value::STRUCT(std::move(struct_values)));
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbListItemV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &ordered_vec = args.data[0];
	auto &content_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto ordered = ordered_vec.GetValue(i);
		auto content = content_vec.GetValue(i);

		map<string, string> attrs;
		attrs["ordered"] = (!ordered.IsNull() && ordered.GetValue<bool>()) ? "true" : "false";

		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_LIST_ITEM, BlockTypes::KIND_BLOCK,
		                                         Value(1), BlockTypes::ENCODING_TEXT, attrs);

		auto result_list = BuildWithContent(parent, content, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbListItemV2NoOrderFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		map<string, string> attrs;
		attrs["ordered"] = "false";

		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_LIST_ITEM, BlockTypes::KIND_BLOCK,
		                                         Value(1), BlockTypes::ENCODING_TEXT, attrs);

		auto result_list = BuildWithContent(parent, content, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbHrV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
		struct_values.push_back(make_pair("element_type", Value(BlockTypes::TYPE_HR)));
		struct_values.push_back(make_pair("content", Value("")));
		struct_values.push_back(make_pair("level", Value(1)));
		struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_TEXT)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap({})));
		struct_values.push_back(make_pair("element_order", Value(0)));

		vector<Value> result_list;
		result_list.push_back(Value::STRUCT(std::move(struct_values)));
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbMetadataV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
		struct_values.push_back(make_pair("element_type", Value(BlockTypes::TYPE_METADATA)));
		struct_values.push_back(make_pair("content", content));
		struct_values.push_back(make_pair("level", Value(0)));
		struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_YAML)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap({})));
		struct_values.push_back(make_pair("element_order", Value(0)));

		vector<Value> result_list;
		result_list.push_back(Value::STRUCT(std::move(struct_values)));
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbImageV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &src_vec = args.data[0];
	auto count = args.size();
	bool has_alt = args.ColumnCount() > 1;
	bool has_title = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		auto src = src_vec.GetValue(i);

		map<string, string> attrs;
		attrs["src"] = src.IsNull() ? "" : src.GetValue<string>();

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
			if (!title.IsNull()) attrs["title"] = title.GetValue<string>();
		}

		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
		struct_values.push_back(make_pair("element_type", Value(BlockTypes::TYPE_IMAGE)));
		struct_values.push_back(make_pair("content", Value(content)));
		struct_values.push_back(make_pair("level", Value(1)));
		struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_TEXT)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap(attrs)));
		struct_values.push_back(make_pair("element_order", Value(0)));

		vector<Value> result_list;
		result_list.push_back(Value::STRUCT(std::move(struct_values)));
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbRawV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &format_vec = args.data[0];
	auto &content_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto format = format_vec.GetValue(i);
		auto content = content_vec.GetValue(i);

		string format_str = format.IsNull() ? "html" : format.GetValue<string>();

		map<string, string> attrs;
		attrs["format"] = format_str;

		const char *encoding = BlockTypes::ENCODING_HTML;
		if (format_str == "xml") encoding = BlockTypes::ENCODING_XML;
		else if (format_str == "latex") encoding = BlockTypes::ENCODING_LATEX;

		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
		struct_values.push_back(make_pair("element_type", Value(BlockTypes::TYPE_RAW)));
		struct_values.push_back(make_pair("content", content));
		struct_values.push_back(make_pair("level", Value(1)));
		struct_values.push_back(make_pair("encoding", Value(encoding)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap(attrs)));
		struct_values.push_back(make_pair("element_order", Value(0)));

		vector<Value> result_list;
		result_list.push_back(Value::STRUCT(std::move(struct_values)));
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbRawV2NoFormatFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		map<string, string> attrs;
		attrs["format"] = "html";

		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
		struct_values.push_back(make_pair("element_type", Value(BlockTypes::TYPE_RAW)));
		struct_values.push_back(make_pair("content", content));
		struct_values.push_back(make_pair("level", Value(1)));
		struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_HTML)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap(attrs)));
		struct_values.push_back(make_pair("element_order", Value(0)));

		vector<Value> result_list;
		result_list.push_back(Value::STRUCT(std::move(struct_values)));
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::Register(ExtensionLoader &loader) {
	auto duck_block_type = BlockTypes::DuckBlockType();
	auto duck_block_list_type = BlockTypes::DuckBlockListType();

	// ========================================================================
	// Legacy V1 API - Only register overloads that DON'T conflict with V2
	// (V2 versions with same input signature but different return type win)
	// ========================================================================

	// db_heading(content, level) - LEGACY ONLY (V2 uses level, content order)
	loader.RegisterFunction(ScalarFunction("db_heading",
	    {LogicalType::VARCHAR, LogicalType::INTEGER}, duck_block_type, DbHeadingFun));

	// db_paragraph(content) - REMOVED: V2 version exists with same signature

	// db_code(content, language) - REMOVED: V2 has same signature (VARCHAR, VARCHAR)
	// db_code(content) - REMOVED: V2 version exists with same signature

	// db_blockquote(content, level) - LEGACY ONLY (V2 uses level, content order)
	loader.RegisterFunction(ScalarFunction("db_blockquote",
	    {LogicalType::VARCHAR, LogicalType::INTEGER}, duck_block_type, DbBlockquoteFun));
	// db_blockquote(content) - REMOVED: V2 version exists with same signature

	// db_list_block(items, ordered) - LEGACY ONLY (V2 uses ordered, items order)
	loader.RegisterFunction(ScalarFunction("db_list_block",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::BOOLEAN}, duck_block_type, DbListBlockFun));
	// db_list_block(items) - REMOVED: V2 version exists with same signature

	// db_hr() - REMOVED: V2 version exists with same signature

	// db_metadata(yaml_content) - REMOVED: V2 version exists with same signature

	// db_image - REMOVED: V2 versions exist with same signatures

	// db_raw(content, format) - REMOVED: V2 has same signature (VARCHAR, VARCHAR)
	// db_raw(content) - REMOVED: V2 version exists with same signature

	// ========================================================================
	// Flattening overloads - take children list, return flattened list
	// ========================================================================

	// db_paragraph(children LIST(duck_block)) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_paragraph",
	    {duck_block_list_type}, duck_block_list_type, DbParagraphFlattenFun));

	// db_heading(level, children LIST(duck_block)) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_heading",
	    {LogicalType::INTEGER, duck_block_list_type}, duck_block_list_type, DbHeadingFlattenFun));

	// db_blockquote(level, children LIST(duck_block)) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_blockquote",
	    {LogicalType::INTEGER, duck_block_list_type}, duck_block_list_type, DbBlockquoteFlattenFun));

	// db_blockquote(children LIST(duck_block)) -> LIST(duck_block) (level defaults to 1)
	loader.RegisterFunction(ScalarFunction("db_blockquote",
	    {duck_block_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &children_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto children_list = children_vec.GetValue(i);
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_BLOCKQUOTE, BlockTypes::KIND_BLOCK,
			                                                               Value(1), BlockTypes::ENCODING_TEXT, {});
			    auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// db_code(language, children LIST(duck_block)) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_code",
	    {LogicalType::VARCHAR, duck_block_list_type}, duck_block_list_type, DbCodeFlattenFun));

	// db_code(children LIST(duck_block)) -> LIST(duck_block) (no language)
	loader.RegisterFunction(ScalarFunction("db_code",
	    {duck_block_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &children_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto children_list = children_vec.GetValue(i);
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_CODE, BlockTypes::KIND_BLOCK,
			                                                               Value(1), BlockTypes::ENCODING_TEXT, {});
			    auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// ========================================================================
	// Nested list overloads - accept LIST(LIST(duck_block)) and flatten
	// This enables: db_heading(1, [db_text('A'), db_bold('B')])
	// ========================================================================
	auto duck_block_nested_list_type = LogicalType::LIST(duck_block_list_type);

	// db_heading(level, LIST(LIST(duck_block))) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_heading",
	    {LogicalType::INTEGER, duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &level_vec = args.data[0];
		    auto &nested_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto level = level_vec.GetValue(i);
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    int32_t heading_level = level.IsNull() ? 1 : level.GetValue<int32_t>();
			    map<string, string> attrs;
			    attrs["heading_level"] = to_string(heading_level);
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_HEADING, BlockTypes::KIND_BLOCK,
			                                                               Value(1), BlockTypes::ENCODING_TEXT, attrs);
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// db_paragraph(LIST(LIST(duck_block))) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_paragraph",
	    {duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &nested_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_PARAGRAPH, BlockTypes::KIND_BLOCK,
			                                                               Value(1), BlockTypes::ENCODING_TEXT, {});
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// db_blockquote(level, LIST(LIST(duck_block))) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_blockquote",
	    {LogicalType::INTEGER, duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &level_vec = args.data[0];
		    auto &nested_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto level = level_vec.GetValue(i);
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    int32_t quote_level = level.IsNull() ? 1 : level.GetValue<int32_t>();
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_BLOCKQUOTE, BlockTypes::KIND_BLOCK,
			                                                               Value(quote_level), BlockTypes::ENCODING_TEXT, {});
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, quote_level);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// db_blockquote(LIST(LIST(duck_block))) -> LIST(duck_block) (level defaults to 1)
	loader.RegisterFunction(ScalarFunction("db_blockquote",
	    {duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &nested_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_BLOCKQUOTE, BlockTypes::KIND_BLOCK,
			                                                               Value(1), BlockTypes::ENCODING_TEXT, {});
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// db_code(language, LIST(LIST(duck_block))) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_code",
	    {LogicalType::VARCHAR, duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &lang_vec = args.data[0];
		    auto &nested_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto lang = lang_vec.GetValue(i);
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    map<string, string> attrs;
			    if (!lang.IsNull()) attrs["language"] = lang.GetValue<string>();
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_CODE, BlockTypes::KIND_BLOCK,
			                                                               Value(1), BlockTypes::ENCODING_TEXT, attrs);
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// db_code(LIST(LIST(duck_block))) -> LIST(duck_block) (no language)
	loader.RegisterFunction(ScalarFunction("db_code",
	    {duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &nested_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_CODE, BlockTypes::KIND_BLOCK,
			                                                               Value(1), BlockTypes::ENCODING_TEXT, {});
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// ========================================================================
	// V2 API: All builders return LIST(duck_block)
	// Config params first, content last
	// These REPLACE legacy overloads by having different return type
	// ========================================================================

	// db_heading(level INTEGER, content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_heading",
	    {LogicalType::INTEGER, LogicalType::VARCHAR}, duck_block_list_type, DbHeadingV2Fun));

	// db_paragraph(content VARCHAR) -> LIST(duck_block)
	// V2: Returns LIST instead of single duck_block
	loader.RegisterFunction(ScalarFunction("db_paragraph",
	    {LogicalType::VARCHAR}, duck_block_list_type, DbParagraphV2Fun));

	// db_code(language VARCHAR, content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_code",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_list_type, DbCodeV2Fun));

	// db_code(content VARCHAR) -> LIST(duck_block) (no language)
	loader.RegisterFunction(ScalarFunction("db_code",
	    {LogicalType::VARCHAR}, duck_block_list_type, DbCodeV2NoLangFun));

	// db_blockquote(level INTEGER, content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_blockquote",
	    {LogicalType::INTEGER, LogicalType::VARCHAR}, duck_block_list_type, DbBlockquoteV2Fun));

	// db_blockquote(content VARCHAR) -> LIST(duck_block) (level defaults to 1)
	loader.RegisterFunction(ScalarFunction("db_blockquote",
	    {LogicalType::VARCHAR}, duck_block_list_type, DbBlockquoteV2NoLevelFun));

	// db_hr() -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_hr",
	    {}, duck_block_list_type, DbHrV2Fun));

	// db_metadata(yaml_content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_metadata",
	    {LogicalType::VARCHAR}, duck_block_list_type, DbMetadataV2Fun));

	// db_image(src VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_image",
	    {LogicalType::VARCHAR}, duck_block_list_type, DbImageV2Fun));

	// db_image(src VARCHAR, alt VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_image",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_list_type, DbImageV2Fun));

	// db_image(src VARCHAR, alt VARCHAR, title VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_image",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_list_type, DbImageV2Fun));

	// db_raw(content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_raw",
	    {LogicalType::VARCHAR}, duck_block_list_type, DbRawV2NoFormatFun));

	// db_raw(format VARCHAR, content VARCHAR) -> LIST(duck_block)
	// Note: V2 has format first

	// db_list_block(ordered BOOLEAN, items VARCHAR[]) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_list_block",
	    {LogicalType::BOOLEAN, LogicalType::LIST(LogicalType::VARCHAR)}, duck_block_list_type, DbListBlockV2Fun));

	// db_list_block(items VARCHAR[]) -> LIST(duck_block) (ordered defaults to false)
	loader.RegisterFunction(ScalarFunction("db_list_block",
	    {LogicalType::LIST(LogicalType::VARCHAR)}, duck_block_list_type, DbListBlockV2NoOrderFun));

	// db_list_item(content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_list_item",
	    {LogicalType::VARCHAR}, duck_block_list_type, DbListItemV2NoOrderFun));

	// db_list_item(ordered BOOLEAN, content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_list_item",
	    {LogicalType::BOOLEAN, LogicalType::VARCHAR}, duck_block_list_type, DbListItemV2Fun));

	// db_list_item(content LIST(duck_block)) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_list_item",
	    {duck_block_list_type}, duck_block_list_type, DbListItemV2NoOrderFun));

	// db_list_item(ordered BOOLEAN, content LIST(duck_block)) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_list_item",
	    {LogicalType::BOOLEAN, duck_block_list_type}, duck_block_list_type, DbListItemV2Fun));

	// db_list_item(content LIST(LIST(duck_block))) -> LIST(duck_block) (flatten nested lists)
	loader.RegisterFunction(ScalarFunction("db_list_item",
	    {duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &nested_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    map<string, string> attrs;
			    attrs["ordered"] = "false";
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_LIST_ITEM, BlockTypes::KIND_BLOCK,
			                                                               Value(1), BlockTypes::ENCODING_TEXT, attrs);
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// db_list_item(ordered BOOLEAN, content LIST(LIST(duck_block))) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_list_item",
	    {LogicalType::BOOLEAN, duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &ordered_vec = args.data[0];
		    auto &nested_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto ordered = ordered_vec.GetValue(i);
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    bool is_ordered = !ordered.IsNull() && ordered.GetValue<bool>();
			    map<string, string> attrs;
			    attrs["ordered"] = is_ordered ? "true" : "false";
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_LIST_ITEM, BlockTypes::KIND_BLOCK,
			                                                               Value(1), BlockTypes::ENCODING_TEXT, attrs);
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// db_raw(format VARCHAR, content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("db_raw",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_list_type, DbRawV2Fun));
}

} // namespace duckdb
