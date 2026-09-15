#pragma once

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

// duck_blocks_body(blocks): the document's BODY, spec 1.4.
//
// Body is a SUBTREE property. A row is body iff IsBody(kind, element_type) holds AND no
// ancestor by level is a kind='value' or element_type='metadata' row. The walk: a value
// or metadata row is a subtree root; it and every following row at a greater level are
// dropped; the subtree ends at the first row whose level is at or above the root's. A
// childless value row (webbed's title) and a lone frontmatter blob (markdown) are
// dropped whole. A projection: element_order is NOT renumbered (recovery contract).
// A scalar LIST -> LIST, not a table macro: panduck measured that re-scanning a table
// function through several CTE references returned zero rows on DuckDB 2.0; a value
// cannot be re-scanned inconsistently.
class BodyFunctions {
public:
	static void Register(ExtensionLoader &loader);
	static void DbBlocksBodyFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
