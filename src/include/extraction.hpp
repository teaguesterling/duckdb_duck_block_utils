#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class ExtractionFunctions {
public:
	// Register all extraction functions with the extension loader
	static void Register(ExtensionLoader &loader);

private:
	// doc_blocks_to_text(blocks LIST(doc_block), separator VARCHAR?) -> VARCHAR
	// Extract plain text content from blocks
	static void DocBlocksToTextFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_blocks_headings(blocks LIST(doc_block)) -> LIST(STRUCT(level, title, id, block_order))
	// Extract heading information
	static void DocBlocksHeadingsFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_blocks_code_blocks(blocks LIST(doc_block)) -> LIST(STRUCT(language, content, block_order))
	// Extract code blocks with metadata
	static void DocBlocksCodeBlocksFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_blocks_stats(blocks LIST(doc_block)) -> LIST(STRUCT(block_type, count, avg_content_length))
	// Get block type statistics
	static void DocBlocksStatsFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
