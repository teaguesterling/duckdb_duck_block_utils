#include "type_functions.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"

namespace duckdb {

// Valid block types
static const std::unordered_set<string> VALID_BLOCK_TYPES = {
    "heading", "paragraph", "code", "blockquote", "list",
    "table", "hr", "metadata", "image", "raw"
};

// Valid encodings
static const std::unordered_set<string> VALID_ENCODINGS = {
    "text", "json", "yaml", "html", "xml"
};

// Helper to get string field from block
static string GetStringField(const Value &block, idx_t idx) {
	auto &children = StructValue::GetChildren(block);
	if (idx >= children.size() || children[idx].IsNull()) {
		return "";
	}
	return children[idx].GetValue<string>();
}

// Helper to get int field from block
static int32_t GetIntField(const Value &block, idx_t idx, int32_t default_val = 0) {
	auto &children = StructValue::GetChildren(block);
	if (idx >= children.size() || children[idx].IsNull()) {
		return default_val;
	}
	return children[idx].GetValue<int32_t>();
}

// Helper to create empty attributes map
static Value CreateEmptyMap() {
	return Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, vector<Value>(), vector<Value>());
}

// Helper to get attribute from block
static string GetAttribute(const Value &block, const string &key) {
	auto &children = StructValue::GetChildren(block);
	auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
	if (attrs.IsNull()) {
		return "";
	}

	auto &map_entries = MapValue::GetChildren(attrs);
	for (auto &entry : map_entries) {
		if (entry.IsNull()) continue;
		auto &kv = StructValue::GetChildren(entry);
		if (kv.size() >= 2 && !kv[0].IsNull() && kv[0].GetValue<string>() == key) {
			if (!kv[1].IsNull()) {
				return kv[1].GetValue<string>();
			}
		}
	}
	return "";
}

void TypeFunctions::DuckBlockFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &block_type_vec = args.data[0];
	auto &content_vec = args.data[1];
	auto &level_vec = args.data[2];
	auto &encoding_vec = args.data[3];
	auto &attributes_vec = args.data[4];
	auto &block_order_vec = args.data[5];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, block_type_vec.GetValue(i));
		struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, content_vec.GetValue(i));
		struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, level_vec.GetValue(i));
		struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, encoding_vec.GetValue(i));
		struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, attributes_vec.GetValue(i));
		struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, block_order_vec.GetValue(i));
	}
}

void TypeFunctions::DuckBlockSimpleFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &block_type_vec = args.data[0];
	auto &content_vec = args.data[1];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, block_type_vec.GetValue(i));
		struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, content_vec.GetValue(i));
		struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value());  // NULL
		struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value("text"));
		struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, CreateEmptyMap());
		struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, Value(0));
	}
}

void TypeFunctions::ToDuckBlockFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &input_vec = args.data[0];

	auto count = args.size();
	auto &struct_entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto input = input_vec.GetValue(i);

		if (input.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}

		auto &children = StructValue::GetChildren(input);

		// Extract fields by name if possible, or by position
		Value block_type, content, level, encoding, attributes, block_order;

		if (children.size() >= 6) {
			block_type = children[0];
			content = children[1];
			level = children[2];
			encoding = children[3];
			attributes = children[4];
			block_order = children[5];
		} else {
			// Minimal struct - fill in defaults
			block_type = children.size() > 0 ? children[0] : Value("paragraph");
			content = children.size() > 1 ? children[1] : Value("");
			level = Value();
			encoding = Value("text");
			attributes = CreateEmptyMap();
			block_order = Value(0);
		}

		struct_entries[BlockTypes::BLOCK_TYPE_IDX]->SetValue(i, block_type);
		struct_entries[BlockTypes::CONTENT_IDX]->SetValue(i, content);
		struct_entries[BlockTypes::LEVEL_IDX]->SetValue(i, level);
		struct_entries[BlockTypes::ENCODING_IDX]->SetValue(i, encoding);
		struct_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, attributes);
		struct_entries[BlockTypes::BLOCK_ORDER_IDX]->SetValue(i, block_order);
	}
}

