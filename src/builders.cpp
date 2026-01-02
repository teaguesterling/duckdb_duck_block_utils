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

// Helper to create a child element with adjusted level and order
static Value CreateChildWithLevelAndOrder(const Value &element, int32_t new_level, int32_t new_order) {
	auto children = StructValue::GetChildren(element);
	children[BlockTypes::LEVEL_IDX] = Value(new_level);
	children[BlockTypes::ELEMENT_ORDER_IDX] = Value(new_order);
	return Value::STRUCT(BlockTypes::DuckBlockType(), std::move(children));
}

// Helper to flatten a parent block with children into a list
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

		// Create parent paragraph block (content is empty since children contain content)
		auto parent = CreateBlock(BlockTypes::TYPE_PARAGRAPH, "", Value(1), BlockTypes::ENCODING_TEXT, {}, 0);

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

		// Create parent heading block
		auto parent = CreateBlock(BlockTypes::TYPE_HEADING, "", Value(), BlockTypes::ENCODING_TEXT, attrs, 0);

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

		// Create parent blockquote
		auto parent = CreateBlock(BlockTypes::TYPE_BLOCKQUOTE, "", Value(level_val), BlockTypes::ENCODING_TEXT, {}, 0);

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

		// Create parent code block
		auto parent = CreateBlock(BlockTypes::TYPE_CODE, "", Value(), BlockTypes::ENCODING_TEXT, attrs, 0);

		// Flatten with parent at level 1
		auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void BuilderFunctions::Register(ExtensionLoader &loader) {
	auto duck_block_type = BlockTypes::DuckBlockType();
	auto duck_block_list_type = BlockTypes::DuckBlockListType();

	// db_heading(content, level)
	loader.RegisterFunction(ScalarFunction("db_heading",
	    {LogicalType::VARCHAR, LogicalType::INTEGER}, duck_block_type, DbHeadingFun));

	// db_paragraph(content)
	loader.RegisterFunction(ScalarFunction("db_paragraph",
	    {LogicalType::VARCHAR}, duck_block_type, DbParagraphFun));

	// db_code(content, language) and db_code(content)
	loader.RegisterFunction(ScalarFunction("db_code",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_type, DbCodeFun));
	loader.RegisterFunction(ScalarFunction("db_code",
	    {LogicalType::VARCHAR}, duck_block_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto count = args.size();
		    auto &entries = StructVector::GetEntries(result);
		    for (idx_t i = 0; i < count; i++) {
			    SetBlockFields(entries, i, BlockTypes::TYPE_CODE,
			                   args.data[0].GetValue(i), Value(),
			                   BlockTypes::ENCODING_TEXT, CreateAttributesMap({}));
		    }
	    }));

	// db_blockquote(content, level) and db_blockquote(content)
	loader.RegisterFunction(ScalarFunction("db_blockquote",
	    {LogicalType::VARCHAR, LogicalType::INTEGER}, duck_block_type, DbBlockquoteFun));
	loader.RegisterFunction(ScalarFunction("db_blockquote",
	    {LogicalType::VARCHAR}, duck_block_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto count = args.size();
		    auto &entries = StructVector::GetEntries(result);
		    for (idx_t i = 0; i < count; i++) {
			    SetBlockFields(entries, i, BlockTypes::TYPE_BLOCKQUOTE,
			                   args.data[0].GetValue(i), Value(1),
			                   BlockTypes::ENCODING_TEXT, CreateAttributesMap({}));
		    }
	    }));

	// db_list_block(items, ordered) and db_list_block(items)
	loader.RegisterFunction(ScalarFunction("db_list_block",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::BOOLEAN}, duck_block_type, DbListBlockFun));
	loader.RegisterFunction(ScalarFunction("db_list_block",
	    {LogicalType::LIST(LogicalType::VARCHAR)}, duck_block_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto count = args.size();
		    auto &entries = StructVector::GetEntries(result);
		    for (idx_t i = 0; i < count; i++) {
			    auto items = args.data[0].GetValue(i);
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
			    attrs["ordered"] = "false";
			    SetBlockFields(entries, i, BlockTypes::TYPE_LIST,
			                   Value(json), Value(1),
			                   BlockTypes::ENCODING_JSON, CreateAttributesMap(attrs));
		    }
	    }));

	// db_hr()
	loader.RegisterFunction(ScalarFunction("db_hr", {}, duck_block_type, DbHrFun));

	// db_metadata(yaml_content)
	loader.RegisterFunction(ScalarFunction("db_metadata",
	    {LogicalType::VARCHAR}, duck_block_type, DbMetadataFun));

	// db_image(src, alt, title), db_image(src, alt), db_image(src)
	loader.RegisterFunction(ScalarFunction("db_image",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_type, DbImageFun));
	loader.RegisterFunction(ScalarFunction("db_image",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto count = args.size();
		    auto &entries = StructVector::GetEntries(result);
		    for (idx_t i = 0; i < count; i++) {
			    auto src = args.data[0].GetValue(i);
			    auto alt = args.data[1].GetValue(i);
			    map<string, string> attrs;
			    attrs["src"] = src.IsNull() ? "" : src.GetValue<string>();
			    if (!alt.IsNull()) attrs["alt"] = alt.GetValue<string>();
			    string content = alt.IsNull() ? "" : alt.GetValue<string>();
			    SetBlockFields(entries, i, BlockTypes::TYPE_IMAGE,
			                   Value(content), Value(),
			                   BlockTypes::ENCODING_TEXT, CreateAttributesMap(attrs));
		    }
	    }));
	loader.RegisterFunction(ScalarFunction("db_image",
	    {LogicalType::VARCHAR}, duck_block_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto count = args.size();
		    auto &entries = StructVector::GetEntries(result);
		    for (idx_t i = 0; i < count; i++) {
			    auto src = args.data[0].GetValue(i);
			    map<string, string> attrs;
			    attrs["src"] = src.IsNull() ? "" : src.GetValue<string>();
			    SetBlockFields(entries, i, BlockTypes::TYPE_IMAGE,
			                   Value(""), Value(),
			                   BlockTypes::ENCODING_TEXT, CreateAttributesMap(attrs));
		    }
	    }));

	// db_raw(content, format) and db_raw(content)
	loader.RegisterFunction(ScalarFunction("db_raw",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_type, DbRawFun));
	loader.RegisterFunction(ScalarFunction("db_raw",
	    {LogicalType::VARCHAR}, duck_block_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto count = args.size();
		    auto &entries = StructVector::GetEntries(result);
		    for (idx_t i = 0; i < count; i++) {
			    map<string, string> attrs;
			    attrs["format"] = "html";
			    SetBlockFields(entries, i, BlockTypes::TYPE_RAW,
			                   args.data[0].GetValue(i), Value(),
			                   BlockTypes::ENCODING_HTML, CreateAttributesMap(attrs));
		    }
	    }));

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
			    auto parent = BuilderFunctions::CreateBlock(BlockTypes::TYPE_BLOCKQUOTE, "", Value(1), BlockTypes::ENCODING_TEXT, {}, 0);
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
			    auto parent = BuilderFunctions::CreateBlock(BlockTypes::TYPE_CODE, "", Value(), BlockTypes::ENCODING_TEXT, {}, 0);
			    auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));
}

} // namespace duckdb
