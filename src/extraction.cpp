#include "extraction.hpp"
#include "block_types.hpp"
#include "pandoc_convert_util.hpp"
#include "duckdb/common/types/value.hpp"

#include <map>
#include <functional>

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

	// MAP is stored as LIST of STRUCT(key, value) in DuckDB
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

// ---------------------------------------------------------------------------
// Structured inline children (issue #20)
//
// Rich text is not stored in a block's `content`; it is the run of kind='inline'
// elements that immediately follows the block in the flat list.
//
// This said "producers DISAGREE about which representation they use", named
// duckdb_markdown and webbed, and described what each emits. That was written
// before the content rule was canonical, and by spec 6.1 it is wrong in the way
// that matters: the rule is ONE shape -- `content` iff the block has a single
// text child, inline children otherwise -- so a producer picking the other one is
// non-conforming rather than exercising a choice. A reader of the old comment
// would conclude the vocabulary is ambiguous here, which is the thing 2.0 through
// 6.1 exist to remove.
//
// Handling both stays correct and stays here: 1.x data exists, and a consumer that
// only reads `content` loses every heading with a <code> in it. But that is
// DEFENSIVE, not a description of what any producer currently does -- a claim I do
// not measure and should not assert on their behalf. The ANSI renderer's
// RenderInlineRun takes the same posture.
//
// This is the plain-text counterpart of that traversal: consume the run of
// inline elements starting at `i` (advancing `i` past them) and concatenate
// their literal text. A container inline (bold, link, span, ...) carries empty
// content and its deeper-level children supply the text, so plain concatenation
// in document order reconstructs the run without needing the level stack that
// styled rendering requires.
// ---------------------------------------------------------------------------
static string CollectInlineRunText(const vector<Value> &list, idx_t &i) {
	string text;
	while (i < list.size()) {
		auto &el = list[i];
		if (el.IsNull()) {
			i++;
			continue;
		}
		if (GetElementStringField(el, BlockTypes::KIND_IDX) != BlockTypes::KIND_INLINE) {
			break;
		}

		auto element_type = GetElementStringField(el, BlockTypes::ELEMENT_TYPE_IDX);
		auto content = GetElementStringField(el, BlockTypes::CONTENT_IDX);

		if (element_type == BlockTypes::INLINE_SPACE || element_type == BlockTypes::INLINE_SOFTBREAK) {
			// Whitespace inlines may carry either " " (pandoc) or "" (builders)
			text += " ";
		} else if (element_type == BlockTypes::INLINE_LINEBREAK) {
			text += "\n";
		} else if (element_type == BlockTypes::INLINE_IMAGE) {
			// An image contributes its alt text, if any
			text += content.empty() ? GetElementAttribute(el, "alt") : content;
		} else {
			text += content;
		}
		i++;
	}
	return text;
}

// Text of a single block: its literal `content` when populated, otherwise the
// text of its structured inline children. `i` points at the block and is left
// pointing past the block and any inline run it owns.
static string BlockText(const vector<Value> &list, idx_t &i) {
	auto &block = list[i];
	auto content = GetElementStringField(block, BlockTypes::CONTENT_IDX);
	i++;
	auto inline_text = CollectInlineRunText(list, i);
	return content.empty() ? inline_text : content;
}

// Cell text of the native table schema {"headers":[...],"rows":[[...]]}.
//
// Without this a table's text was its raw JSON: searching a document for a word
// in a cell failed, while searching for "headers" or "rows" matched every table.
// Same defect the Pandoc tuple caused before 5.0 -- structure serialised into a
// field a text extractor reads verbatim -- and it does not go away just because
// the schema got smaller.
static string TableJsonToText(const string &json, const string &separator) {
	string out;
	bool in_string = false, escaped = false, is_key = false;
	string current;
	// The schema is exactly two keys, both mapping to arrays of strings, so every
	// string that is not one of the two key names is cell text.
	for (size_t i = 0; i < json.size(); i++) {
		char c = json[i];
		if (escaped) {
			current += c;
			escaped = false;
			continue;
		}
		if (in_string && c == '\\') {
			escaped = true;
			continue;
		}
		if (c == '"') {
			if (in_string) {
				in_string = false;
				// A key is a string immediately followed by ':'.
				size_t k = i + 1;
				while (k < json.size() && (json[k] == ' ' || json[k] == '\t')) {
					k++;
				}
				is_key = (k < json.size() && json[k] == ':');
				if (!is_key && !current.empty()) {
					if (!out.empty()) {
						out += separator;
					}
					out += current;
				}
				current.clear();
			} else {
				in_string = true;
				current.clear();
			}
			continue;
		}
		if (in_string) {
			current += c;
		}
	}
	return out;
}

