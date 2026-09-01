#include "normalize.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"

namespace duckdb {

// SPEC 6.0 -- v1's content rule, applied by SHAPE rather than by type name.
//
// A container whose only block child is a `plain` HAS a single text child, and v1's rule
// says that text belongs in the container's own `content`. So `<li>text</li>` reads back
// as `list_item(content='text')`, not `list_item > plain('text')`. 5.0 shipped the second
// and that was one rule too many: a container with a single text child sometimes carried
// content and sometimes grew a child, depending on which producer built it.
//
// `plain` survives exactly where the text has nowhere else to live:
//   * alongside block siblings   -- `section > plain('Lead') + heading`
//   * at the top level           -- the document root has no content field to hold it
//
// TYPE-BLIND ON PURPOSE. This asks only about levels and adjacency, never about the
// parent's element_type, so a container type added later is covered without being listed.
// Enumerating the container types we happen to know is exactly what lost `table`,
// `deflist` and `lineblock` inside every container, in code that was correct when written.
//
// TIGHT-VS-LOOSE IS NOT LOST. Measured before the change, not assumed: the exporter
// already distinguishes the two shapes this leaves behind -- content on the item emits
// `Plain` (tight), a `paragraph` child emits `Para` (loose). That is why `plain` can be
// narrowed without a compensating export change.
void NormalizeFunctions::CollapseLonePlainIntoParent(vector<Value> &blocks) {
	auto field = [&](idx_t i, idx_t f) -> const Value & {
		return StructValue::GetChildren(blocks[i])[f];
	};
	auto str_field = [&](idx_t i, idx_t f) -> string {
		auto &v = field(i, f);
		return v.IsNull() ? string() : StringValue::Get(v);
	};
	auto is_block = [&](idx_t i) {
		return str_field(i, BlockTypes::KIND_IDX) == BlockTypes::KIND_BLOCK;
	};
	auto level_of = [&](idx_t i) -> int32_t {
		auto &v = field(i, BlockTypes::LEVEL_IDX);
		return v.IsNull() ? -1 : v.GetValue<int32_t>();
	};

	for (idx_t i = 0; i < blocks.size(); i++) {
		if (!is_block(i) || !str_field(i, BlockTypes::CONTENT_IDX).empty()) {
			// A parent already carrying content cannot adopt the text without one of
			// the two being silently dropped.
			continue;
		}
		int32_t parent_level = level_of(i);
		if (parent_level < 1) {
			continue;
		}

		idx_t j = i + 1;
		if (j >= blocks.size() || !is_block(j) || level_of(j) != parent_level + 1 ||
		    str_field(j, BlockTypes::ELEMENT_TYPE_IDX) != BlockTypes::TYPE_PLAIN) {
			continue;
		}

		// The plain's own inline children sit deeper than it does; skip past them to
		// find what comes next at the plain's level.
		idx_t k = j + 1;
		while (k < blocks.size() && level_of(k) > parent_level + 1) {
			k++;
		}
		if (k < blocks.size() && level_of(k) == parent_level + 1) {
			// A block sibling. The text has somewhere it must stay, so `plain` earns
			// its keep here -- this is the case the type exists for.
			continue;
		}

		auto plain = StructValue::GetChildren(blocks[j]);
		if (!plain[BlockTypes::ATTRIBUTES_IDX].IsNull() &&
		    !MapValue::GetChildren(plain[BlockTypes::ATTRIBUTES_IDX]).empty()) {
			// Pandoc's Plain carries no Attr, so this never fires for this reader. It is
			// here so a producer that DOES attach attributes loses the collapse rather
			// than the attributes: a visible extra element beats a silent drop.
			continue;
		}

		auto parent = StructValue::GetChildren(blocks[i]);
		parent[BlockTypes::CONTENT_IDX] = plain[BlockTypes::CONTENT_IDX];
		parent[BlockTypes::ENCODING_IDX] = plain[BlockTypes::ENCODING_IDX];
		blocks[i] = Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent));

		// The plain's inlines become the parent's, one level shallower. Level is
		// structural depth in a depth-first ordering, so removing an element from the
		// chain MUST move everything below it up -- leaving a gap would break the one
		// invariant the whole tree is reconstructed from.
		for (idx_t d = j + 1; d < k; d++) {
			auto child = StructValue::GetChildren(blocks[d]);
			child[BlockTypes::LEVEL_IDX] = Value(level_of(d) - 1);
			blocks[d] = Value::STRUCT(BlockTypes::DuckBlockType(), std::move(child));
		}
		blocks.erase(blocks.begin() + j);

		// STEP BACK and re-examine this parent. Its adopted content may be EMPTY -- a
		// plain whose own child holds the text -- in which case the parent is still
		// eligible and its next child may now be a lone plain in turn.
		//
		// This said "no step-back: the parent now carries content, so it can never
		// itself be the lone plain child of ITS parent". That premise is false exactly
		// when the content adopted is empty, and `str_field` maps NULL and "" to the
		// same empty string, so a CHAIN of plains collapsed one level per call:
		//
		//   paragraph > plain > plain > plain('foo')
		//     one pass  -> paragraph > plain('foo')      -- and lint objects to that
		//     two passes -> paragraph('foo')             -- correct
		//
		// So the normalizer emitted a shape its own published rule warns about, and
		// duck_blocks_lint already said so; nothing had put the two in one query.
		// Found by the panduck session by CALLING it, and it is the reason the fixpoint
		// property -- lint(normalize(x)) is empty -- is now asserted rather than
		// idempotence alone. My idempotence test was true only of the single-level case
		// I happened to write.
		//
		// Terminates: every collapse erases exactly one element.
		i--;
	}
}

// duck_blocks_normalize(blocks) -> blocks
//
// Idempotent by construction: after the pass every collapsible run has already been
// collapsed, so a second call finds nothing to do. That matters because a producer
// cannot always tell whether its input was normalised upstream, and a normaliser that
// is unsafe to run twice is one nobody can call defensively.
static void DbBlocksNormalizeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);
		if (blocks_val.IsNull()) {
			result.SetValue(i, blocks_val);
			continue;
		}
		auto blocks = ListValue::GetChildren(blocks_val);
		vector<Value> working(blocks.begin(), blocks.end());
		NormalizeFunctions::CollapseLonePlainIntoParent(working);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(working)));
	}
	result.SetVectorType(count == 1 ? VectorType::CONSTANT_VECTOR : VectorType::FLAT_VECTOR);
}

void NormalizeFunctions::Register(ExtensionLoader &loader) {
	auto duck_block_list_type = BlockTypes::DuckBlockListType();
	ScalarFunction fn("duck_blocks_normalize", {duck_block_list_type}, duck_block_list_type, DbBlocksNormalizeFun);
	loader.RegisterFunction(fn);
}

} // namespace duckdb
