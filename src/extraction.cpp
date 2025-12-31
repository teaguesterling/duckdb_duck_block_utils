#include "extraction.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"

#include <map>

namespace duckdb {

// Helper to get a string field from a block struct
static string GetBlockStringField(const Value &block, idx_t field_idx) {
	auto &children = StructValue::GetChildren(block);
	if (children[field_idx].IsNull()) {
		return "";
	}
	return children[field_idx].GetValue<string>();
}

// Helper to get an int field from a block struct
static int32_t GetBlockIntField(const Value &block, idx_t field_idx, int32_t default_val = 0) {
	auto &children = StructValue::GetChildren(block);
	if (children[field_idx].IsNull()) {
		return default_val;
	}
	return children[field_idx].GetValue<int32_t>();
}

// Helper to get attribute value from a block
static string GetBlockAttribute(const Value &block, const string &key) {
	auto &children = StructValue::GetChildren(block);
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

void ExtractionFunctions::DocBlocksToTextFun(DataChunk &args, ExpressionState &state, Vector &result) {
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

		string text_content;
		bool first = true;

		for (auto &block : blocks_list) {
			if (block.IsNull()) {
				continue;
			}

			auto block_type = GetBlockStringField(block, BlockTypes::BLOCK_TYPE_IDX);
			auto content = GetBlockStringField(block, BlockTypes::CONTENT_IDX);

			// Skip blocks that don't have meaningful text content
			if (block_type == BlockTypes::TYPE_HR || block_type == BlockTypes::TYPE_RAW) {
				continue;
			}

			// Skip empty content
			if (content.empty()) {
				continue;
			}

			if (!first) {
				text_content += separator;
			}
			first = false;
			text_content += content;
		}

		result.SetValue(i, Value(text_content));
	}
}

void ExtractionFunctions::DocBlocksHeadingsFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];

	// Define the return type for headings
	child_list_t<LogicalType> heading_struct_children;
	heading_struct_children.push_back(make_pair("level", LogicalType::INTEGER));
	heading_struct_children.push_back(make_pair("title", LogicalType::VARCHAR));
	heading_struct_children.push_back(make_pair("id", LogicalType::VARCHAR));
	heading_struct_children.push_back(make_pair("block_order", LogicalType::INTEGER));
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

		for (auto &block : blocks_list) {
			if (block.IsNull()) {
				continue;
			}

			auto block_type = GetBlockStringField(block, BlockTypes::BLOCK_TYPE_IDX);

			if (block_type != BlockTypes::TYPE_HEADING) {
				continue;
			}

			auto level = GetBlockIntField(block, BlockTypes::LEVEL_IDX, 1);
			auto title = GetBlockStringField(block, BlockTypes::CONTENT_IDX);
			auto id = GetBlockAttribute(block, "id");
			auto block_order = GetBlockIntField(block, BlockTypes::BLOCK_ORDER_IDX, 0);

			child_list_t<Value> heading_values;
			heading_values.push_back(make_pair("level", Value(level)));
			heading_values.push_back(make_pair("title", Value(title)));
			heading_values.push_back(make_pair("id", Value(id)));
			heading_values.push_back(make_pair("block_order", Value(block_order)));

			headings.push_back(Value::STRUCT(std::move(heading_values)));
		}

		result.SetValue(i, Value::LIST(heading_struct_type, std::move(headings)));
	}
}

void ExtractionFunctions::DocBlocksCodeBlocksFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];

	// Define the return type for code blocks
	child_list_t<LogicalType> code_struct_children;
	code_struct_children.push_back(make_pair("language", LogicalType::VARCHAR));
	code_struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
	code_struct_children.push_back(make_pair("block_order", LogicalType::INTEGER));
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

			auto block_type = GetBlockStringField(block, BlockTypes::BLOCK_TYPE_IDX);

			if (block_type != BlockTypes::TYPE_CODE) {
				continue;
			}

			auto language = GetBlockAttribute(block, "language");
			auto content = GetBlockStringField(block, BlockTypes::CONTENT_IDX);
			auto block_order = GetBlockIntField(block, BlockTypes::BLOCK_ORDER_IDX, 0);

			child_list_t<Value> code_values;
			code_values.push_back(make_pair("language", Value(language)));
			code_values.push_back(make_pair("content", Value(content)));
			code_values.push_back(make_pair("block_order", Value(block_order)));

			code_blocks.push_back(Value::STRUCT(std::move(code_values)));
		}

		result.SetValue(i, Value::LIST(code_struct_type, std::move(code_blocks)));
	}
}