static string BlocksToText(const vector<Value> &blocks_list, const string &separator) {
	string text_content;
	bool first = true;

	idx_t i = 0;
	while (i < blocks_list.size()) {
		auto &block = blocks_list[i];
		if (block.IsNull()) {
			i++;
			continue;
		}

		auto kind = GetElementStringField(block, BlockTypes::KIND_IDX);
		if (kind == BlockTypes::KIND_INLINE) {
			// A leading run of inlines with no parent block (e.g. the output of
			// an inline builder used on its own) is still one unit of text
			auto stray = CollectInlineRunText(blocks_list, i);
			if (!stray.empty()) {
				if (!first) {
					text_content += separator;
				}
				first = false;
				text_content += stray;
			}
			continue;
		}

		// Anything that is not document content is not body text. This is an
		// ALLOWLIST on purpose: the previous form skipped `inline` and treated
		// everything else as a block, so kind='value' fell through and a
		// document's own metadata was appended to its extracted text.
		//
		// The value element's CHILDREN have to go with it. MetaInlines carries
		// kind='inline' children, which would otherwise be picked up above as a
		// stray inline run -- so skipping the marker alone still leaked the title.
		if (kind != BlockTypes::KIND_BLOCK) {
			// LEVEL-SCOPED skip: consume this element and everything nested deeper
			// than it. The container rule from the spec -- children follow at
			// level+1, the container ends at the first element back at its own
			// level -- and it is shape-independent.
			//
			// The previous version resumed at the next block-or-value, which works
			// for MetaInlines/List/Map (inline children) and FAILS for MetaBlocks,
			// whose children ARE blocks: an abstract's paragraph was picked up as
			// body text. A NULL level reads as 1, the depth of a top-level element.
			const int32_t scope_level = GetElementIntField(block, BlockTypes::LEVEL_IDX, 1);
			i++;
			while (i < blocks_list.size()) {
				auto &child = blocks_list[i];
				if (child.IsNull()) {
					i++;
					continue;
				}
				if (GetElementIntField(child, BlockTypes::LEVEL_IDX, 1) <= scope_level) {
					break; // back out to the containing level
				}
				i++;
			}
			continue;
		}

		auto element_type = GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX);

		// Skip blocks that don't have meaningful text content. Their inline
		// children (if any) are consumed and discarded along with them.
		auto text = BlockText(blocks_list, i);
		if (element_type == BlockTypes::TYPE_HR || element_type == BlockTypes::TYPE_RAW) {
			continue;
		}

		// A table's text is its CELLS, not its serialisation. Emitting the JSON made
		// cell words unfindable and made "headers"/"rows" match every table.
		if (element_type == BlockTypes::TYPE_TABLE &&
		    GetElementStringField(block, BlockTypes::ENCODING_IDX) == BlockTypes::ENCODING_JSON) {
			text = TableJsonToText(text, separator);
		}

		// Skip empty content
		if (text.empty()) {
			continue;
		}

		if (!first) {
			text_content += separator;
		}
		first = false;
		text_content += text;
	}

	return text_content;
}

void ExtractionFunctions::DbBlocksToTextFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto &separator_vec = args.data[1];

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);
		auto separator_val = separator_vec.GetValue(i);

		// Handle NULL input
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}

		string separator = separator_val.IsNull() ? "\n\n" : separator_val.GetValue<string>();
		auto &blocks_list = ListValue::GetChildren(blocks_val);

		result.SetValue(i, Value(BlocksToText(blocks_list, separator)));
	}
}

// duck_blocks_to_text(blocks) -- same as above with the default "\n\n" separator
static void DbBlocksToTextDefaultSeparatorFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		if (blocks_val.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);
		result.SetValue(i, Value(BlocksToText(blocks_list, "\n\n")));
	}
}

