#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class ExtractionFunctions {
public:
	// Register all extraction functions with the extension loader
	static void Register(ExtensionLoader &loader);

private:
	// db_blocks_to_text(blocks LIST(duck_block), separator VARCHAR?) -> VARCHAR
	// Extract plain text content from blocks
	static void DbBlocksToTextFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_blocks_headings(blocks LIST(duck_block)) -> LIST(STRUCT(level, title, id, element_order))
	// Extract heading information
	static void DbBlocksHeadingsFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_blocks_code_blocks(blocks LIST(duck_block)) -> LIST(STRUCT(language, content, element_order))
	// Extract code blocks with metadata
	static void DbBlocksCodeBlocksFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_blocks_stats(blocks LIST(duck_block)) -> LIST(STRUCT(element_type, count, avg_content_length))
	// Get block type statistics
	static void DbBlocksStatsFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
