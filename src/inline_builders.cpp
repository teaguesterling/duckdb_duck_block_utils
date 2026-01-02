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

// Helper to set all struct fields for an inline element
static void SetInlineFields(vector<unique_ptr<Vector>> &entries, idx_t i,
                            const char *element_type, const Value &content,
                            const char *encoding, const map<string, string> &attrs,
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
                                           const map<string, string> &attributes,
                                           int32_t level, int32_t inline_order) {
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

// ============================================================================
// Text and whitespace
// ============================================================================

void InlineBuilderFunctions::DocTextFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_TEXT,
		                content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DocSpaceFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_SPACE,
		                Value(""), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DocSoftBreakFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_SOFTBREAK,
		                Value(""), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DocLineBreakFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_LINEBREAK,
		                Value(""), BlockTypes::ENCODING_TEXT, {});
	}
}

// ============================================================================
// Formatting (container types)
// ============================================================================

void InlineBuilderFunctions::DocBoldFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_BOLD,
		                content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DocItalicFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_ITALIC,
		                content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DocStrikethroughFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_STRIKETHROUGH,
		                content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DocSuperscriptFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_SUPERSCRIPT,
		                content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DocSubscriptFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_SUBSCRIPT,
		                content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DocSmallCapsFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_SMALLCAPS,
		                content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DocUnderlineFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_UNDERLINE,
		                content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, {});
	}
}

// ============================================================================
// Semantic elements
// ============================================================================

void InlineBuilderFunctions::DocInlineCodeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_CODE,
		                content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DocMathFun(DataChunk &args, ExpressionState &state, Vector &result) {
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
		SetInlineFields(entries, i, BlockTypes::INLINE_MATH,
		                content_vec.GetValue(i), BlockTypes::ENCODING_LATEX, attrs);
	}
}

void InlineBuilderFunctions::DocLinkFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &text_vec = args.data[0];
	auto &href_vec = args.data[1];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);
	bool has_title = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		auto text = text_vec.GetValue(i);
		auto href = href_vec.GetValue(i);

		map<string, string> attrs;
		if (!href.IsNull()) attrs["href"] = href.GetValue<string>();
		if (has_title) {
			auto title = args.data[2].GetValue(i);
			if (!title.IsNull()) attrs["title"] = title.GetValue<string>();
		}

		string content = text.IsNull() ? "" : text.GetValue<string>();
		SetInlineFields(entries, i, BlockTypes::INLINE_LINK,
		                Value(content), BlockTypes::ENCODING_TEXT, attrs);
	}
}

void InlineBuilderFunctions::DocInlineImageFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &src_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);
	bool has_alt = args.ColumnCount() > 1;
	bool has_title = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		auto src = src_vec.GetValue(i);

		map<string, string> attrs;
		if (!src.IsNull()) attrs["src"] = src.GetValue<string>();

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

		SetInlineFields(entries, i, BlockTypes::INLINE_IMAGE,
		                Value(content), BlockTypes::ENCODING_TEXT, attrs);
	}
}

void InlineBuilderFunctions::DocQuotedFun(DataChunk &args, ExpressionState &state, Vector &result) {
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
		SetInlineFields(entries, i, BlockTypes::INLINE_QUOTED,
		                content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, attrs);
	}
}

void InlineBuilderFunctions::DocCiteFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &key_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);
	bool has_prefix = args.ColumnCount() > 1;
	bool has_suffix = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		auto key = key_vec.GetValue(i);

		map<string, string> attrs;
		if (!key.IsNull()) attrs["key"] = key.GetValue<string>();
		if (has_prefix) {
			auto prefix = args.data[1].GetValue(i);
			if (!prefix.IsNull()) attrs["prefix"] = prefix.GetValue<string>();
		}
		if (has_suffix) {
			auto suffix = args.data[2].GetValue(i);
			if (!suffix.IsNull()) attrs["suffix"] = suffix.GetValue<string>();
		}

		string content = key.IsNull() ? "" : key.GetValue<string>();
		SetInlineFields(entries, i, BlockTypes::INLINE_CITE,
		                Value(content), BlockTypes::ENCODING_TEXT, attrs);
	}
}

void InlineBuilderFunctions::DocNoteFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(entries, i, BlockTypes::INLINE_NOTE,
		                content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, {});
	}
}

void InlineBuilderFunctions::DocSpanFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);
	bool has_id = args.ColumnCount() > 1;
	bool has_classes = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		map<string, string> attrs;
		if (has_id) {
			auto id = args.data[1].GetValue(i);
			if (!id.IsNull()) attrs["id"] = id.GetValue<string>();
		}
		if (has_classes) {
			auto classes = args.data[2].GetValue(i);
			if (!classes.IsNull()) attrs["class"] = classes.GetValue<string>();
		}
		SetInlineFields(entries, i, BlockTypes::INLINE_SPAN,
		                content_vec.GetValue(i), BlockTypes::ENCODING_TEXT, attrs);
	}
}