void ExtractionFunctions::DbBlocksHeadingsFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];

	// Define the return type for headings
	child_list_t<LogicalType> heading_struct_children;
	heading_struct_children.push_back(make_pair("level", LogicalType::INTEGER));
	heading_struct_children.push_back(make_pair("title", LogicalType::VARCHAR));
	heading_struct_children.push_back(make_pair("id", LogicalType::VARCHAR));
	heading_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	auto heading_struct_type = LogicalType::STRUCT(std::move(heading_struct_children));

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		// Handle NULL input
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value::LIST(heading_struct_type, vector<Value>()));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);
		vector<Value> headings;

		int32_t value_scope = -1; // depth of the kind='value' element we are inside, or -1
		idx_t bi = 0;
		while (bi < blocks_list.size()) {
			auto &block = blocks_list[bi];
			if (block.IsNull()) {
				bi++;
				continue;
			}

			// Skip anything inside a kind='value' scope. These loops match on
			// element_type alone, so a Header buried in MetaBlocks metadata --
			// an abstract with its own heading -- was appearing in the document's
			// table of contents. Level-scoped, like the text extractor: a value
			// at level L contains everything deeper than L.
			if (GetElementStringField(block, BlockTypes::KIND_IDX) == BlockTypes::KIND_VALUE) {
				value_scope = GetElementIntField(block, BlockTypes::LEVEL_IDX, 1);
				bi++;
				continue;
			}
			if (value_scope >= 0) {
				if (GetElementIntField(block, BlockTypes::LEVEL_IDX, 1) > value_scope) {
					bi++;
					continue;
				}
				value_scope = -1; // back out to document content
			}

			auto element_type = GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX);

			if (element_type != BlockTypes::TYPE_HEADING) {
				bi++;
				continue;
			}

			// Get heading_level from attributes, falling back to level field for backward compatibility
			auto heading_level_str = GetElementAttribute(block, BlockTypes::ATTR_HEADING_LEVEL);
			int32_t level;
			if (!heading_level_str.empty()) {
				// Safe parse: malformed/out-of-range values fall back instead of throwing
				level = ParseInt32OrDefault(heading_level_str, GetElementIntField(block, BlockTypes::LEVEL_IDX, 1));
			} else {
				// Fall back to level field (for backward compatibility)
				level = GetElementIntField(block, BlockTypes::LEVEL_IDX, 1);
			}
			auto id = GetElementAttribute(block, "id");
			auto element_order = GetElementIntField(block, BlockTypes::ELEMENT_ORDER_IDX, 0);
			// Title is the literal content when populated, otherwise the text of
			// the heading's structured inline children (issue #20). Advances `bi`
			// past the heading and the inline run it owns.
			auto title = BlockText(blocks_list, bi);

			child_list_t<Value> heading_values;
			heading_values.push_back(make_pair("level", Value(level)));
			heading_values.push_back(make_pair("title", Value(title)));
			heading_values.push_back(make_pair("id", Value(id)));
			heading_values.push_back(make_pair("element_order", Value(element_order)));

			headings.push_back(Value::STRUCT(std::move(heading_values)));
		}

		result.SetValue(i, Value::LIST(heading_struct_type, std::move(headings)));
	}
}

void ExtractionFunctions::DbBlocksCodeBlocksFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];

	// Define the return type for code blocks
	child_list_t<LogicalType> code_struct_children;
	code_struct_children.push_back(make_pair("language", LogicalType::VARCHAR));
	code_struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
	code_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	auto code_struct_type = LogicalType::STRUCT(std::move(code_struct_children));

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		// Handle NULL input
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value::LIST(code_struct_type, vector<Value>()));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);
		vector<Value> code_blocks;

		for (auto &block : blocks_list) {
			if (block.IsNull()) {
				continue;
			}

			auto element_type = GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX);

			if (element_type != BlockTypes::TYPE_CODE) {
				continue;
			}

			auto language = GetElementAttribute(block, "language");
			auto content = GetElementStringField(block, BlockTypes::CONTENT_IDX);
			auto element_order = GetElementIntField(block, BlockTypes::ELEMENT_ORDER_IDX, 0);

			child_list_t<Value> code_values;
			code_values.push_back(make_pair("language", Value(language)));
			code_values.push_back(make_pair("content", Value(content)));
			code_values.push_back(make_pair("element_order", Value(element_order)));

			code_blocks.push_back(Value::STRUCT(std::move(code_values)));
		}

		result.SetValue(i, Value::LIST(code_struct_type, std::move(code_blocks)));
	}
}

