#include "assembly.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"

namespace duckdb {

// Helper to create an attributes MAP from vectors
static Value CreateAttributesMap(const vector<Value> &keys, const vector<Value> &values) {
	return Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values);
}

// Helper to create an element Value with updated element_order
static Value CreateElementWithOrder(const Value &element, int32_t new_order) {
	auto children = StructValue::GetChildren(element);
	children[BlockTypes::ELEMENT_ORDER_IDX] = Value(new_order);
	return Value::STRUCT(BlockTypes::DocElementType(), std::move(children));
}

// Helper to create a heading block
static Value CreateHeadingBlock(const string &title, int32_t level, int32_t element_order) {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
	struct_values.push_back(make_pair("element_type", Value(BlockTypes::TYPE_HEADING)));
	struct_values.push_back(make_pair("content", Value(title)));
	struct_values.push_back(make_pair("level", Value(level)));
	struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_TEXT)));
	struct_values.push_back(make_pair("attributes", CreateAttributesMap({}, {})));
	struct_values.push_back(make_pair("element_order", Value(element_order)));

	return Value::STRUCT(std::move(struct_values));
}

// Helper to get the level from an element (returns -1 if NULL or not applicable)
static int32_t GetElementLevel(const Value &element) {
	auto &children = StructValue::GetChildren(element);
	if (children[BlockTypes::LEVEL_IDX].IsNull()) {
		return -1;
	}
	return children[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
}

// Helper to get element_type from an element
static string GetElementType(const Value &element) {
	auto &children = StructValue::GetChildren(element);
	if (children[BlockTypes::ELEMENT_TYPE_IDX].IsNull()) {
		return "";
	}
	return children[BlockTypes::ELEMENT_TYPE_IDX].GetValue<string>();
}

// Helper to create an element with adjusted level
static Value CreateElementWithLevel(const Value &element, int32_t new_level) {
	auto children = StructValue::GetChildren(element);
	children[BlockTypes::LEVEL_IDX] = Value(new_level);
	return Value::STRUCT(BlockTypes::DocElementType(), std::move(children));
}

void AssemblyFunctions::DocAssembleFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		// Handle NULL input
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value(LogicalType::LIST(BlockTypes::DocElementType())));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);

		// Assign sequential element_order values
		vector<Value> assembled;
		int32_t order = 0;
		for (auto &block : blocks_list) {
			if (!block.IsNull()) {
				assembled.push_back(CreateElementWithOrder(block, order++));
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DocElementType(), std::move(assembled)));
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
					section_blocks.push_back(CreateElementWithOrder(child, order++));
				}
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DocElementType(), std::move(section_blocks)));
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
			result.SetValue(i, Value(LogicalType::LIST(BlockTypes::DocElementType())));
			continue;
		}

		int32_t offset = offset_val.IsNull() ? 0 : offset_val.GetValue<int32_t>();
		auto &blocks_list = ListValue::GetChildren(blocks_val);

		vector<Value> rebased;
		for (auto &block : blocks_list) {
			if (block.IsNull()) {
				continue;
			}

			auto element_type = GetElementType(block);
			auto level = GetElementLevel(block);

			// Only adjust level for headings (and other blocks that have levels)
			if (element_type == BlockTypes::TYPE_HEADING && level > 0) {
				int32_t new_level = level + offset;
				// Clamp to valid heading levels (1-6)
				if (new_level < 1) new_level = 1;
				if (new_level > 6) new_level = 6;
				rebased.push_back(CreateElementWithLevel(block, new_level));
			} else {
				rebased.push_back(block);
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DocElementType(), std::move(rebased)));
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

		// Add second list (no element_order adjustment, unlike merge)
		if (!blocks2_val.IsNull()) {
			auto &blocks2_list = ListValue::GetChildren(blocks2_val);
			for (auto &block : blocks2_list) {
				if (!block.IsNull()) {
					concatenated.push_back(block);
				}
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DocElementType(), std::move(concatenated)));
	}
}

void AssemblyFunctions::Register(ExtensionLoader &loader) {
	auto doc_element_type = BlockTypes::DocElementType();
	auto doc_element_list_type = BlockTypes::DocElementListType();

	// doc_assemble(blocks LIST(doc_element)) -> LIST(doc_element)
	auto assemble_func = ScalarFunction(
	    "doc_assemble",
	    {doc_element_list_type},
	    doc_element_list_type,
	    DocAssembleFun
	);
	loader.RegisterFunction(assemble_func);

	// Also register as doc_document (alias for clarity in document construction)
	auto document_func = ScalarFunction(
	    "doc_document",
	    {doc_element_list_type},
	    doc_element_list_type,
	    DocAssembleFun
	);
	loader.RegisterFunction(document_func);

	// doc_section(title VARCHAR, level INTEGER, children LIST(doc_element)) -> LIST(doc_element)
	auto section_func = ScalarFunction(
	    "doc_section",
	    {LogicalType::VARCHAR, LogicalType::INTEGER, doc_element_list_type},
	    doc_element_list_type,
	    DocSectionFun
	);
	loader.RegisterFunction(section_func);

	// Two-arg version: doc_section(title VARCHAR, level INTEGER) -> LIST(doc_element)
	// Creates a section with just a heading (no children)
	auto section_func_2 = ScalarFunction(
	    "doc_section",
	    {LogicalType::VARCHAR, LogicalType::INTEGER},
	    doc_element_list_type,
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

			    result.SetValue(i, Value::LIST(BlockTypes::DocElementType(), std::move(section_blocks)));
		    }
	    }
	);
	loader.RegisterFunction(section_func_2);

	// doc_rebase_levels(blocks LIST(doc_element), offset INTEGER) -> LIST(doc_element)
	auto rebase_func = ScalarFunction(
	    "doc_rebase_levels",
	    {doc_element_list_type, LogicalType::INTEGER},
	    doc_element_list_type,
	    DocRebaseLevelsFun
	);
	loader.RegisterFunction(rebase_func);

	// doc_concat(blocks1 LIST(doc_element), blocks2 LIST(doc_element)) -> LIST(doc_element)
	auto concat_func = ScalarFunction(
	    "doc_concat",
	    {doc_element_list_type, doc_element_list_type},
	    doc_element_list_type,
	    DocConcatFun
	);
	loader.RegisterFunction(concat_func);
}

} // namespace duckdb
