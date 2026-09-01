#include "validation.hpp"
#include "block_types.hpp"
#include "pandoc_convert_util.hpp"
#include "duckdb/common/types/value.hpp"

#include <set>
#include <map>

namespace duckdb {

// Helper to get a string field from an element struct
static string GetElementStringField(const Value &element, idx_t field_idx) {
	auto &children = StructValue::GetChildren(element);
	if (children[field_idx].IsNull()) {
		return "";
	}
	return children[field_idx].GetValue<string>();
}

// Helper to get an int field from an element struct
static int32_t GetElementIntField(const Value &element, idx_t field_idx, int32_t default_val = 0) {
	auto &children = StructValue::GetChildren(element);
	if (children[field_idx].IsNull()) {
		return default_val;
	}
	return children[field_idx].GetValue<int32_t>();
}

// Helper to get attribute value from an element
static string GetElementAttribute(const Value &element, const string &key) {
	auto &children = StructValue::GetChildren(element);
	auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
	if (attrs.IsNull()) {
		return "";
	}

	auto &map_entries = MapValue::GetChildren(attrs);
	for (auto &entry : map_entries) {
		if (entry.IsNull()) {
			continue;
		}
		auto &kv = StructValue::GetChildren(entry);
		if (kv.size() >= 2 && !kv[0].IsNull() && kv[0].GetValue<string>() == key) {
			if (!kv[1].IsNull()) {
				return kv[1].GetValue<string>();
			}
		}
	}
	return "";
}

// Valid encodings
static const std::set<string> VALID_ENCODINGS = {"text", "json", "yaml", "html", "xml", "latex", "markdown"};

// Valid kinds
// Built from the vocabulary, not written out. kind='value' was added without
// updating a hardcoded pair here, and every document carrying metadata then
// validated as invalid -- validation is where this extension owns the spec, so
// it must not hold a second, private copy of what the spec says.
static const std::set<string> VALID_KINDS = {BlockTypes::KIND_BLOCK, BlockTypes::KIND_INLINE, BlockTypes::KIND_VALUE};

void ValidationFunctions::DbBlocksValidateFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];

	// Define error struct type
	child_list_t<LogicalType> error_struct_children;
	error_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	error_struct_children.push_back(make_pair("field", LogicalType::VARCHAR));
	error_struct_children.push_back(make_pair("message", LogicalType::VARCHAR));
	auto error_struct_type = LogicalType::STRUCT(std::move(error_struct_children));
	auto error_list_type = LogicalType::LIST(error_struct_type);

	// Define result struct type
	child_list_t<LogicalType> result_struct_children;
	result_struct_children.push_back(make_pair("valid", LogicalType::BOOLEAN));
	result_struct_children.push_back(make_pair("errors", error_list_type));

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		// Handle NULL input
		if (blocks_val.IsNull()) {
			child_list_t<Value> result_values;
			result_values.push_back(make_pair("valid", Value(true)));
			result_values.push_back(make_pair("errors", Value::LIST(error_struct_type, vector<Value>())));
			result.SetValue(i, Value::STRUCT(std::move(result_values)));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);
		vector<Value> errors;
		std::set<int32_t> seen_orders;
		// Depth of the previous element, for the descend-by-one rule below.
		int32_t prev_level = 0;

		for (auto &block : blocks_list) {
			if (block.IsNull()) {
				continue;
			}

			auto element_order = GetElementIntField(block, BlockTypes::ELEMENT_ORDER_IDX, -1);
			auto kind = GetElementStringField(block, BlockTypes::KIND_IDX);
			auto element_type = GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX);
			auto encoding = GetElementStringField(block, BlockTypes::ENCODING_IDX);

			// LEVEL. `level` and adjacency together describe the entire document tree,
			// so a malformed level does not degrade the structure -- it destroys it.
			// Nothing checked this until 2026-08-31, which is precisely why a
			// NULL-at-top-level convention could be introduced here and propagate to
			// four extensions without anything objecting. NULL, 0, -5 and a 1 -> 3 jump
			// with no parent all returned valid:true.
			{
				auto &lvl_children = StructValue::GetChildren(block);
				const bool level_null =
				    BlockTypes::LEVEL_IDX >= lvl_children.size() || lvl_children[BlockTypes::LEVEL_IDX].IsNull();
				if (level_null) {
					child_list_t<Value> error_values;
					error_values.push_back(make_pair("element_order", Value(element_order)));
					error_values.push_back(make_pair("field", Value("level")));
					error_values.push_back(make_pair(
					    "message", Value("level is NULL; every element carries an explicit structural depth")));
					errors.push_back(Value::STRUCT(std::move(error_values)));
				} else {
					const int32_t lvl = lvl_children[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
					if (lvl < 1) {
						child_list_t<Value> error_values;
						error_values.push_back(make_pair("element_order", Value(element_order)));
						error_values.push_back(make_pair("field", Value("level")));
						error_values.push_back(make_pair(
						    "message", Value("level " + std::to_string(lvl) + " is below 1; top level is 1")));
						errors.push_back(Value::STRUCT(std::move(error_values)));
					} else if (prev_level > 0 && lvl > prev_level + 1) {
						// Depth-first ordering descends one level at a time. A jump means
						// the parent this element claims is not in the list, so the tree
						// cannot be reconstructed from level and adjacency.
						child_list_t<Value> error_values;
						error_values.push_back(make_pair("element_order", Value(element_order)));
						error_values.push_back(make_pair("field", Value("level")));
						error_values.push_back(
						    make_pair("message", Value("level jumps from " + std::to_string(prev_level) + " to " +
						                               std::to_string(lvl) +
						                               "; depth-first ordering descends one at a time, "
						                               "so this element's parent is missing")));
						errors.push_back(Value::STRUCT(std::move(error_values)));
					}
					prev_level = lvl;
				}
			}

			// Check kind is valid
			if (!kind.empty() && VALID_KINDS.find(kind) == VALID_KINDS.end()) {
				child_list_t<Value> error_values;
				error_values.push_back(make_pair("element_order", Value(element_order)));
				error_values.push_back(make_pair("field", Value("kind")));
				error_values.push_back(
				    make_pair("message", Value("Invalid kind '" + kind + "'; see duck_block_kind_names()")));
				errors.push_back(Value::STRUCT(std::move(error_values)));
			}

			// Check element_type is non-empty
			if (element_type.empty()) {
				child_list_t<Value> error_values;
				error_values.push_back(make_pair("element_order", Value(element_order)));
				error_values.push_back(make_pair("field", Value("element_type")));
				error_values.push_back(make_pair("message", Value("element_type cannot be empty")));
				errors.push_back(Value::STRUCT(std::move(error_values)));
			}

			// Check encoding is valid
			if (!encoding.empty() && VALID_ENCODINGS.find(encoding) == VALID_ENCODINGS.end()) {
				child_list_t<Value> error_values;
				error_values.push_back(make_pair("element_order", Value(element_order)));
				error_values.push_back(make_pair("field", Value("encoding")));
				error_values.push_back(make_pair("message", Value("Invalid encoding '" + encoding + "'")));
				errors.push_back(Value::STRUCT(std::move(error_values)));
			}

			// Check element_order is non-negative
			if (element_order < 0) {
				child_list_t<Value> error_values;
				error_values.push_back(make_pair("element_order", Value(element_order)));
				error_values.push_back(make_pair("field", Value("element_order")));
				error_values.push_back(make_pair("message", Value("element_order must be non-negative")));
				errors.push_back(Value::STRUCT(std::move(error_values)));
			}

			// Check for duplicate element_order
			if (element_order >= 0 && seen_orders.count(element_order)) {
				child_list_t<Value> error_values;
				error_values.push_back(make_pair("element_order", Value(element_order)));
				error_values.push_back(make_pair("field", Value("element_order")));
				error_values.push_back(make_pair("message", Value("Duplicate element_order value")));
				errors.push_back(Value::STRUCT(std::move(error_values)));
			}
			seen_orders.insert(element_order);
		}

		child_list_t<Value> result_values;
		result_values.push_back(make_pair("valid", Value(errors.empty())));
		result_values.push_back(make_pair("errors", Value::LIST(error_struct_type, std::move(errors))));
		result.SetValue(i, Value::STRUCT(std::move(result_values)));
	}
}