void ExtractionFunctions::DbBlocksTocFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];

	// Define the return type for TOC entries
	child_list_t<LogicalType> toc_struct_children;
	toc_struct_children.push_back(make_pair("level", LogicalType::INTEGER));
	toc_struct_children.push_back(make_pair("title", LogicalType::VARCHAR));
	toc_struct_children.push_back(make_pair("id", LogicalType::VARCHAR));
	toc_struct_children.push_back(make_pair(BlockTypes::ATTR_INDENT, LogicalType::INTEGER));
	toc_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	auto toc_struct_type = LogicalType::STRUCT(std::move(toc_struct_children));

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		// Handle NULL input
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value::LIST(toc_struct_type, vector<Value>()));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);
		vector<Value> toc_entries;

		// Track minimum heading level for calculating indent
		int32_t min_level = 7; // Higher than max heading level (6)

		// First pass: find headings and minimum level
		vector<std::tuple<int32_t, string, string, int32_t>> headings; // level, title, id, element_order

		int32_t value_scope = -1; // depth of the kind='value' element we are inside, or -1
		idx_t bi = 0;
		while (bi < blocks_list.size()) {
			auto &block = blocks_list[bi];
			if (block.IsNull()) {
				bi++;
				continue;
			}

			// Skip anything inside a kind='value' scope. These loops match on
			// element_type alone, so a Header buried in MetaBlocks metadata --
			// an abstract with its own heading -- was appearing in the document's
			// table of contents. Level-scoped, like the text extractor: a value
			// at level L contains everything deeper than L.
			if (GetElementStringField(block, BlockTypes::KIND_IDX) == BlockTypes::KIND_VALUE) {
				value_scope = GetElementIntField(block, BlockTypes::LEVEL_IDX, 1);
				bi++;
				continue;
			}
			if (value_scope >= 0) {
				if (GetElementIntField(block, BlockTypes::LEVEL_IDX, 1) > value_scope) {
					bi++;
					continue;
				}
				value_scope = -1; // back out to document content
			}

			auto element_type = GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX);

			if (element_type != BlockTypes::TYPE_HEADING) {
				bi++;
				continue;
			}

			// Get heading_level from attributes, falling back to level field
			auto heading_level_str = GetElementAttribute(block, BlockTypes::ATTR_HEADING_LEVEL);
			int32_t level;
			if (!heading_level_str.empty()) {
				// Safe parse: malformed/out-of-range values fall back instead of throwing
				level = ParseInt32OrDefault(heading_level_str, GetElementIntField(block, BlockTypes::LEVEL_IDX, 1));
			} else {
				level = GetElementIntField(block, BlockTypes::LEVEL_IDX, 1);
			}
			auto id = GetElementAttribute(block, "id");
			auto element_order = GetElementIntField(block, BlockTypes::ELEMENT_ORDER_IDX, 0);
			// Same content-or-inline-children rule as duck_blocks_headings (issue #20)
			auto title = BlockText(blocks_list, bi);

			if (level < min_level) {
				min_level = level;
			}

			headings.push_back(std::make_tuple(level, title, id, element_order));
		}

		// Second pass: create TOC entries with calculated indent
		for (auto &heading : headings) {
			int32_t level = std::get<0>(heading);
			auto &title = std::get<1>(heading);
			auto &id = std::get<2>(heading);
			int32_t element_order = std::get<3>(heading);
			int32_t indent = level - min_level; // 0-based indent relative to minimum level

			child_list_t<Value> toc_values;
			toc_values.push_back(make_pair("level", Value(level)));
			toc_values.push_back(make_pair("title", Value(title)));
			toc_values.push_back(make_pair("id", Value(id)));
			toc_values.push_back(make_pair(BlockTypes::ATTR_INDENT, Value(indent)));
			toc_values.push_back(make_pair("element_order", Value(element_order)));

			toc_entries.push_back(Value::STRUCT(std::move(toc_values)));
		}

		result.SetValue(i, Value::LIST(toc_struct_type, std::move(toc_entries)));
	}
}

void ExtractionFunctions::DbBlocksLinksFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];

	// Define the return type for links
	child_list_t<LogicalType> link_struct_children;
	link_struct_children.push_back(make_pair("href", LogicalType::VARCHAR));
	link_struct_children.push_back(make_pair("text", LogicalType::VARCHAR));
	link_struct_children.push_back(make_pair("title", LogicalType::VARCHAR));
	link_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	auto link_struct_type = LogicalType::STRUCT(std::move(link_struct_children));

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		// Handle NULL input
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value::LIST(link_struct_type, vector<Value>()));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);
		vector<Value> links;

		for (auto &block : blocks_list) {
			if (block.IsNull()) {
				continue;
			}

			auto element_type = GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX);
			auto element_order = GetElementIntField(block, BlockTypes::ELEMENT_ORDER_IDX, 0);

			// Check for link inline elements
			if (element_type == BlockTypes::INLINE_LINK) {
				auto href = GetElementAttribute(block, "href");
				auto text = GetElementStringField(block, BlockTypes::CONTENT_IDX);
				auto title = GetElementAttribute(block, "title");

				child_list_t<Value> link_values;
				link_values.push_back(make_pair("href", Value(href)));
				link_values.push_back(make_pair("text", Value(text)));
				link_values.push_back(make_pair("title", Value(title)));
				link_values.push_back(make_pair("element_order", Value(element_order)));

				links.push_back(Value::STRUCT(std::move(link_values)));
			}

			// Check for image blocks (which have URLs)
			if (element_type == BlockTypes::TYPE_IMAGE || element_type == BlockTypes::INLINE_IMAGE) {
				auto src = GetElementAttribute(block, "src");
				auto alt = GetElementStringField(block, BlockTypes::CONTENT_IDX);
				auto title = GetElementAttribute(block, "title");

				// Only include if there's a src URL
				if (!src.empty()) {
					child_list_t<Value> link_values;
					link_values.push_back(make_pair("href", Value(src)));
					link_values.push_back(make_pair("text", Value(alt)));
					link_values.push_back(make_pair("title", Value(title)));
					link_values.push_back(make_pair("element_order", Value(element_order)));

					links.push_back(Value::STRUCT(std::move(link_values)));
				}
			}
		}

		result.SetValue(i, Value::LIST(link_struct_type, std::move(links)));
	}
}

