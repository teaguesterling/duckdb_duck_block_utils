#include "manipulation.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"

#include <unordered_set>
#include <algorithm>

namespace duckdb {

// Helper to create an element Value with updated element_order
static Value CreateElementWithOrder(const Value &element, int32_t new_order) {
	auto children = StructValue::GetChildren(element);
	children[BlockTypes::ELEMENT_ORDER_IDX] = Value(new_order);
	return Value::STRUCT(BlockTypes::DuckBlockType(), std::move(children));
}

// Helper to get element_type from an element struct
static string GetElementType(const Value &element) {
	auto &children = StructValue::GetChildren(element);
	if (children[BlockTypes::ELEMENT_TYPE_IDX].IsNull()) {
		return "";
	}
	return children[BlockTypes::ELEMENT_TYPE_IDX].GetValue<string>();
}

// Helper to get element_order from an element struct
static int32_t GetElementOrder(const Value &element) {
	auto &children = StructValue::GetChildren(element);
	if (children[BlockTypes::ELEMENT_ORDER_IDX].IsNull()) {
		return 0;
	}
	return children[BlockTypes::ELEMENT_ORDER_IDX].GetValue<int32_t>();
}

void ManipulationFunctions::DbBlocksFilterFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto &types_vec = args.data[1];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);
		auto types_val = types_vec.GetValue(i);

		// Handle NULL inputs
		if (blocks_val.IsNull() || types_val.IsNull()) {
			result.SetValue(i, Value(LogicalType::LIST(BlockTypes::DuckBlockType())));
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
			auto block_type = GetElementType(block);
			if (type_set.count(block_type)) {
				filtered.push_back(block);
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(filtered)));
	}
}

void ManipulationFunctions::DbBlocksExcludeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto &types_vec = args.data[1];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);
		auto types_val = types_vec.GetValue(i);

		// Handle NULL inputs
		if (blocks_val.IsNull() || types_val.IsNull()) {
			result.SetValue(i, Value(LogicalType::LIST(BlockTypes::DuckBlockType())));
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
			auto block_type = GetElementType(block);
			if (!type_set.count(block_type)) {
				filtered.push_back(block);
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(filtered)));
	}
}

void ManipulationFunctions::DbBlocksMergeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks1_vec = args.data[0];
	auto &blocks2_vec = args.data[1];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks1_val = blocks1_vec.GetValue(i);
		auto blocks2_val = blocks2_vec.GetValue(i);

		// Handle NULL inputs
		if (blocks1_val.IsNull() && blocks2_val.IsNull()) {
			result.SetValue(i, Value(LogicalType::LIST(BlockTypes::DuckBlockType())));
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
					auto order = GetElementOrder(block);
					if (order > max_order) {
						max_order = order;
					}
				}
			}
		}

		// Process second list with adjusted element_order
		if (!blocks2_val.IsNull()) {
			auto &blocks2_list = ListValue::GetChildren(blocks2_val);
			int32_t offset = max_order + 1;
			for (auto &block : blocks2_list) {
				if (!block.IsNull()) {
					auto old_order = GetElementOrder(block);
					merged.push_back(CreateElementWithOrder(block, old_order + offset));
				}
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(merged)));
	}
}

void ManipulationFunctions::DbBlocksReorderFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		// Handle NULL input
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value(LogicalType::LIST(BlockTypes::DuckBlockType())));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);

		// Sort blocks by current element_order, then reassign sequentially
		vector<std::pair<int32_t, Value>> ordered_blocks;
		for (auto &block : blocks_list) {
			if (!block.IsNull()) {
				ordered_blocks.push_back({GetElementOrder(block), block});
			}
		}

		std::sort(ordered_blocks.begin(), ordered_blocks.end(),
		          [](const std::pair<int32_t, Value> &a, const std::pair<int32_t, Value> &b) {
			          return a.first < b.first;
		          });

		// Reassign element_order as 0, 1, 2, ...
		vector<Value> reordered;
		int32_t new_order = 0;
		for (idx_t j = 0; j < ordered_blocks.size(); j++) {
			reordered.push_back(CreateElementWithOrder(ordered_blocks[j].second, new_order++));
		}

		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(reordered)));
	}
}

void ManipulationFunctions::DbBlocksSliceFun(DataChunk &args, ExpressionState &state, Vector &result) {
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
			result.SetValue(i, Value(LogicalType::LIST(BlockTypes::DuckBlockType())));
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
			auto order = GetElementOrder(block);
			if (order >= start_order && order <= end_order) {
				sliced.push_back(block);
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(sliced)));
	}
}

void ManipulationFunctions::Register(ExtensionLoader &loader) {
	auto duck_block_type = BlockTypes::DuckBlockType();
	auto duck_block_list_type = BlockTypes::DuckBlockListType();

	// db_blocks_filter(blocks LIST(doc_element), types VARCHAR[]) -> LIST(doc_element)
	auto filter_func = ScalarFunction(
	    "db_blocks_filter",
	    {duck_block_list_type, LogicalType::LIST(LogicalType::VARCHAR)},
	    duck_block_list_type,
	    DbBlocksFilterFun
	);
	loader.RegisterFunction(filter_func);

	// db_blocks_exclude(blocks LIST(doc_element), types VARCHAR[]) -> LIST(doc_element)
	auto exclude_func = ScalarFunction(
	    "db_blocks_exclude",
	    {duck_block_list_type, LogicalType::LIST(LogicalType::VARCHAR)},
	    duck_block_list_type,
	    DbBlocksExcludeFun
	);
	loader.RegisterFunction(exclude_func);

	// db_blocks_merge(blocks1 LIST(doc_element), blocks2 LIST(doc_element)) -> LIST(doc_element)
	auto merge_func = ScalarFunction(
	    "db_blocks_merge",
	    {duck_block_list_type, duck_block_list_type},
	    duck_block_list_type,
	    DbBlocksMergeFun
	);
	loader.RegisterFunction(merge_func);

	// db_blocks_reorder(blocks LIST(doc_element)) -> LIST(doc_element)
	auto reorder_func = ScalarFunction(
	    "db_blocks_reorder",
	    {duck_block_list_type},
	    duck_block_list_type,
	    DbBlocksReorderFun
	);
	loader.RegisterFunction(reorder_func);

	// db_blocks_slice(blocks LIST(doc_element), start INTEGER, end INTEGER) -> LIST(doc_element)
	auto slice_func = ScalarFunction(
	    "db_blocks_slice",
	    {duck_block_list_type, LogicalType::INTEGER, LogicalType::INTEGER},
	    duck_block_list_type,
	    DbBlocksSliceFun
	);
	loader.RegisterFunction(slice_func);
}

} // namespace duckdb
