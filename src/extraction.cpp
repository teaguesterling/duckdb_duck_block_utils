#include "extraction.hpp"
#include "block_types.hpp"
#include "pandoc_convert_util.hpp"
#include "duckdb/common/types/value.hpp"

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
// elements that immediately follows the block in the flat list. Producers
// disagree about which representation they use: `markdown` flattens plain text
// into `content`, while `webbed` leaves `content` NULL and emits inline children
// for any heading containing <code>, <em>, <a>, ... Consumers therefore have to
// handle both, exactly as the ANSI renderer's RenderInlineRun already does.
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

		auto element_type = GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX);

		// Skip blocks that don't have meaningful text content. Their inline
		// children (if any) are consumed and discarded along with them.
		auto text = BlockText(blocks_list, i);
		if (element_type == BlockTypes::TYPE_HR || element_type == BlockTypes::TYPE_RAW) {
			continue;
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

// db_blocks_to_text(blocks) -- same as above with the default "\n\n" separator
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

		idx_t bi = 0;
		while (bi < blocks_list.size()) {
			auto &block = blocks_list[bi];
			if (block.IsNull()) {
				bi++;
				continue;
			}

			auto element_type = GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX);

			if (element_type != BlockTypes::TYPE_HEADING) {
				bi++;
				continue;
			}

			// Get heading_level from attributes, falling back to level field for backward compatibility
			auto heading_level_str = GetElementAttribute(block, "heading_level");
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
	toc_struct_children.push_back(make_pair("indent", LogicalType::INTEGER));
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

		idx_t bi = 0;
		while (bi < blocks_list.size()) {
			auto &block = blocks_list[bi];
			if (block.IsNull()) {
				bi++;
				continue;
			}

			auto element_type = GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX);

			if (element_type != BlockTypes::TYPE_HEADING) {
				bi++;
				continue;
			}

			// Get heading_level from attributes, falling back to level field
			auto heading_level_str = GetElementAttribute(block, "heading_level");
			int32_t level;
			if (!heading_level_str.empty()) {
				// Safe parse: malformed/out-of-range values fall back instead of throwing
				level = ParseInt32OrDefault(heading_level_str, GetElementIntField(block, BlockTypes::LEVEL_IDX, 1));
			} else {
				level = GetElementIntField(block, BlockTypes::LEVEL_IDX, 1);
			}
			auto id = GetElementAttribute(block, "id");
			auto element_order = GetElementIntField(block, BlockTypes::ELEMENT_ORDER_IDX, 0);
			// Same content-or-inline-children rule as db_blocks_headings (issue #20)
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
			toc_values.push_back(make_pair("indent", Value(indent)));
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

void ExtractionFunctions::Register(ExtensionLoader &loader) {
	auto duck_block_list_type = BlockTypes::DuckBlockListType();

	// db_blocks_to_text(blocks LIST(duck_block), separator VARCHAR) -> VARCHAR
	auto to_text_func = ScalarFunction("db_blocks_to_text", {duck_block_list_type, LogicalType::VARCHAR},
	                                   LogicalType::VARCHAR, DbBlocksToTextFun);
	loader.RegisterFunction(to_text_func);

	// Single-arg version with default separator
	auto to_text_func_simple = ScalarFunction("db_blocks_to_text", {duck_block_list_type}, LogicalType::VARCHAR,
	                                          DbBlocksToTextDefaultSeparatorFun);
	loader.RegisterFunction(to_text_func_simple);

	// Define return types for headings
	child_list_t<LogicalType> heading_struct_children;
	heading_struct_children.push_back(make_pair("level", LogicalType::INTEGER));
	heading_struct_children.push_back(make_pair("title", LogicalType::VARCHAR));
	heading_struct_children.push_back(make_pair("id", LogicalType::VARCHAR));
	heading_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	auto heading_list_type = LogicalType::LIST(LogicalType::STRUCT(std::move(heading_struct_children)));

	// db_blocks_headings(blocks LIST(duck_block)) -> LIST(STRUCT)
	auto headings_func =
	    ScalarFunction("db_blocks_headings", {duck_block_list_type}, heading_list_type, DbBlocksHeadingsFun);
	loader.RegisterFunction(headings_func);

	// Define return types for code blocks
	child_list_t<LogicalType> code_struct_children;
	code_struct_children.push_back(make_pair("language", LogicalType::VARCHAR));
	code_struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
	code_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	auto code_list_type = LogicalType::LIST(LogicalType::STRUCT(std::move(code_struct_children)));

	// db_blocks_code_blocks(blocks LIST(duck_block)) -> LIST(STRUCT)
	auto code_blocks_func =
	    ScalarFunction("db_blocks_code_blocks", {duck_block_list_type}, code_list_type, DbBlocksCodeBlocksFun);
	loader.RegisterFunction(code_blocks_func);

	// Define return types for stats
	child_list_t<LogicalType> stats_struct_children;
	stats_struct_children.push_back(make_pair("element_type", LogicalType::VARCHAR));
	stats_struct_children.push_back(make_pair("count", LogicalType::INTEGER));
	stats_struct_children.push_back(make_pair("total_content_length", LogicalType::BIGINT));
	stats_struct_children.push_back(make_pair("avg_content_length", LogicalType::DOUBLE));
	auto stats_list_type = LogicalType::LIST(LogicalType::STRUCT(std::move(stats_struct_children)));

	// db_blocks_stats(blocks LIST(duck_block)) -> LIST(STRUCT)
	auto stats_func = ScalarFunction("db_blocks_stats", {duck_block_list_type}, stats_list_type, DbBlocksStatsFun);
	loader.RegisterFunction(stats_func);

	// Define return types for TOC
	child_list_t<LogicalType> toc_struct_children;
	toc_struct_children.push_back(make_pair("level", LogicalType::INTEGER));
	toc_struct_children.push_back(make_pair("title", LogicalType::VARCHAR));
	toc_struct_children.push_back(make_pair("id", LogicalType::VARCHAR));
	toc_struct_children.push_back(make_pair("indent", LogicalType::INTEGER));
	toc_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	auto toc_list_type = LogicalType::LIST(LogicalType::STRUCT(std::move(toc_struct_children)));

	// db_blocks_toc(blocks LIST(duck_block)) -> LIST(STRUCT)
	auto toc_func = ScalarFunction("db_blocks_toc", {duck_block_list_type}, toc_list_type, DbBlocksTocFun);
	loader.RegisterFunction(toc_func);

	// Define return types for links
	child_list_t<LogicalType> link_struct_children;
	link_struct_children.push_back(make_pair("href", LogicalType::VARCHAR));
	link_struct_children.push_back(make_pair("text", LogicalType::VARCHAR));
	link_struct_children.push_back(make_pair("title", LogicalType::VARCHAR));
	link_struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	auto link_list_type = LogicalType::LIST(LogicalType::STRUCT(std::move(link_struct_children)));

	// db_blocks_links(blocks LIST(duck_block)) -> LIST(STRUCT)
	auto links_func = ScalarFunction("db_blocks_links", {duck_block_list_type}, link_list_type, DbBlocksLinksFun);
	loader.RegisterFunction(links_func);
}

} // namespace duckdb