void ExtractionFunctions::DbBlocksStatsFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];

	// Define the return type for stats
	child_list_t<LogicalType> stats_struct_children;
	stats_struct_children.push_back(make_pair("element_type", LogicalType::VARCHAR));
	stats_struct_children.push_back(make_pair("count", LogicalType::INTEGER));
	stats_struct_children.push_back(make_pair("total_content_length", LogicalType::BIGINT));
	stats_struct_children.push_back(make_pair("avg_content_length", LogicalType::DOUBLE));
	auto stats_struct_type = LogicalType::STRUCT(std::move(stats_struct_children));

	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		// Handle NULL input
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value::LIST(stats_struct_type, vector<Value>()));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);

		// Accumulate stats by element type
		std::map<string, std::pair<int32_t, int64_t>> type_stats; // type -> (count, total_length)

		for (auto &block : blocks_list) {
			if (block.IsNull()) {
				continue;
			}

			auto element_type = GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX);
			auto content = GetElementStringField(block, BlockTypes::CONTENT_IDX);

			auto &stats = type_stats[element_type];
			stats.first++;                                          // count
			stats.second += static_cast<int64_t>(content.length()); // total length
		}

		// Convert to result list
		vector<Value> stats_list;
		for (auto &entry : type_stats) {
			auto &type_name = entry.first;
			auto count_val = entry.second.first;
			auto total_length = entry.second.second;
			double avg_length = count_val > 0 ? static_cast<double>(total_length) / count_val : 0.0;

			child_list_t<Value> stat_values;
			stat_values.push_back(make_pair("element_type", Value(type_name)));
			stat_values.push_back(make_pair("count", Value(count_val)));
			stat_values.push_back(make_pair("total_content_length", Value(total_length)));
			stat_values.push_back(make_pair("avg_content_length", Value(avg_length)));

			stats_list.push_back(Value::STRUCT(std::move(stat_values)));
		}

		result.SetValue(i, Value::LIST(stats_struct_type, std::move(stats_list)));
	}
}

// ---------------------------------------------------------------------------
// Spec 6.5: blocks-returning forms of the four projections.
//
// The projections (now `_structs`) stay byte-for-byte what they were; these build
// duck_blocks. element_order is carried through from the source UNRENUMBERED --
// duckeye computes section spans from heading element_order and slices with them,
// so a renumbering here would not fail to bind, it would return the wrong span.
// ---------------------------------------------------------------------------
struct HeadingInfo {
	int32_t level;
	string title;
	string id;
	int32_t order;
	idx_t index;
};

