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

// Helper to set all struct fields for an inline element
static void SetInlineFields(vector<unique_ptr<Vector>> &struct_entries, idx_t i,
                            const char *inline_type, const Value &content,
                            const map<string, string> &attrs,
                            int32_t level = 1, int32_t inline_order = 0) {
	struct_entries[BlockTypes::INLINE_TYPE_IDX]->SetValue(i, Value(inline_type));
	struct_entries[BlockTypes::INLINE_CONTENT_IDX]->SetValue(i, content);
	struct_entries[BlockTypes::INLINE_LEVEL_IDX]->SetValue(i, Value(level));
	struct_entries[BlockTypes::INLINE_ATTRIBUTES_IDX]->SetValue(i, CreateInlineAttributesMap(attrs));
	struct_entries[BlockTypes::INLINE_ORDER_IDX]->SetValue(i, Value(inline_order));
}

Value InlineBuilderFunctions::CreateInline(const string &inline_type, const string &content,
                                           const map<string, string> &attributes,
                                           int32_t level, int32_t inline_order) {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("inline_type", Value(inline_type)));
	struct_values.push_back(make_pair("content", Value(content)));
	struct_values.push_back(make_pair("level", Value(level)));
	struct_values.push_back(make_pair("attributes", CreateInlineAttributesMap(attributes)));
	struct_values.push_back(make_pair("inline_order", Value(inline_order)));

	return Value::STRUCT(std::move(struct_values));
}

// ============================================================================
// Text and whitespace
// ============================================================================

void InlineBuilderFunctions::DocTextFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);
		SetInlineFields(struct_entries, i, BlockTypes::INLINE_TEXT, content, {});
	}
}

void InlineBuilderFunctions::DocSpaceFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(struct_entries, i, BlockTypes::INLINE_SPACE, Value(" "), {});
	}
}

void InlineBuilderFunctions::DocSoftBreakFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(struct_entries, i, BlockTypes::INLINE_SOFTBREAK, Value(""), {});
	}
}

void InlineBuilderFunctions::DocLineBreakFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetInlineFields(struct_entries, i, BlockTypes::INLINE_LINEBREAK, Value("\n"), {});
	}
}

// ============================================================================
// Formatting (container types - content is for flat representation)
// ============================================================================

void InlineBuilderFunctions::DocBoldFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);
		SetInlineFields(struct_entries, i, BlockTypes::INLINE_BOLD, content, {});
	}
}

void InlineBuilderFunctions::DocItalicFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);
		SetInlineFields(struct_entries, i, BlockTypes::INLINE_ITALIC, content, {});
	}
}

void InlineBuilderFunctions::DocStrikethroughFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);
		SetInlineFields(struct_entries, i, BlockTypes::INLINE_STRIKETHROUGH, content, {});
	}
}

void InlineBuilderFunctions::DocSuperscriptFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);
		SetInlineFields(struct_entries, i, BlockTypes::INLINE_SUPERSCRIPT, content, {});
	}
}

void InlineBuilderFunctions::DocSubscriptFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);
		SetInlineFields(struct_entries, i, BlockTypes::INLINE_SUBSCRIPT, content, {});
	}
}

void InlineBuilderFunctions::DocSmallCapsFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);
		SetInlineFields(struct_entries, i, BlockTypes::INLINE_SMALLCAPS, content, {});
	}
}

void InlineBuilderFunctions::DocUnderlineFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);
		SetInlineFields(struct_entries, i, BlockTypes::INLINE_UNDERLINE, content, {});
	}
}

// ============================================================================
// Semantic elements
// ============================================================================

void InlineBuilderFunctions::DocInlineCodeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);
		SetInlineFields(struct_entries, i, BlockTypes::INLINE_CODE, content, {});
	}
}

