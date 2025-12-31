#include "manipulation.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"

#include <unordered_set>
#include <algorithm>

namespace duckdb {

// Helper to create an empty MAP(VARCHAR, VARCHAR)
static Value CreateEmptyAttributesMap() {
	return Value::MAP(LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR), vector<Value>());
}

// Helper to create a block Value with updated block_order
static Value CreateBlockWithOrder(const Value &block, int32_t new_order) {
	auto children = StructValue::GetChildren(block);
	children[BlockTypes::BLOCK_ORDER_IDX] = Value(new_order);
	return Value::STRUCT(BlockTypes::DocBlockType(), std::move(children));
}

// Helper to get block_type from a block struct
static string GetBlockType(const Value &block) {
	auto &children = StructValue::GetChildren(block);
	if (children[BlockTypes::BLOCK_TYPE_IDX].IsNull()) {
		return "";
	}
	return children[BlockTypes::BLOCK_TYPE_IDX].GetValue<string>();
}

// Helper to get block_order from a block struct
static int32_t GetBlockOrder(const Value &block) {
	auto &children = StructValue::GetChildren(block);
	if (children[BlockTypes::BLOCK_ORDER_IDX].IsNull()) {
		return 0;
	}
	return children[BlockTypes::BLOCK_ORDER_IDX].GetValue<int32_t>();
}

void ManipulationFunctions::DocBlocksFilterFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto &types_vec = args.data[1];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);
		auto types_val = types_vec.GetValue(i);

		// Handle NULL inputs
		if (blocks_val.IsNull() || types_val.IsNull()) {
			result.SetValue(i, Value(LogicalType::LIST(BlockTypes::DocBlockType())));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);
		auto &types_list = ListValue::GetChildren(types_val);

		// Build set of types to include
		std::unordered_set<string> type_set;
		for (auto &t : types_list) {
			if (!t.IsNull()) {
				type_set.insert(t.GetValue<string>());
			}
		}

		// Filter blocks
		vector<Value> filtered;
		for (auto &block : blocks_list) {
			if (block.IsNull()) {
				continue;
			}
			auto block_type = GetBlockType(block);
			if (type_set.count(block_type)) {
				filtered.push_back(block);
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DocBlockType(), std::move(filtered)));
	}
}

void ManipulationFunctions::DocBlocksExcludeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto &types_vec = args.data[1];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);
		auto types_val = types_vec.GetValue(i);

		// Handle NULL inputs
		if (blocks_val.IsNull() || types_val.IsNull()) {
			result.SetValue(i, Value(LogicalType::LIST(BlockTypes::DocBlockType())));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);
		auto &types_list = ListValue::GetChildren(types_val);

		// Build set of types to exclude
		std::unordered_set<string> type_set;
		for (auto &t : types_list) {
			if (!t.IsNull()) {
				type_set.insert(t.GetValue<string>());
			}
		}

		// Filter blocks (exclude matching types)
		vector<Value> filtered;
		for (auto &block : blocks_list) {
			if (block.IsNull()) {
				continue;
			}
			auto block_type = GetBlockType(block);
			if (!type_set.count(block_type)) {
				filtered.push_back(block);
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DocBlockType(), std::move(filtered)));
	}
}

void ManipulationFunctions::DocBlocksMergeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks1_vec = args.data[0];
	auto &blocks2_vec = args.data[1];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks1_val = blocks1_vec.GetValue(i);
		auto blocks2_val = blocks2_vec.GetValue(i);

		// Handle NULL inputs
		if (blocks1_val.IsNull() && blocks2_val.IsNull()) {
			result.SetValue(i, Value(LogicalType::LIST(BlockTypes::DocBlockType())));
			continue;
		}

		vector<Value> merged;

		// Process first list
		int32_t max_order = -1;
		if (!blocks1_val.IsNull()) {
			auto &blocks1_list = ListValue::GetChildren(blocks1_val);
			for (auto &block : blocks1_list) {
				if (!block.IsNull()) {
					merged.push_back(block);
					auto order = GetBlockOrder(block);
					if (order > max_order) {
						max_order = order;
					}
				}
			}
		}

		// Process second list with adjusted block_order
		if (!blocks2_val.IsNull()) {
			auto &blocks2_list = ListValue::GetChildren(blocks2_val);
			int32_t offset = max_order + 1;
			for (auto &block : blocks2_list) {
				if (!block.IsNull()) {
					auto old_order = GetBlockOrder(block);
					merged.push_back(CreateBlockWithOrder(block, old_order + offset));
				}
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DocBlockType(), std::move(merged)));
	}
}