// Same walk as DbBlocksHeadingsFun / DbBlocksTocFun: skips kind='value' scopes,
// reads heading_level with the level-field fallback, flattens the title.
static vector<HeadingInfo> CollectHeadings(const vector<Value> &blocks_list) {
	vector<HeadingInfo> out;
	int32_t value_scope = -1;
	idx_t bi = 0;
	while (bi < blocks_list.size()) {
		auto &block = blocks_list[bi];
		if (block.IsNull()) {
			bi++;
			continue;
		}
		if (GetElementStringField(block, BlockTypes::KIND_IDX) == BlockTypes::KIND_VALUE) {
			value_scope = GetElementIntField(block, BlockTypes::LEVEL_IDX, 1);
			bi++;
			continue;
		}
		if (value_scope >= 0) {
			if (GetElementIntField(block, BlockTypes::LEVEL_IDX, 1) > value_scope) {
				bi++;
				continue;
			}
			value_scope = -1;
		}
		if (GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX) != BlockTypes::TYPE_HEADING) {
			bi++;
			continue;
		}
		HeadingInfo h;
		h.index = bi;
		auto heading_level_str = GetElementAttribute(block, BlockTypes::ATTR_HEADING_LEVEL);
		auto fallback = GetElementIntField(block, BlockTypes::LEVEL_IDX, 1);
		h.level = heading_level_str.empty() ? fallback : ParseInt32OrDefault(heading_level_str, fallback);
		h.id = GetElementAttribute(block, "id");
		h.order = GetElementIntField(block, BlockTypes::ELEMENT_ORDER_IDX, 0);
		h.title = BlockText(blocks_list, bi); // advances bi past the inline run
		out.push_back(std::move(h));
	}
	return out;
}

// Outline positions are positions in the OUTLINE, not the heading's own digit:
// h1, h3, h2 reads 1, 1.1, 1.2. Never NULL, never padded. A stack of (level,
// counter): pop deeper entries remembering the last popped counter; a sibling
// increments; anything else pushes at popped+1 so a shallower-but-not-sibling
// heading continues its parent's numbering.
static vector<string> ComputeOutlines(const vector<HeadingInfo> &headings) {
	vector<string> out;
	vector<std::pair<int32_t, int32_t>> stack;
	for (auto &h : headings) {
		int32_t popped = 0;
		while (!stack.empty() && stack.back().first > h.level) {
			popped = stack.back().second;
			stack.pop_back();
		}
		if (!stack.empty() && stack.back().first == h.level) {
			stack.back().second++;
		} else {
			stack.emplace_back(h.level, popped + 1);
		}
		string s;
		for (size_t k = 0; k < stack.size(); k++) {
			if (k) {
				s += '.';
			}
			s += std::to_string(stack[k].second);
		}
		out.push_back(std::move(s));
	}
	return out;
}

static vector<std::pair<string, string>> AttributePairs(const Value &element) {
	vector<std::pair<string, string>> out;
	auto &children = StructValue::GetChildren(element);
	auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
	if (attrs.IsNull()) {
		return out;
	}
	for (auto &entry : MapValue::GetChildren(attrs)) {
		auto &kv = StructValue::GetChildren(entry);
		if (kv[0].IsNull()) {
			continue;
		}
		out.emplace_back(kv[0].GetValue<string>(), kv[1].IsNull() ? "" : kv[1].GetValue<string>());
	}
	return out;
}

static void SetAttribute(vector<std::pair<string, string>> &attrs, const string &key, const string &value) {
	for (auto &p : attrs) {
		if (p.first == key) {
			p.second = value;
			return;
		}
	}
	attrs.emplace_back(key, value);
}

static Value MapFromPairs(const vector<std::pair<string, string>> &attrs) {
	vector<Value> keys, values;
	for (auto &p : attrs) {
		keys.push_back(Value(p.first));
		values.push_back(Value(p.second));
	}
	return Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, std::move(keys), std::move(values));
}

static Value MakeBlock(const string &kind, const string &type, const string &content, int32_t level,
                       const string &encoding, const vector<std::pair<string, string>> &attrs, int32_t order) {
	child_list_t<Value> v;
	v.push_back(make_pair("kind", Value(kind)));
	v.push_back(make_pair("element_type", Value(type)));
	v.push_back(make_pair("content", Value(content)));
	v.push_back(make_pair("level", Value(level)));
	v.push_back(make_pair("encoding", Value(encoding)));
	v.push_back(make_pair("attributes", MapFromPairs(attrs)));
	v.push_back(make_pair("element_order", Value(order)));
	return Value::STRUCT(std::move(v));
}

