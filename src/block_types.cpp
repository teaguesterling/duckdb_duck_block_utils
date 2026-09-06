#include "block_types.hpp"
#include "duckdb_compat.hpp"

#include <algorithm>
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

	UnaryExecutor::Execute<string_t, string_t>(source, CompatStructChild(result_entries, BlockTypes::CONTENT_IDX),
	                                           count, [&](string_t input) { return input; });

	// Set constant values for the other struct fields
	auto empty_attrs = CreateEmptyAttributesMap();
	for (idx_t i = 0; i < count; i++) {
		CompatStructChild(result_entries, BlockTypes::KIND_IDX).SetValue(i, Value(BlockTypes::KIND_INLINE));
		CompatStructChild(result_entries, BlockTypes::ELEMENT_TYPE_IDX).SetValue(i, Value(BlockTypes::INLINE_TEXT));
		CompatStructChild(result_entries, BlockTypes::LEVEL_IDX).SetValue(i, Value(1));
		CompatStructChild(result_entries, BlockTypes::ENCODING_IDX).SetValue(i, Value(BlockTypes::ENCODING_TEXT));
		CompatStructChild(result_entries, BlockTypes::ATTRIBUTES_IDX).SetValue(i, empty_attrs);
		CompatStructChild(result_entries, BlockTypes::ELEMENT_ORDER_IDX).SetValue(i, Value(0));
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

// The ONE widened shape the spec accepts: canonical, then a trailing `filename`.
LogicalType BlockTypes::DuckBlockWithFilenameType() {
	auto children = StructType::GetChildTypes(DuckBlockType());
	children.push_back(make_pair(BlockTypes::FIELD_FILENAME, LogicalType::VARCHAR));
	return LogicalType::STRUCT(std::move(children));
}

// ---------------------------------------------------------------------------
// Accepting the 8-field shape.
//
// DuckDB's named STRUCT-to-STRUCT cast already matches children BY NAME and skips
// a source child the target lacks, so an explicit `::duck_block[]` on an 8-field
// list has always worked and dropped `filename`. What refused the IMPLICIT cast was
// one rule in cast_rules.cpp: a child-count mismatch costs -1. The binder consults
// REGISTERED casts before that rule, so registering the pair with a cost is enough;
// the bound cast itself is DuckDB's own default, which does the name matching.
//
// Two registrations -- the struct and the list of it -- cover every function that
// binds LIST(duck_block) or duck_block, and the doc_* macros that route into them,
// from this one place. Not per-function overloads: panduck measured that two
// arities make an untyped NULL argument ambiguous ("Could not choose a best
// candidate function"), which silently removes NULL-in, empty-document-out
// behaviour from every consumer that has it.
//
// The source type is EXACT, and that is the point: `filename` in any other
// position, any other extra field, or a ninth field stays a loud binder error.
// ---------------------------------------------------------------------------
static BoundCastInfo BindDefaultCast(BindCastInput &input, const LogicalType &source, const LogicalType &target) {
	return DefaultCasts::GetDefaultCastFunction(input, source, target);
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
	CompatReferenceValue(result, kinds, args.size());
}

// The single list of declared element_type names. Shared rather than duplicated:
// `duck_blocks_lint` warns on a type not in here, and if it kept its own copy the
// two would drift -- which is the defect that made an image's alt text invisible
// in this repo, one fact written twice and checked by the party that writes both.
// Every declared encoding, in one place. The list was hardcoded in FOUR: this file's
// callers in type_functions.cpp and validation.cpp, and TWICE in the vendorable SQL --
// so adding `toml` meant editing five copies, four of which nothing compared. The
// header had the constants all along and nobody built the list from them.
const vector<string> &BlockTypes::AllEncodingNames() {
	static const vector<string> names = {
	    BlockTypes::ENCODING_TEXT, BlockTypes::ENCODING_JSON,  BlockTypes::ENCODING_YAML,     BlockTypes::ENCODING_HTML,
	    BlockTypes::ENCODING_XML,  BlockTypes::ENCODING_LATEX, BlockTypes::ENCODING_MARKDOWN, BlockTypes::ENCODING_TOML,
	};
	return names;
}

const vector<string> &BlockTypes::AllTypeNames() {
	static const vector<string> names = {
	    BlockTypes::TYPE_HEADING,       BlockTypes::TYPE_PARAGRAPH,   BlockTypes::TYPE_PLAIN,
	    BlockTypes::TYPE_CODE,          BlockTypes::TYPE_BLOCKQUOTE,  BlockTypes::TYPE_LIST,
	    BlockTypes::TYPE_LIST_ITEM,     BlockTypes::TYPE_TABLE,       BlockTypes::TYPE_HR,
	    BlockTypes::TYPE_METADATA,      BlockTypes::TYPE_IMAGE,       BlockTypes::TYPE_RAW,
	    BlockTypes::TYPE_DIV,           BlockTypes::TYPE_SECTION,     BlockTypes::TYPE_PAGE,
	    BlockTypes::TYPE_LINEBLOCK,     BlockTypes::TYPE_DEFLIST,     BlockTypes::TYPE_FIGURE,
	    BlockTypes::TYPE_CAPTION,       BlockTypes::TYPE_GENERIC,     BlockTypes::INLINE_TEXT,
	    BlockTypes::INLINE_SPACE,       BlockTypes::INLINE_SOFTBREAK, BlockTypes::INLINE_LINEBREAK,
	    BlockTypes::INLINE_BOLD,        BlockTypes::INLINE_ITALIC,    BlockTypes::INLINE_STRIKETHROUGH,
	    BlockTypes::INLINE_SUPERSCRIPT, BlockTypes::INLINE_SUBSCRIPT, BlockTypes::INLINE_SMALLCAPS,
	    BlockTypes::INLINE_UNDERLINE,   BlockTypes::INLINE_CODE,      BlockTypes::INLINE_MATH,
	    BlockTypes::INLINE_LINK,        BlockTypes::INLINE_IMAGE,     BlockTypes::INLINE_QUOTED,
	    BlockTypes::INLINE_CITE,        BlockTypes::INLINE_NOTE,      BlockTypes::INLINE_SPAN,
	    BlockTypes::INLINE_RAW,         BlockTypes::VALUE_STRING,     BlockTypes::VALUE_BOOL,
	    BlockTypes::VALUE_LIST,         BlockTypes::VALUE_MAP,        BlockTypes::VALUE_INLINES,
	    BlockTypes::VALUE_BLOCKS,       BlockTypes::VALUE_VERSION,
	};
	// DEDUPLICATED, because four NAMES are shared across two axes: code, image and raw
	// each exist as a block and an inline, and `list` as a block and a value. The list
	// above enumerates CONSTANTS; a consumer asking the build what types exist wants
	// distinct NAMES, and got "code" twice -- so len() read 47 for a 43-type vocabulary.
	//
	// Found by running duckdb_markdown's container sweep here: it swept 47 rows against
	// 43 types and the mismatch was only visible because the arm reported what it had
	// swept. Every check in this repo built a set() from this function, which is exactly
	// the coarser measurement that hides multiplicity -- the same mistake as comparing
	// advisory findings with SELECT DISTINCT, one function over.
	static const vector<string> distinct_names = [&]() {
		vector<string> out;
		for (auto &n : names) {
			if (std::find(out.begin(), out.end(), n) == out.end()) {
				out.push_back(n);
			}
		}
		return out;
	}();
	return distinct_names;
}

// Exposed so a consumer can ask the BUILD what encodings exist rather than copying a
// list -- and so the vendorable SQL's copy can be compared against it on every check.
static void BlockEncodingsFun(DataChunk &args, ExpressionState &state, Vector &result) {
	vector<Value> vals;
	for (auto &n : BlockTypes::AllEncodingNames()) {
		vals.push_back(Value(n));
	}
	CompatReferenceValue(result, Value::LIST(LogicalType::VARCHAR, std::move(vals)), args.size());
}

static void BlockTypesFun(DataChunk &args, ExpressionState &state, Vector &result) {
	vector<Value> vals;
	for (auto &n : BlockTypes::AllTypeNames()) {
		vals.push_back(Value(n));
	}
	CompatReferenceValue(result, Value::LIST(LogicalType::VARCHAR, std::move(vals)), args.size());
}

static void SpecVersionFun(DataChunk &args, ExpressionState &state, Vector &result) {
	Value v(BlockTypes::SPEC_VERSION);
	CompatReferenceValue(result, v, args.size());
}

// duck_blocks_stamp(blocks) -> blocks with a version marker appended.
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

// duck_blocks_version(blocks) -> the stamped spec version, or NULL if unstamped.
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
	// DEPRECATED since 6.4, removed in 6.5. Nothing produces or consumes it; it stays
	// one release because a user's own CAST(x AS duck_block_ext) is invisible to us.
	loader.RegisterType("duck_block_ext", DuckBlockExtType());

	auto varchar_list = LogicalType::LIST(LogicalType::VARCHAR);
	loader.RegisterFunction(ScalarFunction("duck_block_kind_names", {}, varchar_list, BlockKindsFun));
	loader.RegisterFunction(ScalarFunction("duck_block_type_names", {}, varchar_list, BlockTypesFun));
	loader.RegisterFunction(ScalarFunction("duck_block_encoding_names", {}, varchar_list, BlockEncodingsFun));
	loader.RegisterFunction(ScalarFunction("duck_block_spec_version", {}, LogicalType::VARCHAR, SpecVersionFun));
	loader.RegisterFunction(
	    ScalarFunction("duck_blocks_stamp", {DuckBlockListType()}, DuckBlockListType(), BlocksStampFun));
	loader.RegisterFunction(
	    ScalarFunction("duck_blocks_version", {DuckBlockListType()}, LogicalType::VARCHAR, BlocksVersionFun));

	// Register cast from VARCHAR to duck_block (creates text inline element)
	// Using implicit_cast_cost = -1 means explicit cast only (not implicit)
	// This avoids ambiguity in function overload resolution
	loader.RegisterCastFunction(LogicalType::VARCHAR, duck_block_type, BoundCastInfo(VarcharToDuckBlockCast), -1);

	// The 8-field shape, implicitly. A small positive cost: cheaper than any
	// to-VARCHAR fallback, dearer than an exact match, and no function here is
	// overloaded on its block argument, so nothing competes with it.
	auto with_filename = DuckBlockWithFilenameType();
	loader.RegisterCastFunction(with_filename, duck_block_type, BindDefaultCast, 10);
	loader.RegisterCastFunction(LogicalType::LIST(with_filename), DuckBlockListType(), BindDefaultCast, 10);
}

} // namespace duckdb
