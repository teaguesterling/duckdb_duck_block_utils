#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class InlineBuilderFunctions {
public:
	// Register all inline builder functions with the extension loader
	static void Register(ExtensionLoader &loader);

	// Helper to create a doc_inline Value with the given fields
	static Value CreateInline(const string &inline_type, const string &content,
	                          const map<string, string> &attributes,
	                          int32_t level = 1, int32_t inline_order = 0);

private:
	// Text and whitespace
	static void DocTextFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocSpaceFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocSoftBreakFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocLineBreakFun(DataChunk &args, ExpressionState &state, Vector &result);

	// Formatting (container types)
	static void DocBoldFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocItalicFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocStrikethroughFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocSuperscriptFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocSubscriptFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocSmallCapsFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocUnderlineFun(DataChunk &args, ExpressionState &state, Vector &result);

	// Semantic elements
	static void DocInlineCodeFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocMathFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocLinkFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocInlineImageFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocQuotedFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocCiteFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocNoteFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocSpanFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DocRawInlineFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