// One block per heading: content is the flattened title (inline children are NOT
// carried -- that is the polish that makes the form usable standalone), attributes
// are the source's plus heading_level, outline and, for toc, indent.
static void HeadingBlocks(DataChunk &args, Vector &result, bool with_indent) {
	auto &blocks_vec = args.data[0];
	auto block_type = BlockTypes::DuckBlockType();
	for (idx_t i = 0; i < args.size(); i++) {
		auto blocks_val = blocks_vec.GetValue(i);
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value::LIST(block_type, vector<Value>()));
			continue;
		}
		auto &blocks_list = ListValue::GetChildren(blocks_val);
		auto headings = CollectHeadings(blocks_list);
		auto outlines = ComputeOutlines(headings);
		int32_t min_level = 7;
		for (auto &h : headings) {
			min_level = MinValue<int32_t>(min_level, h.level);
		}
		vector<Value> out;
		for (idx_t k = 0; k < headings.size(); k++) {
			auto &h = headings[k];
			auto &src = blocks_list[h.index];
			auto attrs = AttributePairs(src);
			SetAttribute(attrs, BlockTypes::ATTR_HEADING_LEVEL, std::to_string(h.level));
			SetAttribute(attrs, BlockTypes::ATTR_OUTLINE, outlines[k]);
			if (with_indent) {
				SetAttribute(attrs, BlockTypes::ATTR_INDENT, std::to_string(h.level - min_level));
			}
			auto encoding = GetElementStringField(src, BlockTypes::ENCODING_IDX);
			out.push_back(MakeBlock(BlockTypes::KIND_BLOCK, BlockTypes::TYPE_HEADING, h.title,
			                        GetElementIntField(src, BlockTypes::LEVEL_IDX, 1),
			                        encoding.empty() ? BlockTypes::ENCODING_TEXT : encoding, attrs, h.order));
		}
		result.SetValue(i, Value::LIST(block_type, std::move(out)));
	}
}

void ExtractionFunctions::DbBlocksHeadingsBlocksFun(DataChunk &args, ExpressionState &state, Vector &result) {
	HeadingBlocks(args, result, false);
}

void ExtractionFunctions::DbBlocksTocBlocksFun(DataChunk &args, ExpressionState &state, Vector &result) {
	HeadingBlocks(args, result, true);
}

// The source elements, as they are. Lossless by design: an image stays an image;
// the projection sibling is where href unifies href and src.
static void FilterBlocks(DataChunk &args, Vector &result, const std::function<bool(const Value &)> &keep) {
	auto &blocks_vec = args.data[0];
	auto block_type = BlockTypes::DuckBlockType();
	for (idx_t i = 0; i < args.size(); i++) {
		auto blocks_val = blocks_vec.GetValue(i);
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value::LIST(block_type, vector<Value>()));
			continue;
		}
		vector<Value> out;
		for (auto &block : ListValue::GetChildren(blocks_val)) {
			if (!block.IsNull() && keep(block)) {
				out.push_back(block);
			}
		}
		result.SetValue(i, Value::LIST(block_type, std::move(out)));
	}
}

void ExtractionFunctions::DbBlocksCodeBlocksBlocksFun(DataChunk &args, ExpressionState &state, Vector &result) {
	FilterBlocks(args, result, [](const Value &b) {
		return GetElementStringField(b, BlockTypes::KIND_IDX) == BlockTypes::KIND_BLOCK &&
		       GetElementStringField(b, BlockTypes::ELEMENT_TYPE_IDX) == BlockTypes::TYPE_CODE;
	});
}

void ExtractionFunctions::DbBlocksLinksBlocksFun(DataChunk &args, ExpressionState &state, Vector &result) {
	FilterBlocks(args, result, [](const Value &b) {
		auto type = GetElementStringField(b, BlockTypes::ELEMENT_TYPE_IDX);
		if (type == BlockTypes::INLINE_LINK) {
			return true;
		}
		return (type == BlockTypes::TYPE_IMAGE || type == BlockTypes::INLINE_IMAGE) &&
		       !GetElementAttribute(b, "src").empty();
	});
}

