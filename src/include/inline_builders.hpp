#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class InlineBuilderFunctions {
public:
	// Register all inline builder functions with the extension loader
	static void Register(ExtensionLoader &loader);

private:
	// Helper to create a doc_inline Value with the given fields
	static Value CreateInline(const string &inline_type, const string &content,
	                          const map<string, string> &attributes);

	// Inline element constructors - each returns a single doc_inline

	// doc_text(content VARCHAR) -> doc_inline
	static void DocTextFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_link(text VARCHAR, href VARCHAR, title VARCHAR?) -> doc_inline
	static void DocLinkFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_inline_image(src VARCHAR, alt VARCHAR?, title VARCHAR?) -> doc_inline
	static void DocInlineImageFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_bold(content VARCHAR) -> doc_inline
	static void DocBoldFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_italic(content VARCHAR) -> doc_inline
	static void DocItalicFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_inline_code(content VARCHAR) -> doc_inline
	static void DocInlineCodeFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_strikethrough(content VARCHAR) -> doc_inline
	static void DocStrikethroughFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