void ValidationFunctions::DbBlocksLintFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];

	// Define warning struct type
	child_list_t<LogicalType> warning_struct_children;
	warning_struct_children.push_back(make_pair("severity", LogicalType::VARCHAR));
	warning_struct_children.push_back(make_pair("message", LogicalType::VARCHAR));
	warning_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	auto warning_struct_type = LogicalType::STRUCT(std::move(warning_struct_children));

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		// Handle NULL input
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value::LIST(warning_struct_type, vector<Value>()));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);
		vector<Value> warnings;

		int32_t last_heading_level = 0;
		int32_t last_order = -1;

		// A container that owns nothing. Per the spec a container's children follow it
		// at level + 1, so a container whose next BLOCK is at its own depth or shallower
		// owns no children -- and the blocks that were meant to be inside it are now
		// siblings. That renders as an empty container with its content beside it, which
		// on a rendered page LOOKS CORRECT: the text sits where a reader expects it and
		// nothing is missing, so eyeballing output never finds it.
		//
		// Found live in the portfolio: panduck's epub reader emitted blockquote and its
		// paragraph both at NULL level, and webbed's HTML writer -- which expresses
		// containment through level -- closed the blockquote before the paragraph
		// rendered. Nothing flagged it, because duck_blocks_validate() called that shape
		// valid and the only lint it produced was "empty content in blockquote", which is
		// adjacent to the defect rather than on it.
		int32_t open_container_depth = -1;
		string open_container_type;
		int32_t open_container_order = 0;

		// A held "empty content" candidate, cancelled if the next element is deeper.
		string pending_empty_type;
		int32_t pending_empty_order = 0;
		int32_t pending_empty_depth = 0;

		// Indexed rather than a range-for: the 6.0 `plain` lint below has to look at the
		// element BEFORE this one and at the one after, because whether a text run is an
		// only child is not knowable from the run itself.
		for (idx_t j = 0; j < blocks_list.size(); j++) {
			auto &block = blocks_list[j];
			if (block.IsNull()) {
				continue;
			}

			auto element_type = GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX);
			auto content = GetElementStringField(block, BlockTypes::CONTENT_IDX);
			auto element_order = GetElementIntField(block, BlockTypes::ELEMENT_ORDER_IDX, 0);
			auto block_kind = GetElementStringField(block, BlockTypes::KIND_IDX);

			if (!pending_empty_type.empty()) {
				auto &cur_children = StructValue::GetChildren(block);
				const bool cur_null =
				    BlockTypes::LEVEL_IDX >= cur_children.size() || cur_children[BlockTypes::LEVEL_IDX].IsNull();
				const int32_t cur_depth = cur_null ? 1 : GetElementIntField(block, BlockTypes::LEVEL_IDX, 1);
				// Suppress when the structural warning below covers the same element:
				// "empty" and "owns no children" are one fact, and reporting it twice is
				// the noise that camouflages signal.
				const bool covered_by_structural =
				    !open_container_type.empty() && open_container_order == pending_empty_order;
				if (cur_depth <= pending_empty_depth && !covered_by_structural) {
					child_list_t<Value> warning_values;
					warning_values.push_back(make_pair("severity", Value("info")));
					warning_values.push_back(make_pair(
					    "message", Value("Empty " + pending_empty_type + " element: no content and no children")));
					warning_values.push_back(make_pair("element_order", Value(pending_empty_order)));
					warnings.push_back(Value::STRUCT(std::move(warning_values)));
				}
				pending_empty_type.clear();
			}

			if (block_kind == BlockTypes::KIND_BLOCK) {
				// NULL level means depth 1 -- a top-level element is not nested.
				auto &lvl_children = StructValue::GetChildren(block);
				const bool level_is_null =
				    BlockTypes::LEVEL_IDX >= lvl_children.size() || lvl_children[BlockTypes::LEVEL_IDX].IsNull();
				const int32_t depth = level_is_null ? 1 : GetElementIntField(block, BlockTypes::LEVEL_IDX, 1);

				if (open_container_depth >= 0 && depth <= open_container_depth) {
					child_list_t<Value> warning_values;
					warning_values.push_back(make_pair("severity", Value("warning")));
					// States the OBSERVATION, not an intent. Two elements at the same depth
					// are siblings, and an empty container beside a sibling is legal --
					// webbed established that while checking a defect I had wrongly
					// relayed as theirs. It is still worth a warning, because a producer
					// that meant the next block as content produces exactly this and the
					// result renders as text sitting where a reader expects it.
					warning_values.push_back(make_pair(
					    "message", Value(open_container_type + " at depth " + std::to_string(open_container_depth) +
					                     " owns no children: the next block is at depth " + std::to_string(depth) +
					                     ", so it is a SIBLING rather than content. Legal, but if that block was "
					                     "meant to be inside, it will render outside.")));
					warning_values.push_back(make_pair("element_order", Value(open_container_order)));
					warnings.push_back(Value::STRUCT(std::move(warning_values)));
				}

				// A container that CARRIES CONTENT legitimately owns no children -- that is
				// spec v1.0's rule, restored: content is populated iff the container has a
				// single text child, so `duck_block_list_item('a')` is complete on its own.
				// Only an EMPTY container followed by a sibling is worth flagging.
				//
				// Without the content test this rule fired on every conforming v1-shaped
				// container, because it was written while 2.0 said containers never carry
				// content. A lint that encodes a superseded rule reports correct data as
				// broken -- the same defect as the "level should be NULL on headings"
				// warning removed in 3.0, and the test suite caught it the same way.
				const bool is_container =
				    content.empty() &&
				    (element_type == BlockTypes::TYPE_DIV || element_type == BlockTypes::TYPE_SECTION ||
				     element_type == BlockTypes::TYPE_FIGURE || element_type == BlockTypes::TYPE_CAPTION ||
				     element_type == BlockTypes::TYPE_BLOCKQUOTE || element_type == BlockTypes::TYPE_LIST ||
				     element_type == BlockTypes::TYPE_LIST_ITEM);
				if (is_container) {
					open_container_depth = depth;
					open_container_type = element_type;
					open_container_order = element_order;
				} else {
					open_container_depth = -1;
				}
			}

			// Check for heading level skips
			if (element_type == BlockTypes::TYPE_HEADING) {
				auto heading_level_str = GetElementAttribute(block, "heading_level");
				int32_t heading_level = 1;
				if (!heading_level_str.empty()) {
					heading_level =
					    ParseInt32OrDefault(heading_level_str, GetElementIntField(block, BlockTypes::LEVEL_IDX, 1));
				} else {
					heading_level = GetElementIntField(block, BlockTypes::LEVEL_IDX, 1);
				}

				if (last_heading_level > 0 && heading_level > last_heading_level + 1) {
					child_list_t<Value> warning_values;
					warning_values.push_back(make_pair("severity", Value("warning")));
					warning_values.push_back(
					    make_pair("message", Value("Heading level skipped from h" + std::to_string(last_heading_level) +
					                               " to h" + std::to_string(heading_level))));
					warning_values.push_back(make_pair("element_order", Value(element_order)));
					warnings.push_back(Value::STRUCT(std::move(warning_values)));
				}
				last_heading_level = heading_level;

				// FIELD SEMANTICS. `level` is structural nesting depth; a heading's
				// h1-h6 belongs in attributes['heading_level']. The spec calls this
				// out by name because it is the oldest trap here -- and `level` has
				// since been read three different ways by three implementations,
				// every one of them using the documented field name. Names alone are
				// not a specification.
				//
				// A heading carrying BOTH is CORRECT and required, not a conflict. The
				// two numbers are independent: `level` is the heading's structural depth
				// in the document tree, attributes['heading_level'] is its semantic rank
				// (h1..h6). A top-level h2 is level 1 with heading_level 2.
				//
				// This used to WARN on exactly that, telling producers `level` "should be
				// NULL on headings". That rule encoded the NULL-at-top-level convention
				// which was never approved, and it flagged conforming data as suspect --
				// a linter arguing against the spec it exists to enforce. Removed rather
				// than inverted: there is nothing to warn about here.
			}

			// A heading with no heading_level carries NO rank information under 3.0,
			// because `level` is structural depth and never semantic. A consumer's only
			// remaining move is to guess. As of 2026-08-31 duckdb_markdown's writer
			// guessed by falling back to `level` -- a fair reading while the spec said
			// headings carried NULL there, and under 3.0 it renders a heading's DEPTH as
			// its RANK: a heading inside two containers becomes h3 whatever it is.
			//
			// Stated in the PAST TENSE and dated deliberately. It was reported by that
			// session rather than measured here, and they have been migrating since, so
			// writing it as current behaviour would put an unverified claim about
			// someone else's code into the reason for a lint that outlives both.
			//
			// Warning rather than error: the data is recoverable by defaulting to 1,
			// and both producers here always emit the attribute, so this only reaches
			// third-party blocks. But it is the attribute that carries the meaning, so
			// its absence is worth saying out loud rather than leaving each consumer to
			// invent a fallback.
			if (element_type == BlockTypes::TYPE_HEADING && GetElementAttribute(block, "heading_level").empty()) {
				child_list_t<Value> warning_values;
				warning_values.push_back(make_pair("severity", Value("warning")));
				warning_values.push_back(make_pair(
				    "message", Value("heading without attributes['heading_level']: its rank is unspecified. Do NOT "
				                     "fall back to `level`, which is structural depth, not rank.")));
				warning_values.push_back(make_pair("element_order", Value(element_order)));
				warnings.push_back(Value::STRUCT(std::move(warning_values)));
			}

			// `generic` exists to make an unmapped construct VISIBLE. Without
			// source_type it records that something was lost but not what, which
			// defeats the point of the backstop.
			if (element_type == BlockTypes::TYPE_GENERIC && GetElementAttribute(block, "source_type").empty()) {
				child_list_t<Value> warning_values;
				warning_values.push_back(make_pair("severity", Value("warning")));
				warning_values.push_back(
				    make_pair("message", Value("generic without attributes['source_type']: the gap is recorded but not "
				                               "identifiable")));
				warning_values.push_back(make_pair("element_order", Value(element_order)));
				warnings.push_back(Value::STRUCT(std::move(warning_values)));
			}

			// Empty content is only worth reporting when the element carries NOTHING --
			// no content AND no children. As written it fired on every conforming
			// container, because spec 2.0 says a container carries no content of its
			// own, and on every rich paragraph, whose text is in inline children. So
			// the rule reported correct code as suspicious.
			//
			// That is not merely noise. panduck measured a real defect whose only
			// nearby lint line was one of FOUR identical-looking "empty content" infos,
			// three of them benign -- it made the genuine problem look like the fourth
			// instance of a harmless pattern. A rule that fires on correct code does
			// not just add lines, it camouflages the lines that matter.
			//
			// Deferred rather than emitted: whether an element has children is only
			// knowable from what follows it, so the candidate is held and cancelled if
			// the next element is deeper.
			if (content.empty() && element_type != BlockTypes::TYPE_HR && element_type != BlockTypes::TYPE_IMAGE &&
			    element_type != BlockTypes::INLINE_IMAGE && element_type != BlockTypes::TYPE_RAW &&
			    element_type != BlockTypes::INLINE_SPACE && element_type != BlockTypes::INLINE_SOFTBREAK &&
			    element_type != BlockTypes::INLINE_LINEBREAK) {
				auto &pe_children = StructValue::GetChildren(block);
				const bool pe_null =
				    BlockTypes::LEVEL_IDX >= pe_children.size() || pe_children[BlockTypes::LEVEL_IDX].IsNull();
				pending_empty_type = element_type;
				pending_empty_order = element_order;
				pending_empty_depth = pe_null ? 1 : GetElementIntField(block, BlockTypes::LEVEL_IDX, 1);
			}

			// SPEC 6.0: a `plain` that is the ONLY child of a container whose own content
			// is empty. That text belongs in the container's `content` -- v1's content
			// rule, which says content is populated iff the container has a single text
			// child, and a lone `plain` IS one.
			//
			// Emitted as a lint rather than an error: the 5.0 shape still converts and
			// still renders, so data written against it is readable. But it is the shape
			// where a container with a single text child has TWO representations, which
			// is what every spec version since 2.0 has existed to remove -- and the four
			// extensions vendoring this vocabulary need something that OBJECTS rather
			// than a paragraph of prose in a spec document. The `level` drift got a year
			// of prose and no check, and that is exactly why it drifted.
			if (element_type == BlockTypes::TYPE_PLAIN && block_kind == BlockTypes::KIND_BLOCK && j > 0) {
				auto &parent = blocks_list[j - 1];
				const int32_t plain_level = GetElementIntField(block, BlockTypes::LEVEL_IDX, -1);
				if (!parent.IsNull() && GetElementStringField(parent, BlockTypes::KIND_IDX) == BlockTypes::KIND_BLOCK &&
				    GetElementStringField(parent, BlockTypes::CONTENT_IDX).empty() && plain_level > 1 &&
				    GetElementIntField(parent, BlockTypes::LEVEL_IDX, -1) == plain_level - 1) {
					// Only a child, or does it have a block sibling? A sibling is what
					// makes `plain` correct, so the lint must look before it fires.
					bool has_block_sibling = false;
					for (idx_t k = j + 1; k < blocks_list.size(); k++) {
						if (blocks_list[k].IsNull()) {
							continue;
						}
						const int32_t k_level = GetElementIntField(blocks_list[k], BlockTypes::LEVEL_IDX, -1);
						if (k_level > plain_level) {
							continue; // the plain's own inlines
						}
						if (GetElementStringField(blocks_list[k], BlockTypes::KIND_IDX) == BlockTypes::KIND_BLOCK) {
							has_block_sibling = (k_level == plain_level);
						}
						break;
					}
					if (!has_block_sibling) {
						child_list_t<Value> warning_values;
						warning_values.push_back(make_pair("severity", Value("warning")));
						warning_values.push_back(make_pair(
						    "message", Value("`plain` is the only child of `" +
						                     GetElementStringField(parent, BlockTypes::ELEMENT_TYPE_IDX) +
						                     "`; its text belongs in that element's `content` (spec 6.0). `plain` is "
						                     "for a text run that has nowhere else to live -- beside block siblings, "
						                     "or at the top level.")));
						warning_values.push_back(make_pair("element_order", Value(element_order)));
						warnings.push_back(Value::STRUCT(std::move(warning_values)));
					}
				}
			}

			// Check for code blocks without language
			if (element_type == BlockTypes::TYPE_CODE || element_type == BlockTypes::INLINE_CODE) {
				auto language = GetElementAttribute(block, "language");
				if (language.empty() && element_type == BlockTypes::TYPE_CODE) {
					child_list_t<Value> warning_values;
					warning_values.push_back(make_pair("severity", Value("info")));
					warning_values.push_back(make_pair("message", Value("Code block without language specified")));
					warning_values.push_back(make_pair("element_order", Value(element_order)));
					warnings.push_back(Value::STRUCT(std::move(warning_values)));
				}
			}

			// Check for large gaps in element_order
			if (last_order >= 0 && element_order > last_order + 10) {
				child_list_t<Value> warning_values;
				warning_values.push_back(make_pair("severity", Value("info")));
				warning_values.push_back(
				    make_pair("message", Value("Large gap in element_order (from " + std::to_string(last_order) +
				                               " to " + std::to_string(element_order) + ")")));
				warning_values.push_back(make_pair("element_order", Value(element_order)));
				warnings.push_back(Value::STRUCT(std::move(warning_values)));
			}
			last_order = element_order;
		}

		// A candidate held when the list ended has nothing after it, so nothing deeper.
		if (!pending_empty_type.empty()) {
			child_list_t<Value> warning_values;
			warning_values.push_back(make_pair("severity", Value("info")));
			warning_values.push_back(
			    make_pair("message", Value("Empty " + pending_empty_type + " element: no content and no children")));
			warning_values.push_back(make_pair("element_order", Value(pending_empty_order)));
			warnings.push_back(Value::STRUCT(std::move(warning_values)));
		}

		result.SetValue(i, Value::LIST(warning_struct_type, std::move(warnings)));
	}
}

