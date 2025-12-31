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
	// doc_blocks_filter(blocks LIST(doc_block), types VARCHAR[]) -> LIST(doc_block)
	static void DocBlocksFilterFun(DataChunk &args, ExpressionState &state, Vector &result);

	// Filter blocks to exclude specified types
	// doc_blocks_exclude(blocks LIST(doc_block), types VARCHAR[]) -> LIST(doc_block)
	static void DocBlocksExcludeFun(DataChunk &args, ExpressionState &state, Vector &result);

	// Merge two block lists, adjusting block_order for continuity
	// doc_blocks_merge(blocks1 LIST(doc_block), blocks2 LIST(doc_block)) -> LIST(doc_block)
	static void DocBlocksMergeFun(DataChunk &args, ExpressionState &state, Vector &result);

	// Renumber block_order values sequentially from 0
	// doc_blocks_reorder(blocks LIST(doc_block)) -> LIST(doc_block)
	static void DocBlocksReorderFun(DataChunk &args, ExpressionState &state, Vector &result);

	// Extract a contiguous range of blocks by block_order
	// doc_blocks_slice(blocks LIST(doc_block), start INTEGER, end INTEGER) -> LIST(doc_block)
	static void DocBlocksSliceFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