void ExtractionFunctions::Register(ExtensionLoader &loader) {
	auto duck_block_list_type = BlockTypes::DuckBlockListType();

	// duck_blocks_to_text(blocks LIST(duck_block), separator VARCHAR) -> VARCHAR
	auto to_text_func = ScalarFunction("duck_blocks_to_text", {duck_block_list_type, LogicalType::VARCHAR},
	                                   LogicalType::VARCHAR, DbBlocksToTextFun);
	loader.RegisterFunction(to_text_func);

	// Single-arg version with default separator
	auto to_text_func_simple = ScalarFunction("duck_blocks_to_text", {duck_block_list_type}, LogicalType::VARCHAR,
	                                          DbBlocksToTextDefaultSeparatorFun);
	loader.RegisterFunction(to_text_func_simple);

	// Define return types for headings
	child_list_t<LogicalType> heading_struct_children;
	heading_struct_children.push_back(make_pair("level", LogicalType::INTEGER));
	heading_struct_children.push_back(make_pair("title", LogicalType::VARCHAR));
	heading_struct_children.push_back(make_pair("id", LogicalType::VARCHAR));
	heading_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	auto heading_list_type = LogicalType::LIST(LogicalType::STRUCT(std::move(heading_struct_children)));

	// duck_blocks_headings(blocks LIST(duck_block)) -> LIST(STRUCT)
	auto headings_func =
	    ScalarFunction("duck_blocks_headings_structs", {duck_block_list_type}, heading_list_type, DbBlocksHeadingsFun);
	loader.RegisterFunction(headings_func);
	// 6.5: the base name returns blocks; the projection above lives on as _structs.
	loader.RegisterFunction(ScalarFunction("duck_blocks_headings", {duck_block_list_type}, duck_block_list_type,
	                                       ExtractionFunctions::DbBlocksHeadingsBlocksFun));

	// Define return types for code blocks
	child_list_t<LogicalType> code_struct_children;
	code_struct_children.push_back(make_pair("language", LogicalType::VARCHAR));
	code_struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
	code_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	auto code_list_type = LogicalType::LIST(LogicalType::STRUCT(std::move(code_struct_children)));

	// duck_blocks_code_blocks(blocks LIST(duck_block)) -> LIST(STRUCT)
	auto code_blocks_func = ScalarFunction("duck_blocks_code_blocks_structs", {duck_block_list_type}, code_list_type,
	                                       DbBlocksCodeBlocksFun);
	loader.RegisterFunction(code_blocks_func);
	// 6.5: the base name returns blocks; the projection above lives on as _structs.
	loader.RegisterFunction(ScalarFunction("duck_blocks_code_blocks", {duck_block_list_type}, duck_block_list_type,
	                                       ExtractionFunctions::DbBlocksCodeBlocksBlocksFun));

	// Define return types for stats
	child_list_t<LogicalType> stats_struct_children;
	stats_struct_children.push_back(make_pair("element_type", LogicalType::VARCHAR));
	stats_struct_children.push_back(make_pair("count", LogicalType::INTEGER));
	stats_struct_children.push_back(make_pair("total_content_length", LogicalType::BIGINT));
	stats_struct_children.push_back(make_pair("avg_content_length", LogicalType::DOUBLE));
	auto stats_list_type = LogicalType::LIST(LogicalType::STRUCT(std::move(stats_struct_children)));

	// duck_blocks_stats(blocks LIST(duck_block)) -> LIST(STRUCT)
	auto stats_func = ScalarFunction("duck_blocks_stats", {duck_block_list_type}, stats_list_type, DbBlocksStatsFun);
	loader.RegisterFunction(stats_func);

	// Define return types for TOC
	child_list_t<LogicalType> toc_struct_children;
	toc_struct_children.push_back(make_pair("level", LogicalType::INTEGER));
	toc_struct_children.push_back(make_pair("title", LogicalType::VARCHAR));
	toc_struct_children.push_back(make_pair("id", LogicalType::VARCHAR));
	toc_struct_children.push_back(make_pair(BlockTypes::ATTR_INDENT, LogicalType::INTEGER));
	toc_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	auto toc_list_type = LogicalType::LIST(LogicalType::STRUCT(std::move(toc_struct_children)));

	// duck_blocks_toc(blocks LIST(duck_block)) -> LIST(STRUCT)
	auto toc_func = ScalarFunction("duck_blocks_toc_structs", {duck_block_list_type}, toc_list_type, DbBlocksTocFun);
	loader.RegisterFunction(toc_func);
	// 6.5: the base name returns blocks; the projection above lives on as _structs.
	loader.RegisterFunction(ScalarFunction("duck_blocks_toc", {duck_block_list_type}, duck_block_list_type,
	                                       ExtractionFunctions::DbBlocksTocBlocksFun));

	// Define return types for links
	child_list_t<LogicalType> link_struct_children;
	link_struct_children.push_back(make_pair("href", LogicalType::VARCHAR));
	link_struct_children.push_back(make_pair("text", LogicalType::VARCHAR));
	link_struct_children.push_back(make_pair("title", LogicalType::VARCHAR));
	link_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	auto link_list_type = LogicalType::LIST(LogicalType::STRUCT(std::move(link_struct_children)));

	// duck_blocks_links(blocks LIST(duck_block)) -> LIST(STRUCT)
	auto links_func =
	    ScalarFunction("duck_blocks_links_structs", {duck_block_list_type}, link_list_type, DbBlocksLinksFun);
	loader.RegisterFunction(links_func);
	// 6.5: the base name returns blocks; the projection above lives on as _structs.
	loader.RegisterFunction(ScalarFunction("duck_blocks_links", {duck_block_list_type}, duck_block_list_type,
	                                       ExtractionFunctions::DbBlocksLinksBlocksFun));
}

} // namespace duckdb
