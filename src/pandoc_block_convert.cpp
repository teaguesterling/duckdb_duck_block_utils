#include "pandoc_block_convert.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"

#include <sstream>
#include <vector>
#include <map>

namespace duckdb {

// Helper to create an attributes MAP
static Value CreateAttrsMap(const map<string, string> &attrs) {
	vector<Value> keys;
	vector<Value> values;
	for (auto &entry : attrs) {
		keys.push_back(Value(entry.first));
		values.push_back(Value(entry.second));
	}
	return Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values);
}

// Helper to create a single duck_block Value (for block)
static Value CreateDocBlock(const string &block_type, const string &content,
                            const map<string, string> &attrs, int32_t order,
                            const string &encoding = "text") {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
	struct_values.push_back(make_pair("element_type", Value(block_type)));
	struct_values.push_back(make_pair("content", Value(content)));
	struct_values.push_back(make_pair("level", Value()));  // NULL for blocks
	struct_values.push_back(make_pair("encoding", Value(encoding)));
	struct_values.push_back(make_pair("attributes", CreateAttrsMap(attrs)));
	struct_values.push_back(make_pair("element_order", Value(order)));
	return Value::STRUCT(std::move(struct_values));
}

// Simple JSON string extractor
static string ExtractJsonString(const string &json, const string &key) {
	string search = "\"" + key + "\":";
	size_t pos = json.find(search);
	if (pos == string::npos) return "";

	pos += search.length();
	while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

	if (pos >= json.length()) return "";

	if (json[pos] == '"') {
		pos++;
		string result;
		while (pos < json.length() && json[pos] != '"') {
			if (json[pos] == '\\' && pos + 1 < json.length()) {
				pos++;
				switch (json[pos]) {
					case 'n': result += '\n'; break;
					case 'r': result += '\r'; break;
					case 't': result += '\t'; break;
					case '"': result += '"'; break;
					case '\\': result += '\\'; break;
					default: result += json[pos];
				}
			} else {
				result += json[pos];
			}
			pos++;
		}
		return result;
	}
	return "";
}

// Extract integer from JSON
static int32_t ExtractJsonInt(const string &json, size_t start_pos) {
	while (start_pos < json.length() && !isdigit(json[start_pos]) && json[start_pos] != '-') {
		start_pos++;
	}
	if (start_pos >= json.length()) return 0;

	string num_str;
	while (start_pos < json.length() && (isdigit(json[start_pos]) || json[start_pos] == '-')) {
		num_str += json[start_pos++];
	}
	return num_str.empty() ? 0 : std::stoi(num_str);
}

// Find matching bracket/brace
static size_t FindMatchingBracket(const string &json, size_t start, char open, char close) {
	int count = 1;
	size_t pos = start + 1;
	while (pos < json.length() && count > 0) {
		if (json[pos] == open) count++;
		else if (json[pos] == close) count--;
		pos++;
	}
	return pos;
}

// Extract content from inlines array (simple text extraction)
static string ExtractInlinesText(const string &json) {
	string result;
	size_t pos = 0;

	while (pos < json.length()) {
		// Find Str elements
		size_t str_pos = json.find("\"t\":\"Str\"", pos);
		if (str_pos == string::npos) break;

		size_t c_pos = json.find("\"c\":", str_pos);
		if (c_pos != string::npos) {
			size_t quote_start = json.find("\"", c_pos + 4);
			if (quote_start != string::npos) {
				quote_start++;
				size_t quote_end = quote_start;
				while (quote_end < json.length() && json[quote_end] != '"') {
					if (json[quote_end] == '\\' && quote_end + 1 < json.length()) {
						quote_end += 2;
					} else {
						quote_end++;
					}
				}
				// Decode escaped string
				for (size_t i = quote_start; i < quote_end; i++) {
					if (json[i] == '\\' && i + 1 < quote_end) {
						i++;
						switch (json[i]) {
							case 'n': result += '\n'; break;
							case 't': result += '\t'; break;
							case '"': result += '"'; break;
							case '\\': result += '\\'; break;
							default: result += json[i];
						}
					} else {
						result += json[i];
					}
				}
			}
		}

		// Check for Space
		size_t space_pos = json.find("\"t\":\"Space\"", pos);
		if (space_pos != string::npos && (str_pos == string::npos || space_pos < str_pos)) {
			result += " ";
			pos = space_pos + 11;
			continue;
		}

		pos = str_pos + 10;
	}

	return result;
}

