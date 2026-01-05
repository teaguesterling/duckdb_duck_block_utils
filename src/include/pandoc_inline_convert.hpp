#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class PandocInlineConvert {
public:
	// Register Pandoc inline conversion functions
	static void Register(ExtensionLoader &loader);

	// Convert a list of duck_block inline Values to Pandoc JSON string
	static string ConvertInlinesToPandocJson(const vector<Value> &inlines);

private:
	// pandoc_inlines_to_db_inlines(json) -> LIST(duck_block)
	// Converts nested Pandoc inline JSON to flat duck_block inline rows
	static void PandocInlinesToDbInlinesFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_inlines_to_pandoc(LIST(duck_block)) -> JSON
	// Converts flat duck_block inline rows back to nested Pandoc inline JSON
	static void DbInlinesToPandocFun(DataChunk &args, ExpressionState &state, Vector &result);

	// pandoc_inlines_to_text(json, mode) -> VARCHAR
	// Renders Pandoc inlines to text/markdown/html
	static void PandocInlinesToTextFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
