#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class TypeFunctions {
public:
	// Register all type-related functions with the extension loader
	static void Register(ExtensionLoader &loader);

private:
	// duck_block(block_type, content, level, encoding, attributes, block_order) -> duck_block
	// Full constructor with all fields
	static void DuckBlockFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_block(block_type, content) -> duck_block
	// Minimal constructor with defaults
	static void DuckBlockSimpleFun(DataChunk &args, ExpressionState &state, Vector &result);

	// to_duck_block(struct) -> duck_block
	// Convert a compatible struct to duck_block
	static void ToDuckBlockFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_block_valid(block) -> BOOLEAN
	// Check if a block has valid structure and values
	static void DuckBlockValidFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_block_type(block) -> VARCHAR
	// Extract the block_type field
	static void DuckBlockTypeFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_block_content(block) -> VARCHAR
	// Extract the content field
	static void DuckBlockContentFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_block_level(block) -> INTEGER
	// Extract the level field
	static void DuckBlockLevelFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_block_encoding(block) -> VARCHAR
	// Extract the encoding field
	static void DuckBlockEncodingFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_block_order(block) -> INTEGER
	// Extract the block_order field
	static void DuckBlockOrderFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_block_attr(block, key) -> VARCHAR
	// Extract a specific attribute value
	static void DuckBlockAttrFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_block_set_order(block, new_order) -> duck_block
	// Return a new block with updated block_order
	static void DuckBlockSetOrderFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_block_set_content(block, new_content) -> duck_block
	// Return a new block with updated content
	static void DuckBlockSetContentFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_block_set_level(block, new_level) -> duck_block
	// Return a new block with updated level
	static void DuckBlockSetLevelFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
