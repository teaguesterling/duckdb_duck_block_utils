#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class PandocBlockConvert {
public:
	// Register all Pandoc block conversion functions
	static void Register(ExtensionLoader &loader);

private:
	// pandoc_ast_to_blocks(json VARCHAR) -> LIST(duck_block)
	// Convert Pandoc JSON AST blocks to duck_block list
	static void PandocAstToBlocksFun(DataChunk &args, ExpressionState &state, Vector &result);

	// pandoc_blocks_to_ast(blocks LIST(duck_block)) -> VARCHAR
	// Convert duck_block list back to Pandoc JSON AST
	static void PandocBlocksToAstFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
