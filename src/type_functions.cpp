#include "type_functions.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"

namespace duckdb {

// Valid block types
static const std::unordered_set<string> VALID_BLOCK_TYPES = {"heading", "paragraph", "code",     "blockquote", "list",
                                                             "table",   "hr",        "metadata", "image",      "raw"};

// Valid encodings
static const std::unordered_set<string> VALID_ENCODINGS = {"text", "json", "yaml", "html", "xml", "latex", "markdown"};

// Helper to create empty attributes map
static Value CreateEmptyMap() {
	return Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, vector<Value>(), vector<Value>());
}

// Helper to get attribute from element
static string GetAttribute(const Value &element, const string &key) {
	auto &children = StructValue::GetChildren(element);
	auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
	if (attrs.IsNull()) {
		return "";
	}

	auto &map_entries = MapValue::GetChildren(attrs);
	for (auto &entry : map_entries) {
		if (entry.IsNull())
			continue;
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
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		entries[BlockTypes::KIND_IDX]->SetValue(i, Value(BlockTypes::KIND_BLOCK));
		entries[BlockTypes::ELEMENT_TYPE_IDX]->SetValue(i, block_type_vec.GetValue(i));
		entries[BlockTypes::CONTENT_IDX]->SetValue(i, content_vec.GetValue(i));
		entries[BlockTypes::LEVEL_IDX]->SetValue(i, level_vec.GetValue(i));
		entries[BlockTypes::ENCODING_IDX]->SetValue(i, encoding_vec.GetValue(i));
		entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, attributes_vec.GetValue(i));
		entries[BlockTypes::ELEMENT_ORDER_IDX]->SetValue(i, block_order_vec.GetValue(i));
	}
}

void TypeFunctions::DuckBlockSimpleFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &block_type_vec = args.data[0];
	auto &content_vec = args.data[1];

	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		entries[BlockTypes::KIND_IDX]->SetValue(i, Value(BlockTypes::KIND_BLOCK));
		entries[BlockTypes::ELEMENT_TYPE_IDX]->SetValue(i, block_type_vec.GetValue(i));
		entries[BlockTypes::CONTENT_IDX]->SetValue(i, content_vec.GetValue(i));
		entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value());
		entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value("text"));
		entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, CreateEmptyMap());
		entries[BlockTypes::ELEMENT_ORDER_IDX]->SetValue(i, Value(0));
	}
}

void TypeFunctions::ToDuckBlockFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &input_vec = args.data[0];

	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto input = input_vec.GetValue(i);

		if (input.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}

		auto &children = StructValue::GetChildren(input);

		// Extract fields - now expects 7 fields for duck_block
		Value kind, element_type, content, level, encoding, attributes, element_order;

		if (children.size() >= 7) {
			kind = children[0];
			element_type = children[1];
			content = children[2];
			level = children[3];
			encoding = children[4];
			attributes = children[5];
			element_order = children[6];
		} else {
			// Minimal struct - fill in defaults
			kind = Value(BlockTypes::KIND_BLOCK);
			element_type = children.size() > 0 ? children[0] : Value("paragraph");
			content = children.size() > 1 ? children[1] : Value("");
			level = Value();
			encoding = Value("text");
			attributes = CreateEmptyMap();
			element_order = Value(0);
		}

		entries[BlockTypes::KIND_IDX]->SetValue(i, kind);
		entries[BlockTypes::ELEMENT_TYPE_IDX]->SetValue(i, element_type);
		entries[BlockTypes::CONTENT_IDX]->SetValue(i, content);
		entries[BlockTypes::LEVEL_IDX]->SetValue(i, level);
		entries[BlockTypes::ENCODING_IDX]->SetValue(i, encoding);
		entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, attributes);
		entries[BlockTypes::ELEMENT_ORDER_IDX]->SetValue(i, element_order);
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

		// Check required structure (7 fields for duck_block)
		if (children.size() < 7) {
			result.SetValue(i, Value(false));
			continue;
		}

		// Check element_type is valid
		if (children[BlockTypes::ELEMENT_TYPE_IDX].IsNull()) {
			result.SetValue(i, Value(false));
			continue;
		}
		auto element_type = children[BlockTypes::ELEMENT_TYPE_IDX].GetValue<string>();
		if (VALID_BLOCK_TYPES.find(element_type) == VALID_BLOCK_TYPES.end()) {
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

		// Check element_order is non-negative (if not null)
		if (!children[BlockTypes::ELEMENT_ORDER_IDX].IsNull()) {
			auto order = children[BlockTypes::ELEMENT_ORDER_IDX].GetValue<int32_t>();
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
		result.SetValue(i, StructValue::GetChildren(block)[BlockTypes::ELEMENT_TYPE_IDX]);
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
		result.SetValue(i, StructValue::GetChildren(block)[BlockTypes::ELEMENT_ORDER_IDX]);
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
		children[BlockTypes::ELEMENT_ORDER_IDX] = new_order;
		result.SetValue(i, Value::STRUCT(BlockTypes::DuckBlockType(), std::move(children)));
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
		result.SetValue(i, Value::STRUCT(BlockTypes::DuckBlockType(), std::move(children)));
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
		result.SetValue(i, Value::STRUCT(BlockTypes::DuckBlockType(), std::move(children)));
	}
}

