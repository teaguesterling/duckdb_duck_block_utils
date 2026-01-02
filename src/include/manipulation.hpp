#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class ManipulationFunctions {
public:
	// Register all manipulation functions with the extension loader
	static void Register(ExtensionLoader &loader);

private:
	// Filter blocks to include only specified types
	// db_blocks_filter(blocks LIST(duck_block), types VARCHAR[]) -> LIST(duck_block)
	static void DbBlocksFilterFun(DataChunk &args, ExpressionState &state, Vector &result);

	// Filter blocks to exclude specified types
	// db_blocks_exclude(blocks LIST(duck_block), types VARCHAR[]) -> LIST(duck_block)
	static void DbBlocksExcludeFun(DataChunk &args, ExpressionState &state, Vector &result);

	// Merge two block lists, adjusting element_order for continuity
	// db_blocks_merge(blocks1 LIST(duck_block), blocks2 LIST(duck_block)) -> LIST(duck_block)
	static void DbBlocksMergeFun(DataChunk &args, ExpressionState &state, Vector &result);

	// Renumber element_order values sequentially from 0
	// db_blocks_reorder(blocks LIST(duck_block)) -> LIST(duck_block)
	static void DbBlocksReorderFun(DataChunk &args, ExpressionState &state, Vector &result);

	// Extract a contiguous range of blocks by element_order
	// db_blocks_slice(blocks LIST(duck_block), start INTEGER, end INTEGER) -> LIST(duck_block)
	static void DbBlocksSliceFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
