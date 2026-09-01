#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class ExtractionFunctions {
public:
	// Register all extraction functions with the extension loader
	static void Register(ExtensionLoader &loader);

private:
	// duck_blocks_to_text(blocks LIST(duck_block), separator VARCHAR?) -> VARCHAR
	// Extract plain text content from blocks
	static void DbBlocksToTextFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_blocks_headings(blocks LIST(duck_block)) -> LIST(STRUCT(level, title, id, element_order))
	// Extract heading information
	static void DbBlocksHeadingsFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_blocks_code_blocks(blocks LIST(duck_block)) -> LIST(STRUCT(language, content, element_order))
	// Extract code blocks with metadata
	static void DbBlocksCodeBlocksFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_blocks_stats(blocks LIST(duck_block)) -> LIST(STRUCT(element_type, count, avg_content_length))
	// Get block type statistics
	static void DbBlocksStatsFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_blocks_toc(blocks LIST(duck_block)) -> LIST(STRUCT(level, title, id, indent, element_order))
	// Generate table of contents from headings
	static void DbBlocksTocFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_blocks_links(blocks LIST(duck_block)) -> LIST(STRUCT(href, text, title, element_order))
	// Extract links from blocks
	static void DbBlocksLinksFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
