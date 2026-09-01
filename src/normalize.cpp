#include "normalize.hpp"
#include "duck_block_normalize.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"

namespace duckdb {

// THE TRANSFORM ITSELF LIVES IN vendor/duck_block_normalize.hpp, and this calls it.
//
// Not a copy kept in step -- the same function. Producers who cannot load this
// extension (an ABI-mismatched DuckDB submodule refuses it by every route) vendor that
// header, and if the implementation lived here and were mirrored there, the two would
// drift exactly as an image's alt text drifted from its content in this repo. The way
// to make a duplicate safe is not to maintain it carefully; it is not to have one.
void NormalizeFunctions::CollapseLonePlainIntoParent(vector<Value> &blocks) {
	duck_block::CollapseLonePlainIntoParent(blocks, BlockTypes::DuckBlockType());
}

// duck_blocks_normalize(blocks) -> blocks
//
// Idempotent by construction: after the pass every collapsible run has already been
// collapsed, so a second call finds nothing to do. That matters because a producer
// cannot always tell whether its input was normalised upstream, and a normaliser that
// is unsafe to run twice is one nobody can call defensively.
static void DbBlocksNormalizeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);
		if (blocks_val.IsNull()) {
			result.SetValue(i, blocks_val);
			continue;
		}
		auto blocks = ListValue::GetChildren(blocks_val);
		vector<Value> working(blocks.begin(), blocks.end());
		NormalizeFunctions::CollapseLonePlainIntoParent(working);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(working)));
	}
	result.SetVectorType(count == 1 ? VectorType::CONSTANT_VECTOR : VectorType::FLAT_VECTOR);
}

void NormalizeFunctions::Register(ExtensionLoader &loader) {
	auto duck_block_list_type = BlockTypes::DuckBlockListType();
	ScalarFunction fn("duck_blocks_normalize", {duck_block_list_type}, duck_block_list_type, DbBlocksNormalizeFun);
	loader.RegisterFunction(fn);
}

} // namespace duckdb
