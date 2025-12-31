#include "builders.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"

namespace duckdb {

// Helper to create an attributes MAP from a std::map
static Value CreateAttributesMap(const map<string, string> &attrs) {
	vector<Value> keys;
	vector<Value> values;
	for (auto &[k, v] : attrs) {
		keys.push_back(Value(k));
		values.push_back(Value(v));
	}
	return Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values);
}

Value BuilderFunctions::CreateBlock(const string &block_type, const string &content,
                                    const Value &level, const string &encoding,
                                    const map<string, string> &attributes, int32_t block_order) {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("block_type", Value(block_type)));
	struct_values.push_back(make_pair("content", Value(content)));
	struct_values.push_back(make_pair("level", level));
	struct_values.push_back(make_pair("encoding", Value(encoding)));
	struct_values.push_back(make_pair("attributes", CreateAttributesMap(attributes)));
	struct_values.push_back(make_pair("block_order", Value(block_order)));

	return Value::STRUCT(std::move(struct_values));
}

void BuilderFunctions::DocHeadingFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto &level_vec = args.data[1];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);
		auto level = level_vec.GetValue(i);

		// Set struct fields
		struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, Value(BlockTypes::TYPE_HEADING));
		struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, content);
		struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, level);
		struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(BlockTypes::ENCODING_TEXT));
		struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, CreateAttributesMap({}));
		struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
	}
}

void BuilderFunctions::DocParagraphFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, Value(BlockTypes::TYPE_PARAGRAPH));
		struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, content);
		struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value());  // NULL
		struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(BlockTypes::ENCODING_TEXT));
		struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, CreateAttributesMap({}));
		struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
	}
}

void BuilderFunctions::DocCodeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto &language_vec = args.data[1];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);
		auto language = language_vec.GetValue(i);

		map<string, string> attrs;
		if (!language.IsNull()) {
			attrs["language"] = language.GetValue<string>();
		}

		struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, Value(BlockTypes::TYPE_CODE));
		struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, content);
		struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value());  // NULL
		struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(BlockTypes::ENCODING_TEXT));
		struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, CreateAttributesMap(attrs));
		struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
	}
}

void BuilderFunctions::DocBlockquoteFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto &level_vec = args.data[1];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);
		auto level = level_vec.GetValue(i);

		// Default level to 1 if NULL
		if (level.IsNull()) {
			level = Value(1);
		}

		struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, Value(BlockTypes::TYPE_BLOCKQUOTE));
		struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, content);
		struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, level);
		struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(BlockTypes::ENCODING_TEXT));
		struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, CreateAttributesMap({}));
		struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
	}
}

void BuilderFunctions::DocListBlockFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &items_vec = args.data[0];
	auto &ordered_vec = args.data[1];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto items = items_vec.GetValue(i);
		auto ordered = ordered_vec.GetValue(i);

		// Convert items list to JSON array string
		string json_content = "[";
		if (!items.IsNull()) {
			auto &items_list = ListValue::GetChildren(items);
			bool first = true;
			for (auto &item : items_list) {
				if (!first) json_content += ",";
				first = false;
				// Simple JSON string escaping
				string item_str = item.IsNull() ? "" : item.GetValue<string>();
				json_content += "\"";
				for (char c : item_str) {
					if (c == '"') json_content += "\\\"";
					else if (c == '\\') json_content += "\\\\";
					else if (c == '\n') json_content += "\\n";
					else if (c == '\r') json_content += "\\r";
					else if (c == '\t') json_content += "\\t";
					else json_content += c;
				}
				json_content += "\"";
			}
		}
		json_content += "]";

		map<string, string> attrs;
		if (!ordered.IsNull() && ordered.GetValue<bool>()) {
			attrs["ordered"] = "true";
		} else {
			attrs["ordered"] = "false";
		}

		struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, Value(BlockTypes::TYPE_LIST));
		struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, Value(json_content));
		struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value(1));
		struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(BlockTypes::ENCODING_JSON));
		struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, CreateAttributesMap(attrs));
		struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
	}
}

void BuilderFunctions::DocHrFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, Value(BlockTypes::TYPE_HR));
		struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, Value(""));
		struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value());  // NULL
		struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(BlockTypes::ENCODING_TEXT));
		struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, CreateAttributesMap({}));
		struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
	}
}

void BuilderFunctions::DocMetadataFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, Value(BlockTypes::TYPE_METADATA));
		struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, content);
		struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value(0));  // Metadata is level 0
		struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(BlockTypes::ENCODING_YAML));
		struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, CreateAttributesMap({}));
		struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
	}
}

