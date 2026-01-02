#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class AssemblyFunctions {
public:
	// Register all assembly functions with the extension loader
	static void Register(ExtensionLoader &loader);

private:
	// db_assemble(blocks LIST(duck_block)) -> LIST(duck_block)
	// Flattens nested structures and assigns sequential element_order
	static void DbAssembleFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_section(title VARCHAR, level INTEGER, children LIST(duck_block)) -> LIST(duck_block)
	// Creates a heading followed by children blocks
	static void DbSectionFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_rebase_levels(blocks LIST(duck_block), offset INTEGER) -> LIST(duck_block)
	// Adjusts all heading levels by the given offset
	static void DbRebaseLevelsFun(DataChunk &args, ExpressionState &state, Vector &result);

	// db_concat(blocks1 LIST(duck_block), blocks2 LIST(duck_block)) -> LIST(duck_block)
	// Simple concatenation without element_order adjustment (use merge for that)
	static void DbConcatFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
