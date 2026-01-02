#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class PandocInlineConvert {
public:
	// Register Pandoc inline conversion functions
	static void Register(ExtensionLoader &loader);

private:
	// pandoc_inlines_to_doc_inlines(json) -> LIST(doc_inline)
	// Converts nested Pandoc inline JSON to flat doc_inline rows
	static void PandocInlinesToDocInlinesFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_inlines_to_pandoc(LIST(doc_inline)) -> JSON
	// Converts flat doc_inline rows back to nested Pandoc inline JSON
	static void DocInlinesToPandocFun(DataChunk &args, ExpressionState &state, Vector &result);

	// pandoc_inlines_to_text(json, mode) -> VARCHAR
	// Renders Pandoc inlines to text/markdown/html
	static void PandocInlinesToTextFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