// Process a single Pandoc block and add to result
static void ProcessPandocBlock(const string &block_json, int32_t &order, vector<Value> &result) {
	// Find the type
	size_t type_start = block_json.find("\"t\":");
	if (type_start == string::npos) return;

	size_t quote_start = block_json.find("\"", type_start + 4);
	if (quote_start == string::npos) return;
	quote_start++;

	size_t quote_end = block_json.find("\"", quote_start);
	if (quote_end == string::npos) return;

	string pandoc_type = block_json.substr(quote_start, quote_end - quote_start);

	map<string, string> attrs;
	string content;
	string block_type;
	string encoding = "text";

	if (pandoc_type == "Header") {
		block_type = BlockTypes::TYPE_HEADING;
		// Header format: [level, [id, classes, attrs], inlines]
		size_t c_pos = block_json.find("\"c\":");
		if (c_pos != string::npos) {
			size_t arr_start = block_json.find("[", c_pos);
			if (arr_start != string::npos) {
				// Extract level (first element)
				int32_t level = ExtractJsonInt(block_json, arr_start + 1);
				attrs["heading_level"] = std::to_string(level);

				// Find the inlines array (third element)
				// Skip past level and attr array
				size_t attr_arr = block_json.find("[", arr_start + 1);
				if (attr_arr != string::npos) {
					// Try to extract id from attr array
					size_t id_quote = block_json.find("\"", attr_arr + 1);
					if (id_quote != string::npos) {
						size_t id_end = block_json.find("\"", id_quote + 1);
						if (id_end != string::npos) {
							string id = block_json.substr(id_quote + 1, id_end - id_quote - 1);
							if (!id.empty()) {
								attrs["id"] = id;
							}
						}
					}

					size_t attr_end = FindMatchingBracket(block_json, attr_arr, '[', ']');
					size_t inlines_start = block_json.find("[", attr_end);
					if (inlines_start != string::npos) {
						size_t inlines_end = FindMatchingBracket(block_json, inlines_start, '[', ']');
						string inlines = block_json.substr(inlines_start, inlines_end - inlines_start);
						content = ExtractInlinesText(inlines);
					}
				}
			}
		}
	} else if (pandoc_type == "Para") {
		block_type = BlockTypes::TYPE_PARAGRAPH;
		size_t c_pos = block_json.find("\"c\":");
		if (c_pos != string::npos) {
			size_t arr_start = block_json.find("[", c_pos);
			if (arr_start != string::npos) {
				size_t arr_end = FindMatchingBracket(block_json, arr_start, '[', ']');
				string inlines = block_json.substr(arr_start, arr_end - arr_start);
				content = ExtractInlinesText(inlines);
			}
		}
	} else if (pandoc_type == "Plain") {
		block_type = BlockTypes::TYPE_PARAGRAPH;  // Treat Plain as paragraph
		size_t c_pos = block_json.find("\"c\":");
		if (c_pos != string::npos) {
			size_t arr_start = block_json.find("[", c_pos);
			if (arr_start != string::npos) {
				size_t arr_end = FindMatchingBracket(block_json, arr_start, '[', ']');
				string inlines = block_json.substr(arr_start, arr_end - arr_start);
				content = ExtractInlinesText(inlines);
			}
		}
	} else if (pandoc_type == "CodeBlock") {
		block_type = BlockTypes::TYPE_CODE;
		// CodeBlock format: [[id, classes, attrs], code_string]
		size_t c_pos = block_json.find("\"c\":");
		if (c_pos != string::npos) {
			size_t arr_start = block_json.find("[", c_pos);
			if (arr_start != string::npos) {
				// Find the attributes array
				size_t attr_arr = block_json.find("[", arr_start + 1);
				if (attr_arr != string::npos) {
					// Look for language in classes (second element of attr array)
					size_t classes_start = block_json.find("[", attr_arr + 1);
					if (classes_start != string::npos) {
						size_t class_quote = block_json.find("\"", classes_start);
						size_t classes_end = block_json.find("]", classes_start);
						if (class_quote != string::npos && class_quote < classes_end) {
							size_t class_end = block_json.find("\"", class_quote + 1);
							if (class_end != string::npos) {
								attrs["language"] = block_json.substr(class_quote + 1, class_end - class_quote - 1);
							}
						}
					}

					size_t attr_end = FindMatchingBracket(block_json, attr_arr, '[', ']');
					// Find the code string after attr array
					size_t code_quote = block_json.find("\"", attr_end);
					if (code_quote != string::npos) {
						code_quote++;
						size_t code_end = code_quote;
						// Find end quote (handling escapes)
						while (code_end < block_json.length()) {
							if (block_json[code_end] == '"') break;
							if (block_json[code_end] == '\\' && code_end + 1 < block_json.length()) {
								code_end += 2;
							} else {
								code_end++;
							}
						}
						// Decode the code content
						for (size_t i = code_quote; i < code_end; i++) {
							if (block_json[i] == '\\' && i + 1 < code_end) {
								i++;
								switch (block_json[i]) {
									case 'n': content += '\n'; break;
									case 't': content += '\t'; break;
									case '"': content += '"'; break;
									case '\\': content += '\\'; break;
									default: content += block_json[i];
								}
							} else {
								content += block_json[i];
							}
						}
					}
				}
			}
		}
	} else if (pandoc_type == "BlockQuote") {
		block_type = BlockTypes::TYPE_BLOCKQUOTE;
		// BlockQuote contains nested blocks - extract text from them
		size_t c_pos = block_json.find("\"c\":");
		if (c_pos != string::npos) {
			size_t arr_start = block_json.find("[", c_pos);
			if (arr_start != string::npos) {
				size_t arr_end = FindMatchingBracket(block_json, arr_start, '[', ']');
				string inner = block_json.substr(arr_start, arr_end - arr_start);
				content = ExtractInlinesText(inner);  // Simplified
			}
		}
	} else if (pandoc_type == "BulletList" || pandoc_type == "OrderedList") {
		block_type = BlockTypes::TYPE_LIST;
		attrs["list_type"] = (pandoc_type == "BulletList") ? "bullet" : "ordered";
		encoding = "json";
		// Store the list content as JSON
		size_t c_pos = block_json.find("\"c\":");
		if (c_pos != string::npos) {
			size_t arr_start = block_json.find("[", c_pos);
			if (arr_start != string::npos) {
				size_t arr_end = FindMatchingBracket(block_json, arr_start, '[', ']');
				content = block_json.substr(arr_start, arr_end - arr_start);
			}
		}
	} else if (pandoc_type == "Table") {
		block_type = BlockTypes::TYPE_TABLE;
		encoding = "json";
		// Store table as JSON
		size_t c_pos = block_json.find("\"c\":");
		if (c_pos != string::npos) {
			content = block_json.substr(c_pos + 4);
			// Trim to just the content
			size_t end = content.rfind("}");
			if (end != string::npos) {
				content = content.substr(0, end);
			}
		}
	} else if (pandoc_type == "HorizontalRule") {
		block_type = BlockTypes::TYPE_HR;
		content = "";
	} else if (pandoc_type == "RawBlock") {
		block_type = BlockTypes::TYPE_RAW;
		// RawBlock format: [format_string, content_string]
		size_t c_pos = block_json.find("\"c\":");
		if (c_pos != string::npos) {
			size_t arr_start = block_json.find("[", c_pos);
			if (arr_start != string::npos) {
				// Extract format
				size_t fmt_quote = block_json.find("\"", arr_start);
				if (fmt_quote != string::npos) {
					size_t fmt_end = block_json.find("\"", fmt_quote + 1);
					if (fmt_end != string::npos) {
						attrs["format"] = block_json.substr(fmt_quote + 1, fmt_end - fmt_quote - 1);
					}
					// Extract content
					size_t content_quote = block_json.find("\"", fmt_end + 1);
					if (content_quote != string::npos) {
						content_quote++;
						size_t content_end = content_quote;
						while (content_end < block_json.length()) {
							if (block_json[content_end] == '"') break;
							if (block_json[content_end] == '\\' && content_end + 1 < block_json.length()) {
								content_end += 2;
							} else {
								content_end++;
							}
						}
						for (size_t i = content_quote; i < content_end; i++) {
							if (block_json[i] == '\\' && i + 1 < content_end) {
								i++;
								switch (block_json[i]) {
									case 'n': content += '\n'; break;
									case 't': content += '\t'; break;
									default: content += block_json[i];
								}
							} else {
								content += block_json[i];
							}
						}
					}
				}
			}
		}
	} else if (pandoc_type == "Div") {
		// Div is a container - process its children recursively
		size_t c_pos = block_json.find("\"c\":");
		if (c_pos != string::npos) {
			// Div format: [attr, [blocks]]
			size_t arr_start = block_json.find("[", c_pos);
			if (arr_start != string::npos) {
				// Skip attr array
				size_t attr_arr = block_json.find("[", arr_start + 1);
				if (attr_arr != string::npos) {
					size_t attr_end = FindMatchingBracket(block_json, attr_arr, '[', ']');
					size_t blocks_start = block_json.find("[", attr_end);
					if (blocks_start != string::npos) {
						size_t blocks_end = FindMatchingBracket(block_json, blocks_start, '[', ']');
						string blocks = block_json.substr(blocks_start + 1, blocks_end - blocks_start - 2);
						// Process nested blocks... (simplified for now)
					}
				}
			}
		}
		return;  // Skip Div container itself
	} else {
		// Unknown block type - skip
		return;
	}

	if (!block_type.empty()) {
		result.push_back(CreateDocBlock(block_type, content, attrs, order++, encoding));
	}
}