void TypeFunctions::Register(ExtensionLoader &loader) {
	auto duck_block_type = BlockTypes::DuckBlockType();

	// duck_block(block_type, content, level, encoding, attributes, block_order) -> duck_block
	loader.RegisterFunction(
	    ScalarFunction("duck_block",
	                   {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::INTEGER, LogicalType::VARCHAR,
	                    LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR), LogicalType::INTEGER},
	                   duck_block_type, DuckBlockFun));

	// duck_block(block_type, content) -> duck_block
	loader.RegisterFunction(ScalarFunction("duck_block", {LogicalType::VARCHAR, LogicalType::VARCHAR}, duck_block_type,
	                                       DuckBlockSimpleFun));

	// to_duck_block(struct) -> duck_block
	loader.RegisterFunction(ScalarFunction("to_duck_block", {LogicalType::ANY}, duck_block_type, ToDuckBlockFun));

	// duck_block_valid(element) -> BOOLEAN
	loader.RegisterFunction(
	    ScalarFunction("duck_block_valid", {duck_block_type}, LogicalType::BOOLEAN, DuckBlockValidFun));

	// duck_block_type(element) -> VARCHAR
	loader.RegisterFunction(
	    ScalarFunction("duck_block_type", {duck_block_type}, LogicalType::VARCHAR, DuckBlockTypeFun));

	// duck_block_content(element) -> VARCHAR
	loader.RegisterFunction(
	    ScalarFunction("duck_block_content", {duck_block_type}, LogicalType::VARCHAR, DuckBlockContentFun));

	// duck_block_level(element) -> INTEGER
	loader.RegisterFunction(
	    ScalarFunction("duck_block_level", {duck_block_type}, LogicalType::INTEGER, DuckBlockLevelFun));

	// duck_block_encoding(element) -> VARCHAR
	loader.RegisterFunction(
	    ScalarFunction("duck_block_encoding", {duck_block_type}, LogicalType::VARCHAR, DuckBlockEncodingFun));

	// duck_block_order(element) -> INTEGER
	loader.RegisterFunction(
	    ScalarFunction("duck_block_order", {duck_block_type}, LogicalType::INTEGER, DuckBlockOrderFun));

	// duck_block_attr(element, key) -> VARCHAR
	loader.RegisterFunction(ScalarFunction("duck_block_attr", {duck_block_type, LogicalType::VARCHAR},
	                                       LogicalType::VARCHAR, DuckBlockAttrFun));

	// duck_block_set_order(element, new_order) -> duck_block
	loader.RegisterFunction(ScalarFunction("duck_block_set_order", {duck_block_type, LogicalType::INTEGER},
	                                       duck_block_type, DuckBlockSetOrderFun));

	// duck_block_set_content(element, new_content) -> duck_block
	loader.RegisterFunction(ScalarFunction("duck_block_set_content", {duck_block_type, LogicalType::VARCHAR},
	                                       duck_block_type, DuckBlockSetContentFun));

	// duck_block_set_level(element, new_level) -> duck_block
	loader.RegisterFunction(ScalarFunction("duck_block_set_level", {duck_block_type, LogicalType::INTEGER},
	                                       duck_block_type, DuckBlockSetLevelFun));
}

} // namespace duckdb
