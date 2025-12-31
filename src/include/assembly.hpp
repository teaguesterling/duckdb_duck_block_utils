#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

class AssemblyFunctions {
public:
	// Register all assembly functions with the extension loader
	static void Register(ExtensionLoader &loader);

private:
	// doc_assemble(blocks LIST(doc_block)) -> LIST(doc_block)
	// Flattens nested structures and assigns sequential block_order
	static void DocAssembleFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_section(title VARCHAR, level INTEGER, children LIST(doc_block)) -> LIST(doc_block)
	// Creates a heading followed by children blocks
	static void DocSectionFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_rebase_levels(blocks LIST(doc_block), offset INTEGER) -> LIST(doc_block)
	// Adjusts all heading levels by the given offset
	static void DocRebaseLevelsFun(DataChunk &args, ExpressionState &state, Vector &result);

	// doc_concat(blocks1 LIST(doc_block), blocks2 LIST(doc_block)) -> LIST(doc_block)
	// Simple concatenation without block_order adjustment (use merge for that)
	static void DocConcatFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