void BuilderFunctions::DocImageFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &src_vec = args.data[0];
	auto &alt_vec = args.data[1];
	auto &title_vec = args.data[2];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto src = src_vec.GetValue(i);
		auto alt = alt_vec.GetValue(i);
		auto title = title_vec.GetValue(i);

		map<string, string> attrs;
		attrs["src"] = src.IsNull() ? "" : src.GetValue<string>();
		if (!alt.IsNull()) {
			attrs["alt"] = alt.GetValue<string>();
		}
		if (!title.IsNull()) {
			attrs["title"] = title.GetValue<string>();
		}

		// Content is the alt text for simple representation
		string content = alt.IsNull() ? "" : alt.GetValue<string>();

		struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, Value(BlockTypes::TYPE_IMAGE));
		struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, Value(content));
		struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value());  // NULL
		struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(BlockTypes::ENCODING_TEXT));
		struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, CreateAttributesMap(attrs));
		struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
	}
}

void BuilderFunctions::DocRawFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto &format_vec = args.data[1];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);
		auto format = format_vec.GetValue(i);

		// Default format to html if NULL
		string format_str = format.IsNull() ? "html" : format.GetValue<string>();

		map<string, string> attrs;
		attrs["format"] = format_str;

		// Use the format as encoding
		string encoding = BlockTypes::ENCODING_HTML;
		if (format_str == "xml") encoding = BlockTypes::ENCODING_XML;
		else if (format_str == "html") encoding = BlockTypes::ENCODING_HTML;
		else encoding = BlockTypes::ENCODING_TEXT;

		struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, Value(BlockTypes::TYPE_RAW));
		struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, content);
		struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value());  // NULL
		struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(encoding));
		struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, CreateAttributesMap(attrs));
		struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
	}
}