void InlineBuilderFunctions::DocMathFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	bool has_display = args.ColumnCount() > 1;

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		map<string, string> attrs;
		if (has_display) {
			auto display = args.data[1].GetValue(i);
			if (!display.IsNull() && display.GetValue<bool>()) {
				attrs["display"] = "block";
			} else {
				attrs["display"] = "inline";
			}
		} else {
			attrs["display"] = "inline";
		}

		SetInlineFields(struct_entries, i, BlockTypes::INLINE_MATH, content, attrs);
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
		SetInlineFields(struct_entries, i, BlockTypes::INLINE_LINK, Value(content), attrs);
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

		SetInlineFields(struct_entries, i, BlockTypes::INLINE_IMAGE, Value(content), attrs);
	}
}

void InlineBuilderFunctions::DocQuotedFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	bool has_type = args.ColumnCount() > 1;

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		map<string, string> attrs;
		if (has_type) {
			auto quote_type = args.data[1].GetValue(i);
			if (!quote_type.IsNull()) {
				attrs["quote_type"] = quote_type.GetValue<string>();
			}
		} else {
			attrs["quote_type"] = "double";
		}

		SetInlineFields(struct_entries, i, BlockTypes::INLINE_QUOTED, content, attrs);
	}
}

void InlineBuilderFunctions::DocCiteFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &key_vec = args.data[0];
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	bool has_prefix = args.ColumnCount() > 1;
	bool has_suffix = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		auto key = key_vec.GetValue(i);

		map<string, string> attrs;
		if (!key.IsNull()) {
			attrs["key"] = key.GetValue<string>();
		}
		if (has_prefix) {
			auto prefix = args.data[1].GetValue(i);
			if (!prefix.IsNull()) {
				attrs["prefix"] = prefix.GetValue<string>();
			}
		}
		if (has_suffix) {
			auto suffix = args.data[2].GetValue(i);
			if (!suffix.IsNull()) {
				attrs["suffix"] = suffix.GetValue<string>();
			}
		}

		string content = key.IsNull() ? "" : key.GetValue<string>();
		SetInlineFields(struct_entries, i, BlockTypes::INLINE_CITE, Value(content), attrs);
	}
}

void InlineBuilderFunctions::DocNoteFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);
		SetInlineFields(struct_entries, i, BlockTypes::INLINE_NOTE, content, {});
	}
}

void InlineBuilderFunctions::DocSpanFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	bool has_id = args.ColumnCount() > 1;
	bool has_classes = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		map<string, string> attrs;
		if (has_id) {
			auto id = args.data[1].GetValue(i);
			if (!id.IsNull()) {
				attrs["id"] = id.GetValue<string>();
			}
		}
		if (has_classes) {
			auto classes = args.data[2].GetValue(i);
			if (!classes.IsNull()) {
				attrs["class"] = classes.GetValue<string>();
			}
		}

		SetInlineFields(struct_entries, i, BlockTypes::INLINE_SPAN, content, attrs);
	}
}

void InlineBuilderFunctions::DocRawInlineFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	bool has_format = args.ColumnCount() > 1;

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		map<string, string> attrs;
		if (has_format) {
			auto format = args.data[1].GetValue(i);
			if (!format.IsNull()) {
				attrs["format"] = format.GetValue<string>();
			}
		} else {
			attrs["format"] = "html";
		}

		SetInlineFields(struct_entries, i, BlockTypes::INLINE_RAW, content, attrs);
	}
}

// ============================================================================
// Registration
// ============================================================================

