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
	                                           [&](string_t input) { return input; });

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
	struct_children.push_back(make_pair("attributes", LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)));
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
	struct_children.push_back(make_pair("attributes", LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)));
	struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	// Extended fields for provenance
	struct_children.push_back(make_pair("source_format", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("file_path", LogicalType::VARCHAR));

	return LogicalType::STRUCT(std::move(struct_children));
}

// ---------------------------------------------------------------------------
// Vocabulary introspection.
//
// Exists so sibling extensions (panduck, webbed, markdown, sitting_duck) can
// ASSERT they agree with the vocabulary at test time instead of mirroring
// block_types.hpp -- a copied header drifts silently, which is the same
// two-sources-of-truth defect this family has hit repeatedly.
// ---------------------------------------------------------------------------

static Value StringListValue(const vector<const char *> &items) {
	vector<Value> vals;
	vals.reserve(items.size());
	for (auto *item : items) {
		vals.push_back(Value(item));
	}
	return Value::LIST(LogicalType::VARCHAR, std::move(vals));
}

static void BlockKindsFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto kinds = StringListValue({BlockTypes::KIND_BLOCK, BlockTypes::KIND_INLINE, BlockTypes::KIND_VALUE});
	result.Reference(kinds);
}

static void BlockTypesFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto types = StringListValue({
	    // blocks
	    BlockTypes::TYPE_HEADING,
	    BlockTypes::TYPE_PARAGRAPH,
	    BlockTypes::TYPE_CODE,
	    BlockTypes::TYPE_BLOCKQUOTE,
	    BlockTypes::TYPE_LIST,
	    BlockTypes::TYPE_LIST_ITEM,
	    BlockTypes::TYPE_TABLE,
	    BlockTypes::TYPE_HR,
	    BlockTypes::TYPE_METADATA,
	    BlockTypes::TYPE_IMAGE,
	    BlockTypes::TYPE_RAW,
	    BlockTypes::TYPE_DIV,
	    BlockTypes::TYPE_SECTION,
	    BlockTypes::TYPE_PAGE,
	    BlockTypes::TYPE_LINEBLOCK,
	    BlockTypes::TYPE_DEFLIST,
	    BlockTypes::TYPE_FIGURE,
	    BlockTypes::TYPE_CAPTION,
	    BlockTypes::TYPE_GENERIC,
	    // inlines
	    BlockTypes::INLINE_TEXT,
	    BlockTypes::INLINE_SPACE,
	    BlockTypes::INLINE_SOFTBREAK,
	    BlockTypes::INLINE_LINEBREAK,
	    BlockTypes::INLINE_BOLD,
	    BlockTypes::INLINE_ITALIC,
	    BlockTypes::INLINE_STRIKETHROUGH,
	    BlockTypes::INLINE_SUPERSCRIPT,
	    BlockTypes::INLINE_SUBSCRIPT,
	    BlockTypes::INLINE_SMALLCAPS,
	    BlockTypes::INLINE_UNDERLINE,
	    BlockTypes::INLINE_CODE,
	    BlockTypes::INLINE_MATH,
	    BlockTypes::INLINE_LINK,
	    BlockTypes::INLINE_IMAGE,
	    BlockTypes::INLINE_QUOTED,
	    BlockTypes::INLINE_CITE,
	    BlockTypes::INLINE_NOTE,
	    BlockTypes::INLINE_SPAN,
	    BlockTypes::INLINE_RAW,
	    // values
	    BlockTypes::VALUE_STRING,
	    BlockTypes::VALUE_BOOL,
	    BlockTypes::VALUE_LIST,
	    BlockTypes::VALUE_MAP,
	    BlockTypes::VALUE_INLINES,
	    BlockTypes::VALUE_BLOCKS,
	    BlockTypes::VALUE_VERSION,
	});
	result.Reference(types);
}

static void SpecVersionFun(DataChunk &args, ExpressionState &state, Vector &result) {
	Value v(BlockTypes::SPEC_VERSION);
	result.Reference(v);
}