// Parse Pandoc blocks array
static void ParsePandocBlocks(const string &json, int32_t &order, vector<Value> &result) {
	// Find each block object in the array
	size_t pos = 0;

	while (pos < json.length()) {
		// Find next block object
		size_t obj_start = json.find("{\"t\":", pos);
		if (obj_start == string::npos) break;

		// Find matching closing brace
		size_t obj_end = FindMatchingBracket(json, obj_start, '{', '}');

		string block_json = json.substr(obj_start, obj_end - obj_start);
		ProcessPandocBlock(block_json, order, result);

		pos = obj_end;
	}
}

void PandocBlockConvert::PandocAstToBlocksFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &json_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto json_val = json_vec.GetValue(i);

		// Handle NULL input
		if (json_val.IsNull()) {
			result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), vector<Value>()));
			continue;
		}

		string json = json_val.GetValue<string>();
		vector<Value> blocks;
		int32_t order = 0;

		// Check if this is a full Pandoc AST or just blocks array
		size_t blocks_key = json.find("\"blocks\":");
		if (blocks_key != string::npos) {
			// Full AST - find blocks array
			size_t arr_start = json.find("[", blocks_key);
			if (arr_start != string::npos) {
				size_t arr_end = FindMatchingBracket(json, arr_start, '[', ']');
				string blocks_json = json.substr(arr_start, arr_end - arr_start);
				ParsePandocBlocks(blocks_json, order, blocks);
			}
		} else {
			// Assume it's just a blocks array
			ParsePandocBlocks(json, order, blocks);
		}

		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(blocks)));
	}
}