void InlineBuilderFunctions::Register(ExtensionLoader &loader) {
	auto doc_inline_type = BlockTypes::DocInlineType();

	// Text and whitespace
	loader.RegisterFunction(ScalarFunction("doc_text", {LogicalType::VARCHAR}, doc_inline_type, DocTextFun));
	loader.RegisterFunction(ScalarFunction("doc_space", {}, doc_inline_type, DocSpaceFun));
	loader.RegisterFunction(ScalarFunction("doc_softbreak", {}, doc_inline_type, DocSoftBreakFun));
	loader.RegisterFunction(ScalarFunction("doc_linebreak", {}, doc_inline_type, DocLineBreakFun));

	// Formatting
	loader.RegisterFunction(ScalarFunction("doc_bold", {LogicalType::VARCHAR}, doc_inline_type, DocBoldFun));
	loader.RegisterFunction(ScalarFunction("doc_italic", {LogicalType::VARCHAR}, doc_inline_type, DocItalicFun));
	loader.RegisterFunction(ScalarFunction("doc_strikethrough", {LogicalType::VARCHAR}, doc_inline_type, DocStrikethroughFun));
	loader.RegisterFunction(ScalarFunction("doc_superscript", {LogicalType::VARCHAR}, doc_inline_type, DocSuperscriptFun));
	loader.RegisterFunction(ScalarFunction("doc_subscript", {LogicalType::VARCHAR}, doc_inline_type, DocSubscriptFun));
	loader.RegisterFunction(ScalarFunction("doc_smallcaps", {LogicalType::VARCHAR}, doc_inline_type, DocSmallCapsFun));
	loader.RegisterFunction(ScalarFunction("doc_underline", {LogicalType::VARCHAR}, doc_inline_type, DocUnderlineFun));

	// Semantic - inline code
	loader.RegisterFunction(ScalarFunction("doc_inline_code", {LogicalType::VARCHAR}, doc_inline_type, DocInlineCodeFun));

	// Semantic - math
	loader.RegisterFunction(ScalarFunction("doc_math", {LogicalType::VARCHAR}, doc_inline_type, DocMathFun));
	loader.RegisterFunction(ScalarFunction("doc_math", {LogicalType::VARCHAR, LogicalType::BOOLEAN}, doc_inline_type, DocMathFun));

	// Semantic - link (multiple overloads)
	loader.RegisterFunction(ScalarFunction("doc_link", {LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_inline_type, DocLinkFun));
	loader.RegisterFunction(ScalarFunction("doc_link", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_inline_type, DocLinkFun));

	// Semantic - inline image (multiple overloads)
	loader.RegisterFunction(ScalarFunction("doc_inline_image", {LogicalType::VARCHAR}, doc_inline_type, DocInlineImageFun));
	loader.RegisterFunction(ScalarFunction("doc_inline_image", {LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_inline_type, DocInlineImageFun));
	loader.RegisterFunction(ScalarFunction("doc_inline_image", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_inline_type, DocInlineImageFun));

	// Semantic - quoted
	loader.RegisterFunction(ScalarFunction("doc_quoted", {LogicalType::VARCHAR}, doc_inline_type, DocQuotedFun));
	loader.RegisterFunction(ScalarFunction("doc_quoted", {LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_inline_type, DocQuotedFun));

	// Semantic - citation
	loader.RegisterFunction(ScalarFunction("doc_cite", {LogicalType::VARCHAR}, doc_inline_type, DocCiteFun));
	loader.RegisterFunction(ScalarFunction("doc_cite", {LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_inline_type, DocCiteFun));
	loader.RegisterFunction(ScalarFunction("doc_cite", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_inline_type, DocCiteFun));

	// Semantic - footnote
	loader.RegisterFunction(ScalarFunction("doc_note", {LogicalType::VARCHAR}, doc_inline_type, DocNoteFun));

	// Semantic - span (with optional id and classes)
	loader.RegisterFunction(ScalarFunction("doc_span", {LogicalType::VARCHAR}, doc_inline_type, DocSpanFun));
	loader.RegisterFunction(ScalarFunction("doc_span", {LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_inline_type, DocSpanFun));
	loader.RegisterFunction(ScalarFunction("doc_span", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_inline_type, DocSpanFun));

	// Semantic - raw inline
	loader.RegisterFunction(ScalarFunction("doc_raw_inline", {LogicalType::VARCHAR}, doc_inline_type, DocRawInlineFun));
	loader.RegisterFunction(ScalarFunction("doc_raw_inline", {LogicalType::VARCHAR, LogicalType::VARCHAR}, doc_inline_type, DocRawInlineFun));
}

} // namespace duckdb
