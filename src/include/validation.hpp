#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class ValidationFunctions {
public:
	// Register all validation functions with the extension loader
	static void Register(ExtensionLoader &loader);

private:
	// duck_blocks_validate(blocks LIST(duck_block)) -> STRUCT(valid BOOLEAN, errors LIST(STRUCT))
	// Validate blocks against schema requirements
	static void DbBlocksValidateFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_blocks_lint(blocks LIST(duck_block)) -> LIST(STRUCT(severity, message, element_order))
	// Check for best practices and common issues
	static void DbBlocksLintFun(DataChunk &args, ExpressionState &state, Vector &result);

	// duck_blocks_structure(blocks LIST(duck_block)) -> STRUCT(block_count, inline_count, heading_count, ...)
	// Get document structure summary
	static void DbBlocksStructureFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
