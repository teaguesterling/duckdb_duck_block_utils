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

void BuilderFunctions::DocHeadingFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto &level_vec = args.data[1];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetBlockFields(entries, i, BlockTypes::TYPE_HEADING,
		               content_vec.GetValue(i), level_vec.GetValue(i),
		               BlockTypes::ENCODING_TEXT, CreateAttributesMap({}));
	}
}

void BuilderFunctions::DocParagraphFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetBlockFields(entries, i, BlockTypes::TYPE_PARAGRAPH,
		               content_vec.GetValue(i), Value(),
		               BlockTypes::ENCODING_TEXT, CreateAttributesMap({}));
	}
}

void BuilderFunctions::DocCodeFun(DataChunk &args, ExpressionState &state, Vector &result) {
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

void BuilderFunctions::DocBlockquoteFun(DataChunk &args, ExpressionState &state, Vector &result) {
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

void BuilderFunctions::DocListBlockFun(DataChunk &args, ExpressionState &state, Vector &result) {
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

void BuilderFunctions::DocHrFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetBlockFields(entries, i, BlockTypes::TYPE_HR,
		               Value(""), Value(),
		               BlockTypes::ENCODING_TEXT, CreateAttributesMap({}));
	}
}

void BuilderFunctions::DocMetadataFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetBlockFields(entries, i, BlockTypes::TYPE_METADATA,
		               content_vec.GetValue(i), Value(0),
		               BlockTypes::ENCODING_YAML, CreateAttributesMap({}));
	}
}

void BuilderFunctions::DocImageFun(DataChunk &args, ExpressionState &state, Vector &result) {
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

void BuilderFunctions::DocRawFun(DataChunk &args, ExpressionState &state, Vector &result) {
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

void BuilderFunctions::Register(ExtensionLoader &loader) {
	auto doc_element_type = BlockTypes::DocElementType();

	// doc_heading(content, level)
	loader.RegisterFunction(ScalarFunction("doc_heading",
	    {LogicalType::VARCHAR, LogicalType::INTEGER}, doc_element_type, DocHeadingFun));

	// doc_paragraph(content)
	loader.RegisterFunction(ScalarFunction("doc_paragraph",
	    {LogicalType::VARCHAR}, doc_element_type, DocParagraphFun));

	// doc_code(content, language) and doc_code(content)
	loader.RegisterFunction(ScalarFunction("doc_code",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_element_type, DocCodeFun));
	loader.RegisterFunction(ScalarFunction("doc_code",
	    {LogicalType::VARCHAR}, doc_element_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto count = args.size();
		    auto &entries = StructVector::GetEntries(result);
		    for (idx_t i = 0; i < count; i++) {
			    SetBlockFields(entries, i, BlockTypes::TYPE_CODE,
			                   args.data[0].GetValue(i), Value(),
			                   BlockTypes::ENCODING_TEXT, CreateAttributesMap({}));
		    }
	    }));

	// doc_blockquote(content, level) and doc_blockquote(content)
	loader.RegisterFunction(ScalarFunction("doc_blockquote",
	    {LogicalType::VARCHAR, LogicalType::INTEGER}, doc_element_type, DocBlockquoteFun));
	loader.RegisterFunction(ScalarFunction("doc_blockquote",
	    {LogicalType::VARCHAR}, doc_element_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto count = args.size();
		    auto &entries = StructVector::GetEntries(result);
		    for (idx_t i = 0; i < count; i++) {
			    SetBlockFields(entries, i, BlockTypes::TYPE_BLOCKQUOTE,
			                   args.data[0].GetValue(i), Value(1),
			                   BlockTypes::ENCODING_TEXT, CreateAttributesMap({}));
		    }
	    }));

	// doc_list_block(items, ordered) and doc_list_block(items)
	loader.RegisterFunction(ScalarFunction("doc_list_block",
	    {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::BOOLEAN}, doc_element_type, DocListBlockFun));
	loader.RegisterFunction(ScalarFunction("doc_list_block",
	    {LogicalType::LIST(LogicalType::VARCHAR)}, doc_element_type,
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

	// doc_hr()
	loader.RegisterFunction(ScalarFunction("doc_hr", {}, doc_element_type, DocHrFun));

	// doc_metadata(yaml_content)
	loader.RegisterFunction(ScalarFunction("doc_metadata",
	    {LogicalType::VARCHAR}, doc_element_type, DocMetadataFun));

	// doc_image(src, alt, title), doc_image(src, alt), doc_image(src)
	loader.RegisterFunction(ScalarFunction("doc_image",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_element_type, DocImageFun));
	loader.RegisterFunction(ScalarFunction("doc_image",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_element_type,
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
	loader.RegisterFunction(ScalarFunction("doc_image",
	    {LogicalType::VARCHAR}, doc_element_type,
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

	// doc_raw(content, format) and doc_raw(content)
	loader.RegisterFunction(ScalarFunction("doc_raw",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_element_type, DocRawFun));
	loader.RegisterFunction(ScalarFunction("doc_raw",
	    {LogicalType::VARCHAR}, doc_element_type,
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
}

} // namespace duckdb
