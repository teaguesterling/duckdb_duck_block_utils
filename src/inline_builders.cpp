#include "inline_builders.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"

namespace duckdb {

// Helper to create an attributes MAP from a std::map
static Value CreateInlineAttributesMap(const map<string, string> &attrs) {
	vector<Value> keys;
	vector<Value> values;
	for (auto &entry : attrs) {
		keys.push_back(Value(entry.first));
		values.push_back(Value(entry.second));
	}
	return Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values);
}

Value InlineBuilderFunctions::CreateInline(const string &inline_type, const string &content,
                                           const map<string, string> &attributes) {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("inline_type", Value(inline_type)));
	struct_values.push_back(make_pair("content", Value(content)));
	struct_values.push_back(make_pair("attributes", CreateInlineAttributesMap(attributes)));

	return Value::STRUCT(std::move(struct_values));
}

void InlineBuilderFunctions::DocTextFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		struct_entries[BlockTypes::INLINE_TYPE_IDX]->SetValue(i, Value(BlockTypes::INLINE_TEXT));
		struct_entries[BlockTypes::INLINE_CONTENT_IDX]->SetValue(i, content);
		struct_entries[BlockTypes::INLINE_ATTRIBUTES_IDX]->SetValue(i, CreateInlineAttributesMap({}));
	}
}

void InlineBuilderFunctions::DocLinkFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &text_vec = args.data[0];
	auto &href_vec = args.data[1];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	bool has_title = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		auto text = text_vec.GetValue(i);
		auto href = href_vec.GetValue(i);

		map<string, string> attrs;
		if (!href.IsNull()) {
			attrs["href"] = href.GetValue<string>();
		}
		if (has_title) {
			auto title = args.data[2].GetValue(i);
			if (!title.IsNull()) {
				attrs["title"] = title.GetValue<string>();
			}
		}

		string content = text.IsNull() ? "" : text.GetValue<string>();

		struct_entries[BlockTypes::INLINE_TYPE_IDX]->SetValue(i, Value(BlockTypes::INLINE_LINK));
		struct_entries[BlockTypes::INLINE_CONTENT_IDX]->SetValue(i, Value(content));
		struct_entries[BlockTypes::INLINE_ATTRIBUTES_IDX]->SetValue(i, CreateInlineAttributesMap(attrs));
	}
}

void InlineBuilderFunctions::DocInlineImageFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &src_vec = args.data[0];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	bool has_alt = args.ColumnCount() > 1;
	bool has_title = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		auto src = src_vec.GetValue(i);

		map<string, string> attrs;
		if (!src.IsNull()) {
			attrs["src"] = src.GetValue<string>();
		}

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
			if (!title.IsNull()) {
				attrs["title"] = title.GetValue<string>();
			}
		}

		struct_entries[BlockTypes::INLINE_TYPE_IDX]->SetValue(i, Value(BlockTypes::INLINE_IMAGE));
		struct_entries[BlockTypes::INLINE_CONTENT_IDX]->SetValue(i, Value(content));
		struct_entries[BlockTypes::INLINE_ATTRIBUTES_IDX]->SetValue(i, CreateInlineAttributesMap(attrs));
	}
}

void InlineBuilderFunctions::DocBoldFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		struct_entries[BlockTypes::INLINE_TYPE_IDX]->SetValue(i, Value(BlockTypes::INLINE_BOLD));
		struct_entries[BlockTypes::INLINE_CONTENT_IDX]->SetValue(i, content);
		struct_entries[BlockTypes::INLINE_ATTRIBUTES_IDX]->SetValue(i, CreateInlineAttributesMap({}));
	}
}

void InlineBuilderFunctions::DocItalicFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		struct_entries[BlockTypes::INLINE_TYPE_IDX]->SetValue(i, Value(BlockTypes::INLINE_ITALIC));
		struct_entries[BlockTypes::INLINE_CONTENT_IDX]->SetValue(i, content);
		struct_entries[BlockTypes::INLINE_ATTRIBUTES_IDX]->SetValue(i, CreateInlineAttributesMap({}));
	}
}