// Helper to escape string for JSON
static string JsonEscape(const string &s) {
	string result;
	for (char c : s) {
		switch (c) {
			case '"': result += "\\\""; break;
			case '\\': result += "\\\\"; break;
			case '\n': result += "\\n"; break;
			case '\r': result += "\\r"; break;
			case '\t': result += "\\t"; break;
			default: result += c;
		}
	}
	return result;
}

// Helper to get string field from duck_block
static string GetElementStringField(const Value &element, idx_t field_idx) {
	auto &children = StructValue::GetChildren(element);
	if (children[field_idx].IsNull()) {
		return "";
	}
	return children[field_idx].GetValue<string>();
}

// Helper to get attribute from duck_block
static string GetElementAttribute(const Value &element, const string &key) {
	auto &children = StructValue::GetChildren(element);
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

void PandocBlockConvert::PandocBlocksToAstFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		// Handle NULL input
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value("[]"));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);
		std::ostringstream oss;
		oss << "[";

		bool first = true;
		for (auto &block : blocks_list) {
			if (block.IsNull()) continue;

			auto kind = GetElementStringField(block, BlockTypes::KIND_IDX);
			if (kind != BlockTypes::KIND_BLOCK) continue;  // Skip inlines

			auto element_type = GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX);
			auto content = GetElementStringField(block, BlockTypes::CONTENT_IDX);

			if (!first) oss << ",";
			first = false;

			if (element_type == BlockTypes::TYPE_HEADING) {
				auto level_str = GetElementAttribute(block, "heading_level");
				int level = level_str.empty() ? 1 : std::stoi(level_str);
				auto id = GetElementAttribute(block, "id");
				oss << "{\"t\":\"Header\",\"c\":[" << level << ",[\"" << JsonEscape(id) << "\",[],[]],[{\"t\":\"Str\",\"c\":\"" << JsonEscape(content) << "\"}]]}";
			} else if (element_type == BlockTypes::TYPE_PARAGRAPH) {
				oss << "{\"t\":\"Para\",\"c\":[{\"t\":\"Str\",\"c\":\"" << JsonEscape(content) << "\"}]}";
			} else if (element_type == BlockTypes::TYPE_CODE) {
				auto language = GetElementAttribute(block, "language");
				oss << "{\"t\":\"CodeBlock\",\"c\":[[\"\",[\"" << JsonEscape(language) << "\"],[]],\"" << JsonEscape(content) << "\"]}";
			} else if (element_type == BlockTypes::TYPE_BLOCKQUOTE) {
				oss << "{\"t\":\"BlockQuote\",\"c\":[{\"t\":\"Para\",\"c\":[{\"t\":\"Str\",\"c\":\"" << JsonEscape(content) << "\"}]}]}";
			} else if (element_type == BlockTypes::TYPE_HR) {
				oss << "{\"t\":\"HorizontalRule\"}";
			} else if (element_type == BlockTypes::TYPE_RAW) {
				auto format = GetElementAttribute(block, "format");
				if (format.empty()) format = "html";
				oss << "{\"t\":\"RawBlock\",\"c\":[\"" << JsonEscape(format) << "\",\"" << JsonEscape(content) << "\"]}";
			} else if (element_type == BlockTypes::TYPE_LIST) {
				auto list_type = GetElementAttribute(block, "list_type");
				string pandoc_type = (list_type == "ordered") ? "OrderedList" : "BulletList";
				// If content is already JSON, use it directly
				auto encoding = GetElementStringField(block, BlockTypes::ENCODING_IDX);
				if (encoding == "json" && !content.empty()) {
					if (pandoc_type == "OrderedList") {
						oss << "{\"t\":\"OrderedList\",\"c\":[[1,{\"t\":\"Decimal\"},{\"t\":\"Period\"}]," << content << "]}";
					} else {
						oss << "{\"t\":\"BulletList\",\"c\":" << content << "}";
					}
				} else {
					// Simple list representation
					oss << "{\"t\":\"" << pandoc_type << "\",\"c\":[]}";
				}
			} else if (element_type == BlockTypes::TYPE_TABLE) {
				// Simplified table output
				oss << "{\"t\":\"Table\",\"c\":[[\"\",[],[]],[[{\"t\":\"AlignDefault\"}]],[[[]]]]}";
			} else {
				// Unknown - output as paragraph
				oss << "{\"t\":\"Para\",\"c\":[{\"t\":\"Str\",\"c\":\"" << JsonEscape(content) << "\"}]}";
			}
		}

		oss << "]";
		result.SetValue(i, Value(oss.str()));
	}
}

void PandocBlockConvert::Register(ExtensionLoader &loader) {
	auto duck_block_list_type = BlockTypes::DuckBlockListType();

	// pandoc_ast_to_blocks(json VARCHAR) -> LIST(duck_block)
	auto ast_to_blocks_func = ScalarFunction(
	    "pandoc_ast_to_blocks",
	    {LogicalType::VARCHAR},
	    duck_block_list_type,
	    PandocAstToBlocksFun
	);
	loader.RegisterFunction(ast_to_blocks_func);

	// pandoc_blocks_to_ast(blocks LIST(duck_block)) -> VARCHAR
	auto blocks_to_ast_func = ScalarFunction(
	    "pandoc_blocks_to_ast",
	    {duck_block_list_type},
	    LogicalType::VARCHAR,
	    PandocBlocksToAstFun
	);
	loader.RegisterFunction(blocks_to_ast_func);
}

} // namespace duckdb