void TypeFunctions::DuckBlockValidFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &block_vec = args.data[0];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto block = block_vec.GetValue(i);

		if (block.IsNull()) {
			result.SetValue(i, Value(false));
			continue;
		}

		auto &children = StructValue::GetChildren(block);

		// Check required structure
		if (children.size() < 6) {
			result.SetValue(i, Value(false));
			continue;
		}

		// Check block_type is valid
		if (children[BlockTypes::BLOCK_TYPE_IDX].IsNull()) {
			result.SetValue(i, Value(false));
			continue;
		}
		auto block_type = children[BlockTypes::BLOCK_TYPE_IDX].GetValue<string>();
		if (VALID_BLOCK_TYPES.find(block_type) == VALID_BLOCK_TYPES.end()) {
			result.SetValue(i, Value(false));
			continue;
		}

		// Check encoding is valid (if not null)
		if (!children[BlockTypes::ENCODING_IDX].IsNull()) {
			auto encoding = children[BlockTypes::ENCODING_IDX].GetValue<string>();
			if (VALID_ENCODINGS.find(encoding) == VALID_ENCODINGS.end()) {
				result.SetValue(i, Value(false));
				continue;
			}
		}

		// Check block_order is non-negative (if not null)
		if (!children[BlockTypes::BLOCK_ORDER_IDX].IsNull()) {
			auto order = children[BlockTypes::BLOCK_ORDER_IDX].GetValue<int32_t>();
			if (order < 0) {
				result.SetValue(i, Value(false));
				continue;
			}
		}

		result.SetValue(i, Value(true));
	}
}

void TypeFunctions::DuckBlockTypeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &block_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto block = block_vec.GetValue(i);
		if (block.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}
		result.SetValue(i, StructValue::GetChildren(block)[BlockTypes::BLOCK_TYPE_IDX]);
	}
}

void TypeFunctions::DuckBlockContentFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &block_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto block = block_vec.GetValue(i);
		if (block.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}
		result.SetValue(i, StructValue::GetChildren(block)[BlockTypes::CONTENT_IDX]);
	}
}

void TypeFunctions::DuckBlockLevelFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &block_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto block = block_vec.GetValue(i);
		if (block.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}
		result.SetValue(i, StructValue::GetChildren(block)[BlockTypes::LEVEL_IDX]);
	}
}

void TypeFunctions::DuckBlockEncodingFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &block_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto block = block_vec.GetValue(i);
		if (block.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}
		result.SetValue(i, StructValue::GetChildren(block)[BlockTypes::ENCODING_IDX]);
	}
}

void TypeFunctions::DuckBlockOrderFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &block_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto block = block_vec.GetValue(i);
		if (block.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}
		result.SetValue(i, StructValue::GetChildren(block)[BlockTypes::BLOCK_ORDER_IDX]);
	}
}

void TypeFunctions::DuckBlockAttrFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &block_vec = args.data[0];
	auto &key_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto block = block_vec.GetValue(i);
		auto key = key_vec.GetValue(i);

		if (block.IsNull() || key.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}

		auto attr_value = GetAttribute(block, key.GetValue<string>());
		if (attr_value.empty()) {
			result.SetValue(i, Value());
		} else {
			result.SetValue(i, Value(attr_value));
		}
	}
}

void TypeFunctions::DuckBlockSetOrderFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &block_vec = args.data[0];
	auto &order_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto block = block_vec.GetValue(i);
		auto new_order = order_vec.GetValue(i);

		if (block.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}

		auto children = StructValue::GetChildren(block);
		children[BlockTypes::BLOCK_ORDER_IDX] = new_order;
		result.SetValue(i, Value::STRUCT(BlockTypes::DocBlockType(), std::move(children)));
	}
}

void TypeFunctions::DuckBlockSetContentFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &block_vec = args.data[0];
	auto &content_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto block = block_vec.GetValue(i);
		auto new_content = content_vec.GetValue(i);

		if (block.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}

		auto children = StructValue::GetChildren(block);
		children[BlockTypes::CONTENT_IDX] = new_content;
		result.SetValue(i, Value::STRUCT(BlockTypes::DocBlockType(), std::move(children)));
	}
}

