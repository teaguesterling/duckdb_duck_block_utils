#include "block_types.hpp"
#include "duckdb/function/cast/default_casts.hpp"
#include "duckdb/common/operator/string_cast.hpp"
#include "duckdb/common/vector_operations/unary_executor.hpp"

namespace duckdb {

// Helper to create empty attributes map
static Value CreateEmptyAttributesMap() {
	return Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, vector<Value>(), vector<Value>());
}

// Cast function: VARCHAR -> duck_block (creates text inline element)
static bool VarcharToDuckBlockCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
	auto &result_entries = StructVector::GetEntries(result);

	UnaryExecutor::Execute<string_t, string_t>(source, *result_entries[BlockTypes::CONTENT_IDX], count,
	    [&](string_t input) {
		    return input;
	    });

	// Set constant values for the other struct fields
	auto empty_attrs = CreateEmptyAttributesMap();
	for (idx_t i = 0; i < count; i++) {
		result_entries[BlockTypes::KIND_IDX]->SetValue(i, Value(BlockTypes::KIND_INLINE));
		result_entries[BlockTypes::ELEMENT_TYPE_IDX]->SetValue(i, Value(BlockTypes::INLINE_TEXT));
		result_entries[BlockTypes::LEVEL_IDX]->SetValue(i, Value(1));
		result_entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(BlockTypes::ENCODING_TEXT));
		result_entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, empty_attrs);
		result_entries[BlockTypes::ELEMENT_ORDER_IDX]->SetValue(i, Value(0));
	}

	return true;
}

LogicalType BlockTypes::DuckBlockType() {
	child_list_t<LogicalType> struct_children;
	struct_children.push_back(make_pair("kind", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("element_type", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("level", LogicalType::INTEGER));
	struct_children.push_back(make_pair("encoding", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("attributes",
	                                    LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)));
	struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));

	return LogicalType::STRUCT(std::move(struct_children));
}

LogicalType BlockTypes::DuckBlockListType() {
	return LogicalType::LIST(DuckBlockType());
}

LogicalType BlockTypes::DuckBlockExtType() {
	child_list_t<LogicalType> struct_children;
	struct_children.push_back(make_pair("kind", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("element_type", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("level", LogicalType::INTEGER));
	struct_children.push_back(make_pair("encoding", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("attributes",
	                                    LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)));
	struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	// Extended fields for provenance
	struct_children.push_back(make_pair("source_format", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("file_path", LogicalType::VARCHAR));

	return LogicalType::STRUCT(std::move(struct_children));
}

void BlockTypes::Register(ExtensionLoader &loader) {
	auto duck_block_type = DuckBlockType();
	loader.RegisterType("duck_block", duck_block_type);
	loader.RegisterType("duck_block_ext", DuckBlockExtType());

	// Register cast from VARCHAR to duck_block (creates text inline element)
	// Using implicit_cast_cost = -1 means explicit cast only (not implicit)
	// This avoids ambiguity in function overload resolution
	loader.RegisterCastFunction(LogicalType::VARCHAR, duck_block_type,
	                            BoundCastInfo(VarcharToDuckBlockCast), -1);
}

} // namespace duckdb
