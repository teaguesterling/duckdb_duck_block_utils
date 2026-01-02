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

	// ========================================================================
	// V2 API: Core utility for handling duck_block_content
	// ========================================================================

	// Build a result list from a parent block and content input
	// Handles: VARCHAR -> set content field
	//          duck_block -> content=NULL, add child at level+1
	//          LIST(duck_block) -> content=NULL, add children at level+1
	static vector<Value> BuildWithContent(const Value &parent_block, const Value &content_input,
	                                      int32_t base_level = 1);

	// Create a parent block with NULL content (for use with children)
	static Value CreateBlockWithNullContent(const string &block_type, const string &kind,
	                                        const Value &level, const string &encoding,
	                                        const map<string, string> &attributes);

private:
	// ========================================================================
	// V2 API: Block builders - all return LIST(duck_block)
	// ========================================================================

	// db_heading(level INTEGER, content) -> LIST(duck_block)
	static void DbHeadingV2Fun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_paragraph(content) -> LIST(duck_block)
	static void DbParagraphV2Fun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_code(content) -> LIST(duck_block) (no language)
	// db_code(language VARCHAR, content) -> LIST(duck_block)
	static void DbCodeV2Fun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbCodeV2NoLangFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_blockquote(content) -> LIST(duck_block) (level defaults to 1)
	// db_blockquote(level INTEGER, content) -> LIST(duck_block)
	static void DbBlockquoteV2Fun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbBlockquoteV2NoLevelFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_list_block(items VARCHAR[]) -> LIST(duck_block)
	// db_list_block(ordered BOOLEAN, items VARCHAR[]) -> LIST(duck_block)
	static void DbListBlockV2Fun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbListBlockV2NoOrderFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_list_item(content) -> LIST(duck_block)
	// db_list_item(ordered BOOLEAN, content) -> LIST(duck_block)
	static void DbListItemV2Fun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbListItemV2NoOrderFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_hr() -> LIST(duck_block)
	static void DbHrV2Fun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_metadata(yaml_content VARCHAR) -> LIST(duck_block)
	static void DbMetadataV2Fun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_image(src VARCHAR, alt? VARCHAR, title? VARCHAR) -> LIST(duck_block)
	static void DbImageV2Fun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_raw(content VARCHAR) -> LIST(duck_block)
	// db_raw(format VARCHAR, content VARCHAR) -> LIST(duck_block)
	static void DbRawV2Fun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbRawV2NoFormatFun(DataChunk &args, ExpressionState &state, Vector &result);

	// ========================================================================
	// Legacy V1 API (kept for backwards compatibility during transition)
	// ========================================================================

	static void DbHeadingFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbParagraphFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbCodeFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbBlockquoteFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbListBlockFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbHrFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbMetadataFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbImageFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbRawFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbParagraphFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbHeadingFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbBlockquoteFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
	static void DbCodeFlattenFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