void InlineBuilderFunctions::DocInlineCodeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		struct_entries[BlockTypes::INLINE_TYPE_IDX]->SetValue(i, Value(BlockTypes::INLINE_CODE));
		struct_entries[BlockTypes::INLINE_CONTENT_IDX]->SetValue(i, content);
		struct_entries[BlockTypes::INLINE_ATTRIBUTES_IDX]->SetValue(i, CreateInlineAttributesMap({}));
	}
}

void InlineBuilderFunctions::DocStrikethroughFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		struct_entries[BlockTypes::INLINE_TYPE_IDX]->SetValue(i, Value(BlockTypes::INLINE_STRIKETHROUGH));
		struct_entries[BlockTypes::INLINE_CONTENT_IDX]->SetValue(i, content);
		struct_entries[BlockTypes::INLINE_ATTRIBUTES_IDX]->SetValue(i, CreateInlineAttributesMap({}));
	}
}

void InlineBuilderFunctions::Register(ExtensionLoader &loader) {
	auto doc_inline_type = BlockTypes::DocInlineType();

	// doc_text(content VARCHAR) -> doc_inline
	auto text_func = ScalarFunction(
	    "doc_text",
	    {LogicalType::VARCHAR},
	    doc_inline_type,
	    DocTextFun
	);
	loader.RegisterFunction(text_func);

	// doc_link(text VARCHAR, href VARCHAR) -> doc_inline
	auto link_func_2 = ScalarFunction(
	    "doc_link",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR},
	    doc_inline_type,
	    DocLinkFun
	);
	loader.RegisterFunction(link_func_2);

	// doc_link(text VARCHAR, href VARCHAR, title VARCHAR) -> doc_inline
	auto link_func_3 = ScalarFunction(
	    "doc_link",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
	    doc_inline_type,
	    DocLinkFun
	);
	loader.RegisterFunction(link_func_3);

	// doc_inline_image(src VARCHAR) -> doc_inline
	auto inline_image_func_1 = ScalarFunction(
	    "doc_inline_image",
	    {LogicalType::VARCHAR},
	    doc_inline_type,
	    DocInlineImageFun
	);
	loader.RegisterFunction(inline_image_func_1);

	// doc_inline_image(src VARCHAR, alt VARCHAR) -> doc_inline
	auto inline_image_func_2 = ScalarFunction(
	    "doc_inline_image",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR},
	    doc_inline_type,
	    DocInlineImageFun
	);
	loader.RegisterFunction(inline_image_func_2);

	// doc_inline_image(src VARCHAR, alt VARCHAR, title VARCHAR) -> doc_inline
	auto inline_image_func_3 = ScalarFunction(
	    "doc_inline_image",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
	    doc_inline_type,
	    DocInlineImageFun
	);
	loader.RegisterFunction(inline_image_func_3);

	// doc_bold(content VARCHAR) -> doc_inline
	auto bold_func = ScalarFunction(
	    "doc_bold",
	    {LogicalType::VARCHAR},
	    doc_inline_type,
	    DocBoldFun
	);
	loader.RegisterFunction(bold_func);

	// doc_italic(content VARCHAR) -> doc_inline
	auto italic_func = ScalarFunction(
	    "doc_italic",
	    {LogicalType::VARCHAR},
	    doc_inline_type,
	    DocItalicFun
	);
	loader.RegisterFunction(italic_func);

	// doc_inline_code(content VARCHAR) -> doc_inline
	auto inline_code_func = ScalarFunction(
	    "doc_inline_code",
	    {LogicalType::VARCHAR},
	    doc_inline_type,
	    DocInlineCodeFun
	);
	loader.RegisterFunction(inline_code_func);

	// doc_strikethrough(content VARCHAR) -> doc_inline
	auto strikethrough_func = ScalarFunction(
	    "doc_strikethrough",
	    {LogicalType::VARCHAR},
	    doc_inline_type,
	    DocStrikethroughFun
	);
	loader.RegisterFunction(strikethrough_func);
}

} // namespace duckdb