void ExtractionFunctions::DocBlocksStatsFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];

	// Define the return type for stats
	child_list_t<LogicalType> stats_struct_children;
	stats_struct_children.push_back(make_pair("block_type", LogicalType::VARCHAR));
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

		// Accumulate stats by block type
		std::map<string, std::pair<int32_t, int64_t>> type_stats;  // type -> (count, total_length)

		for (auto &block : blocks_list) {
			if (block.IsNull()) {
				continue;
			}

			auto block_type = GetBlockStringField(block, BlockTypes::BLOCK_TYPE_IDX);
			auto content = GetBlockStringField(block, BlockTypes::CONTENT_IDX);

			auto &stats = type_stats[block_type];
			stats.first++;  // count
			stats.second += static_cast<int64_t>(content.length());  // total length
		}

		// Convert to result list
		vector<Value> stats_list;
		for (auto &entry : type_stats) {
			auto &type_name = entry.first;
			auto count_val = entry.second.first;
			auto total_length = entry.second.second;
			double avg_length = count_val > 0 ? static_cast<double>(total_length) / count_val : 0.0;

			child_list_t<Value> stat_values;
			stat_values.push_back(make_pair("block_type", Value(type_name)));
			stat_values.push_back(make_pair("count", Value(count_val)));
			stat_values.push_back(make_pair("total_content_length", Value(total_length)));
			stat_values.push_back(make_pair("avg_content_length", Value(avg_length)));

			stats_list.push_back(Value::STRUCT(std::move(stat_values)));
		}

		result.SetValue(i, Value::LIST(stats_struct_type, std::move(stats_list)));
	}
}

void ExtractionFunctions::Register(ExtensionLoader &loader) {
	auto doc_block_list_type = BlockTypes::DocBlockListType();

	// doc_blocks_to_text(blocks LIST(doc_block), separator VARCHAR) -> VARCHAR
	auto to_text_func = ScalarFunction(
	    "doc_blocks_to_text",
	    {doc_block_list_type, LogicalType::VARCHAR},
	    LogicalType::VARCHAR,
	    DocBlocksToTextFun
	);
	loader.RegisterFunction(to_text_func);

	// Single-arg version with default separator
	auto to_text_func_simple = ScalarFunction(
	    "doc_blocks_to_text",
	    {doc_block_list_type},
	    LogicalType::VARCHAR,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &blocks_vec = args.data[0];
		    auto count = args.size();

		    for (idx_t i = 0; i < count; i++) {
			    auto blocks_val = blocks_vec.GetValue(i);

			    if (blocks_val.IsNull()) {
				    result.SetValue(i, Value());
				    continue;
			    }

			    auto &blocks_list = ListValue::GetChildren(blocks_val);
			    string text_content;
			    bool first = true;

			    for (auto &block : blocks_list) {
				    if (block.IsNull()) {
					    continue;
				    }

				    auto block_type = GetBlockStringField(block, BlockTypes::BLOCK_TYPE_IDX);
				    auto content = GetBlockStringField(block, BlockTypes::CONTENT_IDX);

				    if (block_type == BlockTypes::TYPE_HR || block_type == BlockTypes::TYPE_RAW) {
					    continue;
				    }

				    if (content.empty()) {
					    continue;
				    }

				    if (!first) {
					    text_content += "\n\n";
				    }
				    first = false;
				    text_content += content;
			    }

			    result.SetValue(i, Value(text_content));
		    }
	    }
	);
	loader.RegisterFunction(to_text_func_simple);

	// Define return types for headings
	child_list_t<LogicalType> heading_struct_children;
	heading_struct_children.push_back(make_pair("level", LogicalType::INTEGER));
	heading_struct_children.push_back(make_pair("title", LogicalType::VARCHAR));
	heading_struct_children.push_back(make_pair("id", LogicalType::VARCHAR));
	heading_struct_children.push_back(make_pair("block_order", LogicalType::INTEGER));
	auto heading_list_type = LogicalType::LIST(LogicalType::STRUCT(std::move(heading_struct_children)));

	// doc_blocks_headings(blocks LIST(doc_block)) -> LIST(STRUCT)
	auto headings_func = ScalarFunction(
	    "doc_blocks_headings",
	    {doc_block_list_type},
	    heading_list_type,
	    DocBlocksHeadingsFun
	);
	loader.RegisterFunction(headings_func);

	// Define return types for code blocks
	child_list_t<LogicalType> code_struct_children;
	code_struct_children.push_back(make_pair("language", LogicalType::VARCHAR));
	code_struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
	code_struct_children.push_back(make_pair("block_order", LogicalType::INTEGER));
	auto code_list_type = LogicalType::LIST(LogicalType::STRUCT(std::move(code_struct_children)));

	// doc_blocks_code_blocks(blocks LIST(doc_block)) -> LIST(STRUCT)
	auto code_blocks_func = ScalarFunction(
	    "doc_blocks_code_blocks",
	    {doc_block_list_type},
	    code_list_type,
	    DocBlocksCodeBlocksFun
	);
	loader.RegisterFunction(code_blocks_func);

	// Define return types for stats
	child_list_t<LogicalType> stats_struct_children;
	stats_struct_children.push_back(make_pair("block_type", LogicalType::VARCHAR));
	stats_struct_children.push_back(make_pair("count", LogicalType::INTEGER));
	stats_struct_children.push_back(make_pair("total_content_length", LogicalType::BIGINT));
	stats_struct_children.push_back(make_pair("avg_content_length", LogicalType::DOUBLE));
	auto stats_list_type = LogicalType::LIST(LogicalType::STRUCT(std::move(stats_struct_children)));

	// doc_blocks_stats(blocks LIST(doc_block)) -> LIST(STRUCT)
	auto stats_func = ScalarFunction(
	    "doc_blocks_stats",
	    {doc_block_list_type},
	    stats_list_type,
	    DocBlocksStatsFun
	);
	loader.RegisterFunction(stats_func);
}

} // namespace duckdb