void ValidationFunctions::DbBlocksStructureFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		int32_t block_count = 0;
		int32_t inline_count = 0;
		int32_t heading_count = 0;
		int32_t paragraph_count = 0;
		int32_t code_block_count = 0;
		int32_t list_count = 0;
		int32_t link_count = 0;
		int32_t image_count = 0;
		int32_t max_heading_level = 0;
		int32_t min_heading_level = 7;
		int64_t total_content_length = 0;

		// Handle NULL input
		if (!blocks_val.IsNull()) {
			auto &blocks_list = ListValue::GetChildren(blocks_val);

			for (auto &block : blocks_list) {
				if (block.IsNull()) {
					continue;
				}

				auto kind = GetElementStringField(block, BlockTypes::KIND_IDX);
				auto element_type = GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX);
				auto content = GetElementStringField(block, BlockTypes::CONTENT_IDX);

				total_content_length += static_cast<int64_t>(content.length());

				if (kind == BlockTypes::KIND_BLOCK) {
					block_count++;
				} else if (kind == BlockTypes::KIND_INLINE) {
					inline_count++;
				}

				if (element_type == BlockTypes::TYPE_HEADING) {
					heading_count++;
					auto heading_level_str = GetElementAttribute(block, "heading_level");
					int32_t heading_level = 1;
					if (!heading_level_str.empty()) {
						heading_level =
						    ParseInt32OrDefault(heading_level_str, GetElementIntField(block, BlockTypes::LEVEL_IDX, 1));
					} else {
						heading_level = GetElementIntField(block, BlockTypes::LEVEL_IDX, 1);
					}
					if (heading_level > max_heading_level)
						max_heading_level = heading_level;
					if (heading_level < min_heading_level)
						min_heading_level = heading_level;
				} else if (element_type == BlockTypes::TYPE_PARAGRAPH || element_type == BlockTypes::TYPE_PLAIN) {
					paragraph_count++;
				} else if (element_type == BlockTypes::TYPE_CODE) {
					code_block_count++;
				} else if (element_type == BlockTypes::TYPE_LIST) {
					list_count++;
				} else if (element_type == BlockTypes::INLINE_LINK) {
					link_count++;
				} else if (element_type == BlockTypes::TYPE_IMAGE || element_type == BlockTypes::INLINE_IMAGE) {
					image_count++;
				}
			}
		}

		// If no headings found, set min to 0
		if (min_heading_level == 7) {
			min_heading_level = 0;
		}

		child_list_t<Value> result_values;
		result_values.push_back(make_pair("block_count", Value(block_count)));
		result_values.push_back(make_pair("inline_count", Value(inline_count)));
		result_values.push_back(make_pair("heading_count", Value(heading_count)));
		result_values.push_back(make_pair("paragraph_count", Value(paragraph_count)));
		result_values.push_back(make_pair("code_block_count", Value(code_block_count)));
		result_values.push_back(make_pair("list_count", Value(list_count)));
		result_values.push_back(make_pair("link_count", Value(link_count)));
		result_values.push_back(make_pair("image_count", Value(image_count)));
		result_values.push_back(make_pair("max_heading_level", Value(max_heading_level)));
		result_values.push_back(make_pair("min_heading_level", Value(min_heading_level)));
		result_values.push_back(make_pair("total_content_length", Value(total_content_length)));

		result.SetValue(i, Value::STRUCT(std::move(result_values)));
	}
}

