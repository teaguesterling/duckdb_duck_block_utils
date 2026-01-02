#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class BuilderFunctions {
public:
	// Register all builder functions with the extension loader
	static void Register(ExtensionLoader &loader);

	// Helper to create a duck_block Value with the given fields (public for flattening functions)
	static Value CreateBlock(const string &block_type, const string &content,
	                         const Value &level, const string &encoding,
	                         const map<string, string> &attributes, int32_t block_order = 0);

private:
	// Atomic constructors - each returns a single duck_block

	// db_heading(content VARCHAR, level INTEGER) -> duck_block
	static void DbHeadingFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_paragraph(content VARCHAR) -> duck_block
	static void DbParagraphFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_code(content VARCHAR, language VARCHAR?) -> duck_block
	static void DbCodeFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_blockquote(content VARCHAR, level INTEGER?) -> duck_block
	static void DbBlockquoteFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_list_block(items VARCHAR[], ordered BOOLEAN?) -> duck_block
	static void DbListBlockFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_hr() -> duck_block
	static void DbHrFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_metadata(yaml_content VARCHAR) -> duck_block
	static void DbMetadataFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_image(src VARCHAR, alt VARCHAR?, title VARCHAR?) -> duck_block
	static void DbImageFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_raw(content VARCHAR, format VARCHAR?) -> duck_block
	static void DbRawFun(DataChunk &args, ExpressionState &state, Vector &result);

	// Flattening overloads - each returns LIST(duck_block) with parent + children

	// db_paragraph(children LIST(duck_block)) -> LIST(duck_block)
	static void DbParagraphFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_heading(level INTEGER, children LIST(duck_block)) -> LIST(duck_block)
	static void DbHeadingFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_blockquote(level INTEGER, children LIST(duck_block)) -> LIST(duck_block)
	static void DbBlockquoteFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_code(language VARCHAR, children LIST(duck_block)) -> LIST(duck_block)
	static void DbCodeFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