void ManipulationFunctions::DocBlocksReorderFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		// Handle NULL input
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value(LogicalType::LIST(BlockTypes::DocBlockType())));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);

		// Sort blocks by current block_order, then reassign sequentially
		vector<std::pair<int32_t, Value>> ordered_blocks;
		for (auto &block : blocks_list) {
			if (!block.IsNull()) {
				ordered_blocks.push_back({GetBlockOrder(block), block});
			}
		}

		std::sort(ordered_blocks.begin(), ordered_blocks.end(),
		          [](const std::pair<int32_t, Value> &a, const std::pair<int32_t, Value> &b) {
			          return a.first < b.first;
		          });

		// Reassign block_order as 0, 1, 2, ...
		vector<Value> reordered;
		int32_t new_order = 0;
		for (idx_t j = 0; j < ordered_blocks.size(); j++) {
			reordered.push_back(CreateBlockWithOrder(ordered_blocks[j].second, new_order++));
		}

		result.SetValue(i, Value::LIST(BlockTypes::DocBlockType(), std::move(reordered)));
	}
}

void ManipulationFunctions::DocBlocksSliceFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto &start_vec = args.data[1];
	auto &end_vec = args.data[2];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);
		auto start_val = start_vec.GetValue(i);
		auto end_val = end_vec.GetValue(i);

		// Handle NULL inputs
		if (blocks_val.IsNull() || start_val.IsNull() || end_val.IsNull()) {
			result.SetValue(i, Value(LogicalType::LIST(BlockTypes::DocBlockType())));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);
		auto start_order = start_val.GetValue<int32_t>();
		auto end_order = end_val.GetValue<int32_t>();

		// Filter blocks within the range (inclusive)
		vector<Value> sliced;
		for (auto &block : blocks_list) {
			if (block.IsNull()) {
				continue;
			}
			auto order = GetBlockOrder(block);
			if (order >= start_order && order <= end_order) {
				sliced.push_back(block);
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DocBlockType(), std::move(sliced)));
	}
}

void ManipulationFunctions::Register(ExtensionLoader &loader) {
	auto doc_block_type = BlockTypes::DocBlockType();
	auto doc_block_list_type = BlockTypes::DocBlockListType();

	// doc_blocks_filter(blocks LIST(doc_block), types VARCHAR[]) -> LIST(doc_block)
	auto filter_func = ScalarFunction(
	    "doc_blocks_filter",
	    {doc_block_list_type, LogicalType::LIST(LogicalType::VARCHAR)},
	    doc_block_list_type,
	    DocBlocksFilterFun
	);
	loader.RegisterFunction(filter_func);

	// doc_blocks_exclude(blocks LIST(doc_block), types VARCHAR[]) -> LIST(doc_block)
	auto exclude_func = ScalarFunction(
	    "doc_blocks_exclude",
	    {doc_block_list_type, LogicalType::LIST(LogicalType::VARCHAR)},
	    doc_block_list_type,
	    DocBlocksExcludeFun
	);
	loader.RegisterFunction(exclude_func);

	// doc_blocks_merge(blocks1 LIST(doc_block), blocks2 LIST(doc_block)) -> LIST(doc_block)
	auto merge_func = ScalarFunction(
	    "doc_blocks_merge",
	    {doc_block_list_type, doc_block_list_type},
	    doc_block_list_type,
	    DocBlocksMergeFun
	);
	loader.RegisterFunction(merge_func);

	// doc_blocks_reorder(blocks LIST(doc_block)) -> LIST(doc_block)
	auto reorder_func = ScalarFunction(
	    "doc_blocks_reorder",
	    {doc_block_list_type},
	    doc_block_list_type,
	    DocBlocksReorderFun
	);
	loader.RegisterFunction(reorder_func);

	// doc_blocks_slice(blocks LIST(doc_block), start INTEGER, end INTEGER) -> LIST(doc_block)
	auto slice_func = ScalarFunction(
	    "doc_blocks_slice",
	    {doc_block_list_type, LogicalType::INTEGER, LogicalType::INTEGER},
	    doc_block_list_type,
	    DocBlocksSliceFun
	);
	loader.RegisterFunction(slice_func);
}

} // namespace duckdb