void ValidationFunctions::Register(ExtensionLoader &loader) {
	auto duck_block_list_type = BlockTypes::DuckBlockListType();

	// Define error struct type for validation
	child_list_t<LogicalType> error_struct_children;
	error_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	error_struct_children.push_back(make_pair("field", LogicalType::VARCHAR));
	error_struct_children.push_back(make_pair("message", LogicalType::VARCHAR));
	auto error_list_type = LogicalType::LIST(LogicalType::STRUCT(std::move(error_struct_children)));

	// Define result struct type for validation
	child_list_t<LogicalType> validate_result_children;
	validate_result_children.push_back(make_pair("valid", LogicalType::BOOLEAN));
	validate_result_children.push_back(make_pair("errors", error_list_type));
	auto validate_result_type = LogicalType::STRUCT(std::move(validate_result_children));

	// duck_blocks_validate(blocks LIST(duck_block)) -> STRUCT(valid, errors)
	auto validate_func =
	    ScalarFunction("duck_blocks_validate", {duck_block_list_type}, validate_result_type, DbBlocksValidateFun);
	loader.RegisterFunction(validate_func);

	// Define warning struct type for lint
	child_list_t<LogicalType> warning_struct_children;
	warning_struct_children.push_back(make_pair("severity", LogicalType::VARCHAR));
	warning_struct_children.push_back(make_pair("message", LogicalType::VARCHAR));
	warning_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	auto warning_list_type = LogicalType::LIST(LogicalType::STRUCT(std::move(warning_struct_children)));

	// duck_blocks_lint(blocks LIST(duck_block)) -> LIST(STRUCT)
	auto lint_func = ScalarFunction("duck_blocks_lint", {duck_block_list_type}, warning_list_type, DbBlocksLintFun);
	loader.RegisterFunction(lint_func);

	// Define structure result type
	child_list_t<LogicalType> structure_result_children;
	structure_result_children.push_back(make_pair("block_count", LogicalType::INTEGER));
	structure_result_children.push_back(make_pair("inline_count", LogicalType::INTEGER));
	structure_result_children.push_back(make_pair("heading_count", LogicalType::INTEGER));
	structure_result_children.push_back(make_pair("paragraph_count", LogicalType::INTEGER));
	structure_result_children.push_back(make_pair("code_block_count", LogicalType::INTEGER));
	structure_result_children.push_back(make_pair("list_count", LogicalType::INTEGER));
	structure_result_children.push_back(make_pair("link_count", LogicalType::INTEGER));
	structure_result_children.push_back(make_pair("image_count", LogicalType::INTEGER));
	structure_result_children.push_back(make_pair("max_heading_level", LogicalType::INTEGER));
	structure_result_children.push_back(make_pair("min_heading_level", LogicalType::INTEGER));
	structure_result_children.push_back(make_pair("total_content_length", LogicalType::BIGINT));
	auto structure_result_type = LogicalType::STRUCT(std::move(structure_result_children));

	// duck_blocks_structure(blocks LIST(duck_block)) -> STRUCT
	auto structure_func =
	    ScalarFunction("duck_blocks_structure", {duck_block_list_type}, structure_result_type, DbBlocksStructureFun);
	loader.RegisterFunction(structure_func);
}

} // namespace duckdb
