#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class BuilderFunctions {
public:
	// Register all builder functions with the extension loader
	static void Register(ExtensionLoader &loader);

private:
	// Atomic constructors - each returns a single doc_block

	// doc_heading(content VARCHAR, level INTEGER, id VARCHAR?) -> doc_block
	static void DocHeadingFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_paragraph(content VARCHAR) -> doc_block
	static void DocParagraphFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_code(content VARCHAR, language VARCHAR?) -> doc_block
	static void DocCodeFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_blockquote(content VARCHAR, level INTEGER?) -> doc_block
	static void DocBlockquoteFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_list_block(items VARCHAR[], ordered BOOLEAN?) -> doc_block
	static void DocListBlockFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_hr() -> doc_block
	static void DocHrFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_metadata(yaml_content VARCHAR) -> doc_block
	static void DocMetadataFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_image(src VARCHAR, alt VARCHAR?, title VARCHAR?) -> doc_block
	static void DocImageFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_raw(content VARCHAR, format VARCHAR?) -> doc_block
	static void DocRawFun(DataChunk &args, ExpressionState &state, Vector &result);

	// Helper to create a doc_block Value with the given fields
	static Value CreateBlock(const string &block_type, const string &content,
	                         const Value &level, const string &encoding,
	                         const map<string, string> &attributes, int32_t block_order = 0);
};

} // namespace duckdb
