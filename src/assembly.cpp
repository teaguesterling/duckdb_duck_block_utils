#include "assembly.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"

namespace duckdb {

// Helper to create an attributes MAP from vectors
static Value CreateAttributesMap(const vector<Value> &keys, const vector<Value> &values) {
	return Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values);
}

// Helper to create a block Value with updated block_order
static Value CreateBlockWithOrder(const Value &block, int32_t new_order) {
	auto children = StructValue::GetChildren(block);
	children[BlockTypes::BLOCK_ORDER_IDX] = Value(new_order);
	return Value::STRUCT(BlockTypes::DocBlockType(), std::move(children));
}

// Helper to create a heading block
static Value CreateHeadingBlock(const string &title, int32_t level, int32_t block_order) {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("block_type", Value(BlockTypes::TYPE_HEADING)));
	struct_values.push_back(make_pair("content", Value(title)));
	struct_values.push_back(make_pair("level", Value(level)));
	struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_TEXT)));
	struct_values.push_back(make_pair("attributes", CreateAttributesMap({}, {})));
	struct_values.push_back(make_pair("block_order", Value(block_order)));

	return Value::STRUCT(std::move(struct_values));
}

// Helper to get the level from a block (returns -1 if NULL or not applicable)
static int32_t GetBlockLevel(const Value &block) {
	auto &children = StructValue::GetChildren(block);
	if (children[BlockTypes::LEVEL_IDX].IsNull()) {
		return -1;
	}
	return children[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
}

// Helper to get block_type from a block
static string GetBlockType(const Value &block) {
	auto &children = StructValue::GetChildren(block);
	if (children[BlockTypes::BLOCK_TYPE_IDX].IsNull()) {
		return "";
	}
	return children[BlockTypes::BLOCK_TYPE_IDX].GetValue<string>();
}

// Helper to create a block with adjusted level
static Value CreateBlockWithLevel(const Value &block, int32_t new_level) {
	auto children = StructValue::GetChildren(block);
	children[BlockTypes::LEVEL_IDX] = Value(new_level);
	return Value::STRUCT(BlockTypes::DocBlockType(), std::move(children));
}

void AssemblyFunctions::DocAssembleFun(DataChunk &args, ExpressionState &state, Vector &result) {
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

		// Assign sequential block_order values
		vector<Value> assembled;
		int32_t order = 0;
		for (auto &block : blocks_list) {
			if (!block.IsNull()) {
				assembled.push_back(CreateBlockWithOrder(block, order++));
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DocBlockType(), std::move(assembled)));
	}
}

void AssemblyFunctions::DocSectionFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &title_vec = args.data[0];
	auto &level_vec = args.data[1];
	auto &children_vec = args.data[2];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto title = title_vec.GetValue(i);
		auto level = level_vec.GetValue(i);
		auto children = children_vec.GetValue(i);

		vector<Value> section_blocks;

		// Create heading as first block
		string title_str = title.IsNull() ? "" : title.GetValue<string>();
		int32_t level_val = level.IsNull() ? 2 : level.GetValue<int32_t>();

		section_blocks.push_back(CreateHeadingBlock(title_str, level_val, 0));

		// Add children
		if (!children.IsNull()) {
			auto &children_list = ListValue::GetChildren(children);
			int32_t order = 1;
			for (auto &child : children_list) {
				if (!child.IsNull()) {
					section_blocks.push_back(CreateBlockWithOrder(child, order++));
				}
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DocBlockType(), std::move(section_blocks)));
	}
}

void AssemblyFunctions::DocRebaseLevelsFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto &offset_vec = args.data[1];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);
		auto offset_val = offset_vec.GetValue(i);

		// Handle NULL input
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value(LogicalType::LIST(BlockTypes::DocBlockType())));
			continue;
		}

		int32_t offset = offset_val.IsNull() ? 0 : offset_val.GetValue<int32_t>();
		auto &blocks_list = ListValue::GetChildren(blocks_val);

		vector<Value> rebased;
		for (auto &block : blocks_list) {
			if (block.IsNull()) {
				continue;
			}

			auto block_type = GetBlockType(block);
			auto level = GetBlockLevel(block);

			// Only adjust level for headings (and other blocks that have levels)
			if (block_type == BlockTypes::TYPE_HEADING && level > 0) {
				int32_t new_level = level + offset;
				// Clamp to valid heading levels (1-6)
				if (new_level < 1) new_level = 1;
				if (new_level > 6) new_level = 6;
				rebased.push_back(CreateBlockWithLevel(block, new_level));
			} else {
				rebased.push_back(block);
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DocBlockType(), std::move(rebased)));
	}
}