void BuilderFunctions::Register(ExtensionLoader &loader) {
	auto doc_block_type = BlockTypes::DocBlockType();

	// doc_heading(content VARCHAR, level INTEGER) -> doc_block
	auto heading_func = ScalarFunction(
	    "doc_heading",
	    {LogicalType::VARCHAR, LogicalType::INTEGER},
	    doc_block_type,
	    DocHeadingFun
	);
	loader.RegisterFunction(heading_func);

	// doc_paragraph(content VARCHAR) -> doc_block
	auto paragraph_func = ScalarFunction(
	    "doc_paragraph",
	    {LogicalType::VARCHAR},
	    doc_block_type,
	    DocParagraphFun
	);
	loader.RegisterFunction(paragraph_func);

	// doc_code(content VARCHAR, language VARCHAR) -> doc_block
	// With optional language (can be NULL)
	auto code_func = ScalarFunction(
	    "doc_code",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR},
	    doc_block_type,
	    DocCodeFun
	);
	loader.RegisterFunction(code_func);

	// Also register single-arg version for convenience
	auto code_func_simple = ScalarFunction(
	    "doc_code",
	    {LogicalType::VARCHAR},
	    doc_block_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    // Single arg version - no language
		    auto &content_vec = args.data[0];
		    auto count = args.size();
		    auto &struct_entries = StructVector::GetEntries(result);

		    for (idx_t i = 0; i < count; i++) {
			    auto content = content_vec.GetValue(i);

			    struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, Value(BlockTypes::TYPE_CODE));
			    struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, content);
			    struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value());
			    struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(BlockTypes::ENCODING_TEXT));
			    struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i,
			        Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, {}, {}));
			    struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
		    }
	    }
	);
	loader.RegisterFunction(code_func_simple);

	// doc_blockquote(content VARCHAR, level INTEGER) -> doc_block
	auto blockquote_func = ScalarFunction(
	    "doc_blockquote",
	    {LogicalType::VARCHAR, LogicalType::INTEGER},
	    doc_block_type,
	    DocBlockquoteFun
	);
	loader.RegisterFunction(blockquote_func);

	// Single-arg version with default level 1
	auto blockquote_func_simple = ScalarFunction(
	    "doc_blockquote",
	    {LogicalType::VARCHAR},
	    doc_block_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &content_vec = args.data[0];
		    auto count = args.size();
		    auto &struct_entries = StructVector::GetEntries(result);

		    for (idx_t i = 0; i < count; i++) {
			    auto content = content_vec.GetValue(i);

			    struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, Value(BlockTypes::TYPE_BLOCKQUOTE));
			    struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, content);
			    struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value(1));
			    struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(BlockTypes::ENCODING_TEXT));
			    struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i,
			        Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, {}, {}));
			    struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
		    }
	    }
	);
	loader.RegisterFunction(blockquote_func_simple);

	// doc_list_block(items VARCHAR[], ordered BOOLEAN) -> doc_block
	auto list_func = ScalarFunction(
	    "doc_list_block",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::BOOLEAN},
	    doc_block_type,
	    DocListBlockFun
	);
	loader.RegisterFunction(list_func);

	// Single-arg version (unordered by default)
	auto list_func_simple = ScalarFunction(
	    "doc_list_block",
	    {LogicalType::LIST(LogicalType::VARCHAR)},
	    doc_block_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &items_vec = args.data[0];
		    auto count = args.size();
		    auto &struct_entries = StructVector::GetEntries(result);

		    for (idx_t i = 0; i < count; i++) {
			    auto items = items_vec.GetValue(i);

			    // Convert items list to JSON array string
			    string json_content = "[";
			    if (!items.IsNull()) {
				    auto &items_list = ListValue::GetChildren(items);
				    bool first = true;
				    for (auto &item : items_list) {
					    if (!first) json_content += ",";
					    first = false;
					    string item_str = item.IsNull() ? "" : item.GetValue<string>();
					    json_content += "\"";
					    for (char c : item_str) {
						    if (c == '"') json_content += "\\\"";
						    else if (c == '\\') json_content += "\\\\";
						    else if (c == '\n') json_content += "\\n";
						    else if (c == '\r') json_content += "\\r";
						    else if (c == '\t') json_content += "\\t";
						    else json_content += c;
					    }
					    json_content += "\"";
				    }
			    }
			    json_content += "]";

			    vector<Value> keys = {Value("ordered")};
			    vector<Value> values = {Value("false")};

			    struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, Value(BlockTypes::TYPE_LIST));
			    struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, Value(json_content));
			    struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value(1));
			    struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(BlockTypes::ENCODING_JSON));
			    struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i,
			        Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values));
			    struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
		    }
	    }
	);
	loader.RegisterFunction(list_func_simple);

	// doc_hr() -> doc_block
	auto hr_func = ScalarFunction(
	    "doc_hr",
	    {},
	    doc_block_type,
	    DocHrFun
	);
	loader.RegisterFunction(hr_func);

	// doc_metadata(yaml_content VARCHAR) -> doc_block
	auto metadata_func = ScalarFunction(
	    "doc_metadata",
	    {LogicalType::VARCHAR},
	    doc_block_type,
	    DocMetadataFun
	);
	loader.RegisterFunction(metadata_func);

	// doc_image(src VARCHAR, alt VARCHAR, title VARCHAR) -> doc_block
	auto image_func = ScalarFunction(
	    "doc_image",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
	    doc_block_type,
	    DocImageFun
	);
	loader.RegisterFunction(image_func);

	// Two-arg version (src, alt)
	auto image_func_2 = ScalarFunction(
	    "doc_image",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR},
	    doc_block_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &src_vec = args.data[0];
		    auto &alt_vec = args.data[1];
		    auto count = args.size();
		    auto &struct_entries = StructVector::GetEntries(result);

		    for (idx_t i = 0; i < count; i++) {
			    auto src = src_vec.GetValue(i);
			    auto alt = alt_vec.GetValue(i);

			    vector<Value> keys = {Value("src")};
			    vector<Value> values = {src.IsNull() ? Value("") : src};
			    if (!alt.IsNull()) {
				    keys.push_back(Value("alt"));
				    values.push_back(alt);
			    }

			    string content = alt.IsNull() ? "" : alt.GetValue<string>();

			    struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, Value(BlockTypes::TYPE_IMAGE));
			    struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, Value(content));
			    struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value());
			    struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(BlockTypes::ENCODING_TEXT));
			    struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i,
			        Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values));
			    struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
		    }
	    }
	);
	loader.RegisterFunction(image_func_2);

	// Single-arg version (src only)
	auto image_func_1 = ScalarFunction(
	    "doc_image",
	    {LogicalType::VARCHAR},
	    doc_block_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &src_vec = args.data[0];
		    auto count = args.size();
		    auto &struct_entries = StructVector::GetEntries(result);

		    for (idx_t i = 0; i < count; i++) {
			    auto src = src_vec.GetValue(i);

			    vector<Value> keys = {Value("src")};
			    vector<Value> values = {src.IsNull() ? Value("") : src};

			    struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, Value(BlockTypes::TYPE_IMAGE));
			    struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, Value(""));
			    struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value());
			    struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(BlockTypes::ENCODING_TEXT));
			    struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i,
			        Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values));
			    struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
		    }
	    }
	);
	loader.RegisterFunction(image_func_1);

	// doc_raw(content VARCHAR, format VARCHAR) -> doc_block
	auto raw_func = ScalarFunction(
	    "doc_raw",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR},
	    doc_block_type,
	    DocRawFun
	);
	loader.RegisterFunction(raw_func);

	// Single-arg version (html by default)
	auto raw_func_simple = ScalarFunction(
	    "doc_raw",
	    {LogicalType::VARCHAR},
	    doc_block_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &content_vec = args.data[0];
		    auto count = args.size();
		    auto &struct_entries = StructVector::GetEntries(result);

		    for (idx_t i = 0; i < count; i++) {
			    auto content = content_vec.GetValue(i);

			    vector<Value> keys = {Value("format")};
			    vector<Value> values = {Value("html")};

			    struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, Value(BlockTypes::TYPE_RAW));
			    struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, content);
			    struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value());
			    struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(BlockTypes::ENCODING_HTML));
			    struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i,
			        Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values));
			    struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
		    }
	    }
	);
	loader.RegisterFunction(raw_func_simple);
}

} // namespace duckdb
