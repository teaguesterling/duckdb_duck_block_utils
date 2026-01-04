#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class PandocBlockConvert {
public:
	// Register all Pandoc block conversion functions
	static void Register(ExtensionLoader &loader);

	// Core conversion function (exposed for reuse)
	static void ConvertPandocAstToBlocks(const string &json, vector<Value> &result);

private:
	// pandoc_ast_to_blocks(json VARCHAR) -> LIST(duck_block)
	// Convert Pandoc JSON AST blocks to duck_block list
	static void PandocAstToBlocksFun(DataChunk &args, ExpressionState &state, Vector &result);

	// pandoc_blocks_to_ast(blocks LIST(duck_block)) -> VARCHAR
	// Convert duck_block list back to Pandoc JSON AST
	static void PandocBlocksToAstFun(DataChunk &args, ExpressionState &state, Vector &result);

	// read_pandoc_ast(file_path VARCHAR) -> LIST(duck_block)
	// Read a Pandoc JSON file and convert to duck_blocks
	static void ReadPandocAstFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