void InlineBuilderFunctions::DocRawInlineFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);
	bool has_format = args.ColumnCount() > 1;

	for (idx_t i = 0; i < count; i++) {
		string format_str = "html";
		if (has_format) {
			auto format = args.data[1].GetValue(i);
			if (!format.IsNull()) format_str = format.GetValue<string>();
		}

		map<string, string> attrs;
		attrs["format"] = format_str;

		const char *encoding = BlockTypes::ENCODING_HTML;
		if (format_str == "latex") encoding = BlockTypes::ENCODING_LATEX;
		else if (format_str == "xml") encoding = BlockTypes::ENCODING_XML;

		SetInlineFields(entries, i, BlockTypes::INLINE_RAW,
		                content_vec.GetValue(i), encoding, attrs);
	}
}

// ============================================================================
// Registration
// ============================================================================

void InlineBuilderFunctions::Register(ExtensionLoader &loader) {
	auto doc_element_type = BlockTypes::DocElementType();

	// Text and whitespace
	loader.RegisterFunction(ScalarFunction("doc_text", {LogicalType::VARCHAR}, doc_element_type, DocTextFun));
	loader.RegisterFunction(ScalarFunction("doc_space", {}, doc_element_type, DocSpaceFun));
	loader.RegisterFunction(ScalarFunction("doc_softbreak", {}, doc_element_type, DocSoftBreakFun));
	loader.RegisterFunction(ScalarFunction("doc_linebreak", {}, doc_element_type, DocLineBreakFun));

	// Formatting
	loader.RegisterFunction(ScalarFunction("doc_bold", {LogicalType::VARCHAR}, doc_element_type, DocBoldFun));
	loader.RegisterFunction(ScalarFunction("doc_italic", {LogicalType::VARCHAR}, doc_element_type, DocItalicFun));
	loader.RegisterFunction(ScalarFunction("doc_strikethrough", {LogicalType::VARCHAR}, doc_element_type, DocStrikethroughFun));
	loader.RegisterFunction(ScalarFunction("doc_superscript", {LogicalType::VARCHAR}, doc_element_type, DocSuperscriptFun));
	loader.RegisterFunction(ScalarFunction("doc_subscript", {LogicalType::VARCHAR}, doc_element_type, DocSubscriptFun));
	loader.RegisterFunction(ScalarFunction("doc_smallcaps", {LogicalType::VARCHAR}, doc_element_type, DocSmallCapsFun));
	loader.RegisterFunction(ScalarFunction("doc_underline", {LogicalType::VARCHAR}, doc_element_type, DocUnderlineFun));

	// Semantic - inline code
	loader.RegisterFunction(ScalarFunction("doc_inline_code", {LogicalType::VARCHAR}, doc_element_type, DocInlineCodeFun));

	// Semantic - math
	loader.RegisterFunction(ScalarFunction("doc_math", {LogicalType::VARCHAR}, doc_element_type, DocMathFun));
	loader.RegisterFunction(ScalarFunction("doc_math", {LogicalType::VARCHAR, LogicalType::BOOLEAN}, doc_element_type, DocMathFun));

	// Semantic - link
	loader.RegisterFunction(ScalarFunction("doc_link", {LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_element_type, DocLinkFun));
	loader.RegisterFunction(ScalarFunction("doc_link", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_element_type, DocLinkFun));

	// Semantic - inline image
	loader.RegisterFunction(ScalarFunction("doc_inline_image", {LogicalType::VARCHAR}, doc_element_type, DocInlineImageFun));
	loader.RegisterFunction(ScalarFunction("doc_inline_image", {LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_element_type, DocInlineImageFun));
	loader.RegisterFunction(ScalarFunction("doc_inline_image", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_element_type, DocInlineImageFun));

	// Semantic - quoted
	loader.RegisterFunction(ScalarFunction("doc_quoted", {LogicalType::VARCHAR}, doc_element_type, DocQuotedFun));
	loader.RegisterFunction(ScalarFunction("doc_quoted", {LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_element_type, DocQuotedFun));

	// Semantic - citation
	loader.RegisterFunction(ScalarFunction("doc_cite", {LogicalType::VARCHAR}, doc_element_type, DocCiteFun));
	loader.RegisterFunction(ScalarFunction("doc_cite", {LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_element_type, DocCiteFun));
	loader.RegisterFunction(ScalarFunction("doc_cite", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_element_type, DocCiteFun));

	// Semantic - footnote
	loader.RegisterFunction(ScalarFunction("doc_note", {LogicalType::VARCHAR}, doc_element_type, DocNoteFun));

	// Semantic - span
	loader.RegisterFunction(ScalarFunction("doc_span", {LogicalType::VARCHAR}, doc_element_type, DocSpanFun));
	loader.RegisterFunction(ScalarFunction("doc_span", {LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_element_type, DocSpanFun));
	loader.RegisterFunction(ScalarFunction("doc_span", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_element_type, DocSpanFun));

	// Semantic - raw inline
	loader.RegisterFunction(ScalarFunction("doc_raw_inline", {LogicalType::VARCHAR}, doc_element_type, DocRawInlineFun));
	loader.RegisterFunction(ScalarFunction("doc_raw_inline", {LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_element_type, DocRawInlineFun));
}

} // namespace duckdb