void AssemblyFunctions::DocConcatFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks1_vec = args.data[0];
	auto &blocks2_vec = args.data[1];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks1_val = blocks1_vec.GetValue(i);
		auto blocks2_val = blocks2_vec.GetValue(i);

		vector<Value> concatenated;

		// Add first list
		if (!blocks1_val.IsNull()) {
			auto &blocks1_list = ListValue::GetChildren(blocks1_val);
			for (auto &block : blocks1_list) {
				if (!block.IsNull()) {
					concatenated.push_back(block);
				}
			}
		}

		// Add second list (no block_order adjustment, unlike merge)
		if (!blocks2_val.IsNull()) {
			auto &blocks2_list = ListValue::GetChildren(blocks2_val);
			for (auto &block : blocks2_list) {
				if (!block.IsNull()) {
					concatenated.push_back(block);
				}
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DocBlockType(), std::move(concatenated)));
	}
}

void AssemblyFunctions::Register(ExtensionLoader &loader) {
	auto doc_block_type = BlockTypes::DocBlockType();
	auto doc_block_list_type = BlockTypes::DocBlockListType();

	// doc_assemble(blocks LIST(doc_block)) -> LIST(doc_block)
	auto assemble_func = ScalarFunction(
	    "doc_assemble",
	    {doc_block_list_type},
	    doc_block_list_type,
	    DocAssembleFun
	);
	loader.RegisterFunction(assemble_func);

	// Also register as doc_document (alias for clarity in document construction)
	auto document_func = ScalarFunction(
	    "doc_document",
	    {doc_block_list_type},
	    doc_block_list_type,
	    DocAssembleFun
	);
	loader.RegisterFunction(document_func);

	// doc_section(title VARCHAR, level INTEGER, children LIST(doc_block)) -> LIST(doc_block)
	auto section_func = ScalarFunction(
	    "doc_section",
	    {LogicalType::VARCHAR, LogicalType::INTEGER, doc_block_list_type},
	    doc_block_list_type,
	    DocSectionFun
	);
	loader.RegisterFunction(section_func);

	// Two-arg version: doc_section(title VARCHAR, level INTEGER) -> LIST(doc_block)
	// Creates a section with just a heading (no children)
	auto section_func_2 = ScalarFunction(
	    "doc_section",
	    {LogicalType::VARCHAR, LogicalType::INTEGER},
	    doc_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &title_vec = args.data[0];
		    auto &level_vec = args.data[1];
		    auto count = args.size();

		    for (idx_t i = 0; i < count; i++) {
			    auto title = title_vec.GetValue(i);
			    auto level = level_vec.GetValue(i);

			    string title_str = title.IsNull() ? "" : title.GetValue<string>();
			    int32_t level_val = level.IsNull() ? 2 : level.GetValue<int32_t>();

			    vector<Value> section_blocks;
			    section_blocks.push_back(CreateHeadingBlock(title_str, level_val, 0));

			    result.SetValue(i, Value::LIST(BlockTypes::DocBlockType(), std::move(section_blocks)));
		    }
	    }
	);
	loader.RegisterFunction(section_func_2);

	// doc_rebase_levels(blocks LIST(doc_block), offset INTEGER) -> LIST(doc_block)
	auto rebase_func = ScalarFunction(
	    "doc_rebase_levels",
	    {doc_block_list_type, LogicalType::INTEGER},
	    doc_block_list_type,
	    DocRebaseLevelsFun
	);
	loader.RegisterFunction(rebase_func);

	// doc_concat(blocks1 LIST(doc_block), blocks2 LIST(doc_block)) -> LIST(doc_block)
	auto concat_func = ScalarFunction(
	    "doc_concat",
	    {doc_block_list_type, doc_block_list_type},
	    doc_block_list_type,
	    DocConcatFun
	);
	loader.RegisterFunction(concat_func);
}

} // namespace duckdb