// db_blocks_stamp(blocks) -> blocks with a version marker appended.
static void BlocksStampFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto block_type = BlockTypes::DuckBlockType();
	UnaryExecutor::Execute<list_entry_t, list_entry_t>(args.data[0], result, args.size(),
	                                                   [&](list_entry_t) { return list_entry_t(); });

	for (idx_t i = 0; i < args.size(); i++) {
		auto in = args.data[0].GetValue(i);
		if (in.IsNull()) {
			result.SetValue(i, in);
			continue;
		}
		auto children = ListValue::GetChildren(in);
		int32_t order = 0;
		for (auto &el : children) {
			if (el.IsNull()) {
				continue;
			}
			auto &f = StructValue::GetChildren(el);
			if (!f[BlockTypes::ELEMENT_ORDER_IDX].IsNull()) {
				order = MaxValue<int32_t>(order, f[BlockTypes::ELEMENT_ORDER_IDX].GetValue<int32_t>() + 1);
			}
		}
		child_list_t<Value> marker;
		marker.push_back(make_pair("kind", Value(BlockTypes::KIND_VALUE)));
		marker.push_back(make_pair("element_type", Value(BlockTypes::VALUE_VERSION)));
		marker.push_back(make_pair("content", Value(BlockTypes::SPEC_VERSION)));
		marker.push_back(make_pair("level", Value(1)));
		marker.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_TEXT)));
		marker.push_back(make_pair("attributes", CreateEmptyAttributesMap()));
		marker.push_back(make_pair("element_order", Value(order)));
		children.push_back(Value::STRUCT(std::move(marker)));
		result.SetValue(i, Value::LIST(block_type, std::move(children)));
	}
}

// db_blocks_version(blocks) -> the stamped spec version, or NULL if unstamped.
static void BlocksVersionFun(DataChunk &args, ExpressionState &state, Vector &result) {
	for (idx_t i = 0; i < args.size(); i++) {
		auto in = args.data[0].GetValue(i);
		Value found;
		if (!in.IsNull()) {
			for (auto &el : ListValue::GetChildren(in)) {
				if (el.IsNull()) {
					continue;
				}
				auto &f = StructValue::GetChildren(el);
				if (f[BlockTypes::KIND_IDX].IsNull() || f[BlockTypes::ELEMENT_TYPE_IDX].IsNull()) {
					continue;
				}
				if (f[BlockTypes::KIND_IDX].GetValue<string>() == BlockTypes::KIND_VALUE &&
				    f[BlockTypes::ELEMENT_TYPE_IDX].GetValue<string>() == BlockTypes::VALUE_VERSION) {
					found = f[BlockTypes::CONTENT_IDX];
					break;
				}
			}
		}
		result.SetValue(i, found);
	}
}

void BlockTypes::Register(ExtensionLoader &loader) {
	auto duck_block_type = DuckBlockType();
	loader.RegisterType("duck_block", duck_block_type);
	loader.RegisterType("duck_block_ext", DuckBlockExtType());

	auto varchar_list = LogicalType::LIST(LogicalType::VARCHAR);
	loader.RegisterFunction(ScalarFunction("db_block_kinds", {}, varchar_list, BlockKindsFun));
	loader.RegisterFunction(ScalarFunction("db_block_types", {}, varchar_list, BlockTypesFun));
	loader.RegisterFunction(ScalarFunction("db_block_spec_version", {}, LogicalType::VARCHAR, SpecVersionFun));
	loader.RegisterFunction(
	    ScalarFunction("db_blocks_stamp", {DuckBlockListType()}, DuckBlockListType(), BlocksStampFun));
	loader.RegisterFunction(
	    ScalarFunction("db_blocks_version", {DuckBlockListType()}, LogicalType::VARCHAR, BlocksVersionFun));

	// Register cast from VARCHAR to duck_block (creates text inline element)
	// Using implicit_cast_cost = -1 means explicit cast only (not implicit)
	// This avoids ambiguity in function overload resolution
	loader.RegisterCastFunction(LogicalType::VARCHAR, duck_block_type, BoundCastInfo(VarcharToDuckBlockCast), -1);
}

} // namespace duckdb