void TypeFunctions::DuckBlockSetLevelFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &block_vec = args.data[0];
	auto &level_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto block = block_vec.GetValue(i);
		auto new_level = level_vec.GetValue(i);

		if (block.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}

		auto children = StructValue::GetChildren(block);
		children[BlockTypes::LEVEL_IDX] = new_level;
		result.SetValue(i, Value::STRUCT(BlockTypes::DocBlockType(), std::move(children)));
	}
}

void TypeFunctions::Register(ExtensionLoader &loader) {
	auto doc_block_type = BlockTypes::DocBlockType();

	// duck_block(block_type, content, level, encoding, attributes, block_order) -> doc_block
	auto full_constructor = ScalarFunction(
	    "duck_block",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::INTEGER,
	     LogicalType::VARCHAR, LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR),
	     LogicalType::INTEGER},
	    doc_block_type,
	    DuckBlockFun
	);
	loader.RegisterFunction(full_constructor);

	// duck_block(block_type, content) -> doc_block
	auto simple_constructor = ScalarFunction(
	    "duck_block",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR},
	    doc_block_type,
	    DuckBlockSimpleFun
	);
	loader.RegisterFunction(simple_constructor);

	// to_duck_block(struct) -> doc_block
	// Accept any struct type
	auto to_block = ScalarFunction(
	    "to_duck_block",
	    {LogicalType::ANY},
	    doc_block_type,
	    ToDuckBlockFun
	);
	loader.RegisterFunction(to_block);

	// duck_block_valid(block) -> BOOLEAN
	auto valid_func = ScalarFunction(
	    "duck_block_valid",
	    {doc_block_type},
	    LogicalType::BOOLEAN,
	    DuckBlockValidFun
	);
	loader.RegisterFunction(valid_func);

	// duck_block_type(block) -> VARCHAR
	auto type_func = ScalarFunction(
	    "duck_block_type",
	    {doc_block_type},
	    LogicalType::VARCHAR,
	    DuckBlockTypeFun
	);
	loader.RegisterFunction(type_func);

	// duck_block_content(block) -> VARCHAR
	auto content_func = ScalarFunction(
	    "duck_block_content",
	    {doc_block_type},
	    LogicalType::VARCHAR,
	    DuckBlockContentFun
	);
	loader.RegisterFunction(content_func);

	// duck_block_level(block) -> INTEGER
	auto level_func = ScalarFunction(
	    "duck_block_level",
	    {doc_block_type},
	    LogicalType::INTEGER,
	    DuckBlockLevelFun
	);
	loader.RegisterFunction(level_func);

	// duck_block_encoding(block) -> VARCHAR
	auto encoding_func = ScalarFunction(
	    "duck_block_encoding",
	    {doc_block_type},
	    LogicalType::VARCHAR,
	    DuckBlockEncodingFun
	);
	loader.RegisterFunction(encoding_func);

	// duck_block_order(block) -> INTEGER
	auto order_func = ScalarFunction(
	    "duck_block_order",
	    {doc_block_type},
	    LogicalType::INTEGER,
	    DuckBlockOrderFun
	);
	loader.RegisterFunction(order_func);

	// duck_block_attr(block, key) -> VARCHAR
	auto attr_func = ScalarFunction(
	    "duck_block_attr",
	    {doc_block_type, LogicalType::VARCHAR},
	    LogicalType::VARCHAR,
	    DuckBlockAttrFun
	);
	loader.RegisterFunction(attr_func);

	// duck_block_set_order(block, new_order) -> doc_block
	auto set_order_func = ScalarFunction(
	    "duck_block_set_order",
	    {doc_block_type, LogicalType::INTEGER},
	    doc_block_type,
	    DuckBlockSetOrderFun
	);
	loader.RegisterFunction(set_order_func);

	// duck_block_set_content(block, new_content) -> doc_block
	auto set_content_func = ScalarFunction(
	    "duck_block_set_content",
	    {doc_block_type, LogicalType::VARCHAR},
	    doc_block_type,
	    DuckBlockSetContentFun
	);
	loader.RegisterFunction(set_content_func);

	// duck_block_set_level(block, new_level) -> doc_block
	auto set_level_func = ScalarFunction(
	    "duck_block_set_level",
	    {doc_block_type, LogicalType::INTEGER},
	    doc_block_type,
	    DuckBlockSetLevelFun
	);
	loader.RegisterFunction(set_level_func);
}

} // namespace duckdb
