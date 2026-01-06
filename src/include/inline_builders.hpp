#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class InlineBuilderFunctions {
public:
	// Register all inline builder functions with the extension loader
	static void Register(ExtensionLoader &loader);

	// Helper to create a duck_block inline Value with the given fields
	static Value CreateInline(const string &inline_type, const string &content, const map<string, string> &attributes,
	                          int32_t level = 1, int32_t inline_order = 0);

	// Helper to create a duck_block inline Value with NULL content (for parent of children)
	static Value CreateInlineWithNullContent(const string &inline_type, const map<string, string> &attributes,
	                                         int32_t level = 1, int32_t inline_order = 0);

private:
	// Text and whitespace
	static void DbTextFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbSpaceFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbSoftBreakFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbLineBreakFun(DataChunk &args, ExpressionState &state, Vector &result);

	// Formatting (container types)
	static void DbBoldFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbItalicFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbStrikethroughFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbSuperscriptFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbSubscriptFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbSmallCapsFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbUnderlineFun(DataChunk &args, ExpressionState &state, Vector &result);

	// Semantic elements
	static void DbInlineCodeFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbMathFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbLinkFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbInlineImageFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbQuotedFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbCiteFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbNoteFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbSpanFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbRawInlineFun(DataChunk &args, ExpressionState &state, Vector &result);

	// Flattening overloads - return LIST(duck_block) with parent + children
	static void DbBoldFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbItalicFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbStrikethroughFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbSuperscriptFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbSubscriptFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbSmallCapsFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbUnderlineFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbLinkFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbQuotedFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbSpanFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbNoteFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
