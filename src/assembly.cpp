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
	return Value::STRUCT(BlockTypes::DuckBlockType(), std::move(children));
}

// Helper to create a heading block with heading_level in attributes
static Value CreateHeadingBlock(const string &title, int32_t heading_level, int32_t element_order) {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
	struct_values.push_back(make_pair("element_type", Value(BlockTypes::TYPE_HEADING)));
	struct_values.push_back(make_pair("content", Value(title)));
	struct_values.push_back(make_pair("level", Value()));  // NULL - structural level not used for headings
	struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_TEXT)));
	// Store heading level in attributes
	vector<Value> keys = {Value("heading_level")};
	vector<Value> values = {Value(std::to_string(heading_level))};
	struct_values.push_back(make_pair("attributes", CreateAttributesMap(keys, values)));
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

// Helper to get heading_level from attributes, falling back to level field for backward compatibility
static int32_t GetHeadingLevel(const Value &element) {
	auto &children = StructValue::GetChildren(element);

	// First check attributes for heading_level
	auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
	if (!attrs.IsNull()) {
		auto &map_entries = MapValue::GetChildren(attrs);
		for (auto &entry : map_entries) {
			auto &kv = StructValue::GetChildren(entry);
			if (!kv[0].IsNull() && kv[0].GetValue<string>() == "heading_level") {
				if (!kv[1].IsNull()) {
					return std::stoi(kv[1].GetValue<string>());
				}
			}
		}
	}

	// Fall back to level field for backward compatibility
	if (!children[BlockTypes::LEVEL_IDX].IsNull()) {
		return children[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
	}

	return -1;
}

// Helper to create an element with updated heading_level attribute
static Value CreateElementWithHeadingLevel(const Value &element, int32_t new_heading_level) {
	auto children = StructValue::GetChildren(element);

	// Rebuild attributes map with updated heading_level
	auto &old_attrs = children[BlockTypes::ATTRIBUTES_IDX];
	vector<Value> keys;
	vector<Value> values;

	bool found = false;
	if (!old_attrs.IsNull()) {
		auto &map_entries = MapValue::GetChildren(old_attrs);
		for (auto &entry : map_entries) {
			auto &kv = StructValue::GetChildren(entry);
			if (!kv[0].IsNull()) {
				string key = kv[0].GetValue<string>();
				if (key == "heading_level") {
					keys.push_back(Value(key));
					values.push_back(Value(std::to_string(new_heading_level)));
					found = true;
				} else {
					keys.push_back(kv[0]);
					values.push_back(kv[1]);
				}
			}
		}
	}

	if (!found) {
		keys.push_back(Value("heading_level"));
		values.push_back(Value(std::to_string(new_heading_level)));
	}

	children[BlockTypes::ATTRIBUTES_IDX] = CreateAttributesMap(keys, values);
	return Value::STRUCT(BlockTypes::DuckBlockType(), std::move(children));
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
	return Value::STRUCT(BlockTypes::DuckBlockType(), std::move(children));
}

void AssemblyFunctions::DbAssembleFun(DataChunk &args, ExpressionState &state, Vector &result) {
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

		// Assign sequential element_order values
		vector<Value> assembled;
		int32_t order = 0;
		for (auto &block : blocks_list) {
			if (!block.IsNull()) {
				assembled.push_back(CreateElementWithOrder(block, order++));
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(assembled)));
	}
}

void AssemblyFunctions::DbSectionFun(DataChunk &args, ExpressionState &state, Vector &result) {
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

		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(section_blocks)));
	}
}

void AssemblyFunctions::DbRebaseLevelsFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto &offset_vec = args.data[1];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);
		auto offset_val = offset_vec.GetValue(i);

		// Handle NULL input
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value(LogicalType::LIST(BlockTypes::DuckBlockType())));
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

			// Only adjust heading_level for headings
			if (element_type == BlockTypes::TYPE_HEADING) {
				auto heading_level = GetHeadingLevel(block);
				if (heading_level > 0) {
					int32_t new_level = heading_level + offset;
					// Clamp to valid heading levels (1-6)
					if (new_level < 1) new_level = 1;
					if (new_level > 6) new_level = 6;
					rebased.push_back(CreateElementWithHeadingLevel(block, new_level));
				} else {
					rebased.push_back(block);
				}
			} else {
				rebased.push_back(block);
			}
		}

		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(rebased)));
	}
}

void AssemblyFunctions::DbConcatFun(DataChunk &args, ExpressionState &state, Vector &result) {
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

		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(concatenated)));
	}
}

void AssemblyFunctions::Register(ExtensionLoader &loader) {
	auto duck_block_type = BlockTypes::DuckBlockType();
	auto duck_block_list_type = BlockTypes::DuckBlockListType();

	// db_assemble(blocks LIST(duck_block)) -> LIST(duck_block)
	auto assemble_func = ScalarFunction(
	    "db_assemble",
	    {duck_block_list_type},
	    duck_block_list_type,
	    DbAssembleFun
	);
	loader.RegisterFunction(assemble_func);

	// Also register as db_document (alias for clarity in document construction)
	auto document_func = ScalarFunction(
	    "db_document",
	    {duck_block_list_type},
	    duck_block_list_type,
	    DbAssembleFun
	);
	loader.RegisterFunction(document_func);

	// db_section(title VARCHAR, level INTEGER, children LIST(duck_block)) -> LIST(duck_block)
	auto section_func = ScalarFunction(
	    "db_section",
	    {LogicalType::VARCHAR, LogicalType::INTEGER, duck_block_list_type},
	    duck_block_list_type,
	    DbSectionFun
	);
	loader.RegisterFunction(section_func);

	// Two-arg version: db_section(title VARCHAR, level INTEGER) -> LIST(duck_block)
	// Creates a section with just a heading (no children)
	auto section_func_2 = ScalarFunction(
	    "db_section",
	    {LogicalType::VARCHAR, LogicalType::INTEGER},
	    duck_block_list_type,
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

			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(section_blocks)));
		    }
	    }
	);
	loader.RegisterFunction(section_func_2);

	// db_rebase_levels(blocks LIST(duck_block), offset INTEGER) -> LIST(duck_block)
	auto rebase_func = ScalarFunction(
	    "db_rebase_levels",
	    {duck_block_list_type, LogicalType::INTEGER},
	    duck_block_list_type,
	    DbRebaseLevelsFun
	);
	loader.RegisterFunction(rebase_func);

	// db_concat(blocks1 LIST(duck_block), blocks2 LIST(duck_block)) -> LIST(duck_block)
	auto concat_func = ScalarFunction(
	    "db_concat",
	    {duck_block_list_type, duck_block_list_type},
	    duck_block_list_type,
	    DbConcatFun
	);
	loader.RegisterFunction(concat_func);
}

} // namespace duckdb
