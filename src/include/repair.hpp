#pragma once

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

// duck_blocks_repair(blocks): the deterministic fixes for the list-level rules.
//
// Passes, in order: wrap orphans in their implicit parent (L4, L5); rebase levels so
// the shallowest is 1 (L2); collapse level jumps (L3); renumber element_order from 0
// (L1). Structure first, numbering last, because wrapping inserts elements that need
// numbers. Idempotent. Never changes an existing element's content, attributes or
// element_type, never removes one -- that is what makes every fix deterministic. What
// it cannot fix (a per-element error) it leaves for duck_blocks_validate to report.
class RepairFunctions {
public:
	static void Register(ExtensionLoader &loader);
	// In-place on a list of duck_block Values. The exporter calls this too.
	static void RepairBlocks(vector<Value> &blocks);
	static void DbBlocksRepairFun(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
