#include "pandoc_block_convert.hpp"
#include "pandoc_inline_convert.hpp"
#include "block_types.hpp"
#include "duckdb_compat.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"

#include <sstream>
#include <vector>
#include <map>
#include <fstream>
#include <utility>

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
static Value CreateDocBlock(const string &block_type, const string &content, const map<string, string> &attrs,
                            int32_t order, const string &encoding = "text") {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
	struct_values.push_back(make_pair("element_type", Value(block_type)));
	struct_values.push_back(make_pair("content", Value(content)));
	struct_values.push_back(make_pair("level", Value())); // NULL for blocks
	struct_values.push_back(make_pair("encoding", Value(encoding)));
	struct_values.push_back(make_pair("attributes", CreateAttrsMap(attrs)));
	struct_values.push_back(make_pair("element_order", Value(order)));
	return Value::STRUCT(std::move(struct_values));
}

// Simple JSON string extractor
static string ExtractJsonString(const string &json, const string &key) {
	string search = "\"" + key + "\":";
	size_t pos = json.find(search);
	if (pos == string::npos)
		return "";

	pos += search.length();
	while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t'))
		pos++;

	if (pos >= json.length())
		return "";

	if (json[pos] == '"') {
		pos++;
		string result;
		while (pos < json.length() && json[pos] != '"') {
			if (json[pos] == '\\' && pos + 1 < json.length()) {
				pos++;
				switch (json[pos]) {
				case 'n':
					result += '\n';
					break;
				case 'r':
					result += '\r';
					break;
				case 't':
					result += '\t';
					break;
				case '"':
					result += '"';
					break;
				case '\\':
					result += '\\';
					break;
				default:
					result += json[pos];
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
	if (start_pos >= json.length())
		return 0;

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
		if (json[pos] == open)
			count++;
		else if (json[pos] == close)
			count--;
		pos++;
	}
	return pos;
}

// Extract content from inlines array (simple text extraction)
static string ExtractInlinesText(const string &json) {
	string result;
	size_t pos = 0;

	while (pos < json.length()) {
		// Find next Str or Space element
		size_t str_pos = json.find("\"t\":\"Str\"", pos);
		size_t space_pos = json.find("\"t\":\"Space\"", pos);
		size_t softbreak_pos = json.find("\"t\":\"SoftBreak\"", pos);
		size_t linebreak_pos = json.find("\"t\":\"LineBreak\"", pos);

		// Find the earliest element
		size_t next_pos = string::npos;
		string token_type;

		if (str_pos != string::npos && (next_pos == string::npos || str_pos < next_pos)) {
			next_pos = str_pos;
			token_type = "Str";
		}
		if (space_pos != string::npos && (next_pos == string::npos || space_pos < next_pos)) {
			next_pos = space_pos;
			token_type = "Space";
		}
		if (softbreak_pos != string::npos && (next_pos == string::npos || softbreak_pos < next_pos)) {
			next_pos = softbreak_pos;
			token_type = "SoftBreak";
		}
		if (linebreak_pos != string::npos && (next_pos == string::npos || linebreak_pos < next_pos)) {
			next_pos = linebreak_pos;
			token_type = "LineBreak";
		}

		if (next_pos == string::npos)
			break;

		if (token_type == "Space") {
			result += " ";
			pos = next_pos + 11; // strlen("\"t\":\"Space\"")
		} else if (token_type == "SoftBreak") {
			result += " ";
			pos = next_pos + 15; // strlen("\"t\":\"SoftBreak\"")
		} else if (token_type == "LineBreak") {
			result += "\n";
			pos = next_pos + 15; // strlen("\"t\":\"LineBreak\"")
		} else if (token_type == "Str") {
			size_t c_pos = json.find("\"c\":", next_pos);
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
							case 'n':
								result += '\n';
								break;
							case 't':
								result += '\t';
								break;
							case '"':
								result += '"';
								break;
							case '\\':
								result += '\\';
								break;
							default:
								result += json[i];
							}
						} else {
							result += json[i];
						}
					}
					pos = quote_end + 1;
					continue;
				}
			}
			pos = next_pos + 10; // strlen("\"t\":\"Str\"")
		}
	}

	return result;
}

// Process a single Pandoc block and add to result
static void ProcessPandocBlock(const string &block_json, int32_t &order, vector<Value> &result) {
	// Find the type
	size_t type_start = block_json.find("\"t\":");
	if (type_start == string::npos)
		return;

	size_t quote_start = block_json.find("\"", type_start + 4);
	if (quote_start == string::npos)
		return;
	quote_start++;

	size_t quote_end = block_json.find("\"", quote_start);
	if (quote_end == string::npos)
		return;

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
		block_type = BlockTypes::TYPE_PARAGRAPH; // Treat Plain as paragraph
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
							if (block_json[code_end] == '"')
								break;
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
								case 'n':
									content += '\n';
									break;
								case 't':
									content += '\t';
									break;
								case '"':
									content += '"';
									break;
								case '\\':
									content += '\\';
									break;
								default:
									content += block_json[i];
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
				content = ExtractInlinesText(inner); // Simplified
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
							if (block_json[content_end] == '"')
								break;
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
								case 'n':
									content += '\n';
									break;
								case 't':
									content += '\t';
									break;
								default:
									content += block_json[i];
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
		block_type = BlockTypes::TYPE_DIV;
		// Div format: [attr, [blocks]] where attr is [id, [classes], [[key,val]...]]
		size_t c_pos = block_json.find("\"c\":");
		if (c_pos != string::npos) {
			size_t arr_start = block_json.find("[", c_pos);
			if (arr_start != string::npos) {
				// Find attr array [id, [classes], [...]]
				size_t attr_arr = block_json.find("[", arr_start + 1);
				if (attr_arr != string::npos) {
					// Extract id (first element)
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
					// Extract classes (second element - array)
					size_t classes_start = block_json.find("[", attr_arr + 1);
					if (classes_start != string::npos) {
						size_t classes_end = FindMatchingBracket(block_json, classes_start, '[', ']');
						// Look for class strings inside
						size_t class_quote = block_json.find("\"", classes_start + 1);
						if (class_quote != string::npos && class_quote < classes_end) {
							size_t class_end = block_json.find("\"", class_quote + 1);
							if (class_end != string::npos && class_end < classes_end) {
								attrs["class"] = block_json.substr(class_quote + 1, class_end - class_quote - 1);
							}
						}
					}

					size_t attr_end = FindMatchingBracket(block_json, attr_arr, '[', ']');
					size_t blocks_start = block_json.find("[", attr_end);
					if (blocks_start != string::npos) {
						size_t blocks_end = FindMatchingBracket(block_json, blocks_start, '[', ']');
						string nested_blocks = block_json.substr(blocks_start, blocks_end - blocks_start);

						// First add the div block itself
						result.push_back(CreateDocBlock(block_type, "", attrs, order++, encoding));

						// Inline parsing of nested blocks (can't call ParsePandocBlocks - defined later)
						size_t nested_pos = 0;
						while (nested_pos < nested_blocks.length()) {
							size_t obj_start = nested_blocks.find("{\"t\":", nested_pos);
							if (obj_start == string::npos)
								break;
							size_t obj_end = FindMatchingBracket(nested_blocks, obj_start, '{', '}');
							string child_block = nested_blocks.substr(obj_start, obj_end - obj_start);
							ProcessPandocBlock(child_block, order, result);
							nested_pos = obj_end;
						}
						return; // Already added blocks
					}
				}
			}
		}
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
		if (obj_start == string::npos)
			break;

		// Find matching closing brace
		size_t obj_end = FindMatchingBracket(json, obj_start, '{', '}');

		string block_json = json.substr(obj_start, obj_end - obj_start);
		ProcessPandocBlock(block_json, order, result);

		pos = obj_end;
	}
}

// Public function for converting Pandoc AST JSON to blocks
void PandocBlockConvert::ConvertPandocAstToBlocks(const string &json, vector<Value> &blocks) {
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
		ConvertPandocAstToBlocks(json, blocks);

		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(blocks)));
	}
}

// Helper to escape string for JSON
static string JsonEscape(const string &s) {
	string result;
	for (char c : s) {
		switch (c) {
		case '"':
			result += "\\\"";
			break;
		case '\\':
			result += "\\\\";
			break;
		case '\n':
			result += "\\n";
			break;
		case '\r':
			result += "\\r";
			break;
		case '\t':
			result += "\\t";
			break;
		default:
			result += c;
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

// Helper to get level from duck_block
static int32_t GetElementLevel(const Value &element) {
	auto &children = StructValue::GetChildren(element);
	if (children[BlockTypes::LEVEL_IDX].IsNull()) {
		return 1;
	}
	return children[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
}

// Forward declaration for recursive list processing
static string ConvertListToPandocJson(const vector<Value> &blocks_list, idx_t &start_idx, int32_t list_level);

// Forward declaration for recursive div processing
static string ConvertDivToPandocJson(const vector<Value> &blocks_list, idx_t &start_idx, int32_t div_level);

// Convert a list block and its children to Pandoc JSON, handling nested lists recursively
static string ConvertListToPandocJson(const vector<Value> &blocks_list, idx_t &start_idx, int32_t list_level) {
	auto &list_block = blocks_list[start_idx];
	auto list_type = GetElementAttribute(list_block, "list_type");
	auto ordered_attr = GetElementAttribute(list_block, "ordered");
	bool is_ordered = (list_type == "ordered") || (ordered_attr == "true");
	string pandoc_type = is_ordered ? "OrderedList" : "BulletList";

	// List items: each item is a list of blocks (Plain + optional nested list)
	// Structure: vector of (content, inlines, nested_list_json)
	struct ListItem {
		string content;
		vector<Value> inlines;
		string nested_list_json;
	};
	vector<ListItem> items;
	ListItem current_item;
	bool in_item = false;

	idx_t j = start_idx + 1;
	while (j < blocks_list.size()) {
		auto &child = blocks_list[j];
		if (child.IsNull()) {
			j++;
			continue;
		}

		auto child_kind = GetElementStringField(child, BlockTypes::KIND_IDX);
		auto child_type = GetElementStringField(child, BlockTypes::ELEMENT_TYPE_IDX);
		int32_t child_level = GetElementLevel(child);

		// Check if this element belongs to this list (level > list_level)
		if (child_kind == BlockTypes::KIND_BLOCK) {
			if (child_type == BlockTypes::TYPE_LIST_ITEM && child_level == list_level + 1) {
				// This is a direct item of this list
				if (in_item) {
					items.push_back(current_item);
				}
				current_item = ListItem();
				current_item.content = GetElementStringField(child, BlockTypes::CONTENT_IDX);
				in_item = true;
				j++;
			} else if (child_type == BlockTypes::TYPE_LIST && child_level == list_level + 1) {
				// Nested list at same level as items - attach to previous item
				if (in_item) {
					// Recursively convert the nested list
					current_item.nested_list_json = ConvertListToPandocJson(blocks_list, j, child_level);
					// j is advanced by the recursive call
				} else {
					// No previous item - treat as orphan nested list, skip
					j++;
				}
			} else if (child_level <= list_level) {
				// Back to parent level or above - done with this list
				break;
			} else {
				// Some other block at deeper level - skip
				j++;
			}
		} else if (child_kind == BlockTypes::KIND_INLINE && in_item) {
			// Inline children for current item - check if they belong to this item
			if (child_level == list_level + 2) {
				current_item.inlines.push_back(child);
			}
			j++;
		} else {
			j++;
		}
	}

	// Don't forget the last item
	if (in_item) {
		items.push_back(current_item);
	}

	// Update start_idx to where we stopped
	start_idx = j;

	// Build Pandoc JSON for this list
	std::ostringstream oss;
	oss << "{\"t\":\"" << pandoc_type << "\",\"c\":";
	if (is_ordered) {
		oss << "[[1,{\"t\":\"Decimal\"},{\"t\":\"Period\"}],[";
	} else {
		oss << "[";
	}

	bool first_item = true;
	for (auto &item : items) {
		if (!first_item)
			oss << ",";
		first_item = false;

		// Each item is an array of blocks
		oss << "[";

		// First block: Plain with content/inlines
		if (!item.inlines.empty()) {
			string inlines_json = PandocInlineConvert::ConvertInlinesToPandocJson(item.inlines);
			oss << "{\"t\":\"Plain\",\"c\":" << inlines_json << "}";
		} else if (!item.content.empty()) {
			oss << "{\"t\":\"Plain\",\"c\":[{\"t\":\"Str\",\"c\":\"" << JsonEscape(item.content) << "\"}]}";
		} else {
			oss << "{\"t\":\"Plain\",\"c\":[]}";
		}

		// Second block (optional): nested list
		if (!item.nested_list_json.empty()) {
			oss << "," << item.nested_list_json;
		}

		oss << "]";
	}

	if (is_ordered) {
		oss << "]]}";
	} else {
		oss << "]}";
	}

	return oss.str();
}

// Convert a div block and its children to Pandoc JSON
// Div format: {"t":"Div","c":[["id",["classes"],[]],...blocks...]}
static string ConvertDivToPandocJson(const vector<Value> &blocks_list, idx_t &start_idx, int32_t div_level) {
	auto &div_block = blocks_list[start_idx];
	auto id = GetElementAttribute(div_block, "id");
	auto classes = GetElementAttribute(div_block, "class");

	std::ostringstream oss;
	oss << "{\"t\":\"Div\",\"c\":[[\"" << JsonEscape(id) << "\",[";

	// Output classes (split by space if multiple)
	if (!classes.empty()) {
		oss << "\"" << JsonEscape(classes) << "\"";
	}
	oss << "],[]],[";

	// Collect and convert child blocks
	bool first_child = true;
	idx_t j = start_idx + 1;
	while (j < blocks_list.size()) {
		auto &child = blocks_list[j];
		if (child.IsNull()) {
			j++;
			continue;
		}

		auto child_kind = GetElementStringField(child, BlockTypes::KIND_IDX);
		auto child_type = GetElementStringField(child, BlockTypes::ELEMENT_TYPE_IDX);
		int32_t child_level = GetElementLevel(child);

		// Stop when we reach blocks at or above div level
		if (child_level <= div_level && child_kind == BlockTypes::KIND_BLOCK) {
			break;
		}

		// Skip inlines - they're handled by their parent blocks
		if (child_kind == BlockTypes::KIND_INLINE) {
			j++;
			continue;
		}

		// Skip list_item - handled by list converter
		if (child_type == BlockTypes::TYPE_LIST_ITEM) {
			j++;
			continue;
		}

		// Convert child blocks
		if (child_kind == BlockTypes::KIND_BLOCK) {
			auto content = GetElementStringField(child, BlockTypes::CONTENT_IDX);

			// Collect inline children for this block
			vector<Value> inline_children;
			for (idx_t k = j + 1; k < blocks_list.size(); k++) {
				auto &inl = blocks_list[k];
				if (inl.IsNull())
					continue;
				auto inl_kind = GetElementStringField(inl, BlockTypes::KIND_IDX);
				if (inl_kind == BlockTypes::KIND_BLOCK)
					break;
				if (inl_kind == BlockTypes::KIND_INLINE) {
					inline_children.push_back(inl);
				}
			}

			if (!first_child)
				oss << ",";
			first_child = false;

			if (child_type == BlockTypes::TYPE_PARAGRAPH) {
				if (!inline_children.empty()) {
					string inlines_json = PandocInlineConvert::ConvertInlinesToPandocJson(inline_children);
					oss << "{\"t\":\"Para\",\"c\":" << inlines_json << "}";
				} else {
					oss << "{\"t\":\"Para\",\"c\":[{\"t\":\"Str\",\"c\":\"" << JsonEscape(content) << "\"}]}";
				}
				j++;
			} else if (child_type == BlockTypes::TYPE_HEADING) {
				auto level_str = GetElementAttribute(child, "heading_level");
				int level = level_str.empty() ? 1 : std::stoi(level_str);
				auto hid = GetElementAttribute(child, "id");
				if (!inline_children.empty()) {
					string inlines_json = PandocInlineConvert::ConvertInlinesToPandocJson(inline_children);
					oss << "{\"t\":\"Header\",\"c\":[" << level << ",[\"" << JsonEscape(hid) << "\",[],[]],"
					    << inlines_json << "]}";
				} else {
					oss << "{\"t\":\"Header\",\"c\":[" << level << ",[\"" << JsonEscape(hid)
					    << "\",[],[]],[{\"t\":\"Str\",\"c\":\"" << JsonEscape(content) << "\"}]]}";
				}
				j++;
			} else if (child_type == BlockTypes::TYPE_CODE) {
				auto language = GetElementAttribute(child, "language");
				oss << "{\"t\":\"CodeBlock\",\"c\":[[\"\",[\"" << JsonEscape(language) << "\"],[]],\""
				    << JsonEscape(content) << "\"]}";
				j++;
			} else if (child_type == BlockTypes::TYPE_BLOCKQUOTE) {
				if (!inline_children.empty()) {
					string inlines_json = PandocInlineConvert::ConvertInlinesToPandocJson(inline_children);
					oss << "{\"t\":\"BlockQuote\",\"c\":[{\"t\":\"Para\",\"c\":" << inlines_json << "}]}";
				} else if (!content.empty()) {
					oss << "{\"t\":\"BlockQuote\",\"c\":[{\"t\":\"Para\",\"c\":[{\"t\":\"Str\",\"c\":\""
					    << JsonEscape(content) << "\"}]}]}";
				} else {
					oss << "{\"t\":\"BlockQuote\",\"c\":[{\"t\":\"Para\",\"c\":[]}]}";
				}
				j++;
			} else if (child_type == BlockTypes::TYPE_HR) {
				oss << "{\"t\":\"HorizontalRule\"}";
				j++;
			} else if (child_type == BlockTypes::TYPE_LIST) {
				oss << ConvertListToPandocJson(blocks_list, j, child_level);
			} else if (child_type == BlockTypes::TYPE_DIV) {
				oss << ConvertDivToPandocJson(blocks_list, j, child_level);
			} else {
				// Unknown block - skip
				j++;
			}
		} else {
			j++;
		}
	}

	oss << "]]}";
	start_idx = j;
	return oss.str();
}

void PandocBlockConvert::DuckBlocksToPandocBlocksFun(DataChunk &args, ExpressionState &state, Vector &result) {
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
		for (idx_t block_idx = 0; block_idx < blocks_list.size();) {
			auto &block = blocks_list[block_idx];
			if (block.IsNull()) {
				block_idx++;
				continue;
			}

			auto kind = GetElementStringField(block, BlockTypes::KIND_IDX);
			if (kind != BlockTypes::KIND_BLOCK) {
				block_idx++;
				continue; // Skip inlines at this level
			}

			auto element_type = GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX);
			auto content = GetElementStringField(block, BlockTypes::CONTENT_IDX);

			// Skip list_item blocks - they're processed as part of their parent list
			if (element_type == BlockTypes::TYPE_LIST_ITEM) {
				block_idx++;
				continue;
			}

			// Skip metadata blocks - they don't belong in Pandoc's blocks array
			if (element_type == BlockTypes::TYPE_METADATA) {
				block_idx++;
				continue;
			}

			// Collect inline children (elements with kind='inline' following this block)
			vector<Value> inline_children;
			for (idx_t j = block_idx + 1; j < blocks_list.size(); j++) {
				auto &child = blocks_list[j];
				if (child.IsNull())
					continue;
				auto child_kind = GetElementStringField(child, BlockTypes::KIND_IDX);
				if (child_kind == BlockTypes::KIND_BLOCK)
					break; // Stop at next block
				if (child_kind == BlockTypes::KIND_INLINE) {
					inline_children.push_back(child);
				}
			}

			if (!first)
				oss << ",";
			first = false;

			if (element_type == BlockTypes::TYPE_HEADING) {
				auto level_str = GetElementAttribute(block, "heading_level");
				int level = level_str.empty() ? 1 : std::stoi(level_str);
				auto id = GetElementAttribute(block, "id");
				// If has inline children, convert them; otherwise use content
				if (!inline_children.empty()) {
					string inlines_json = PandocInlineConvert::ConvertInlinesToPandocJson(inline_children);
					oss << "{\"t\":\"Header\",\"c\":[" << level << ",[\"" << JsonEscape(id) << "\",[],[]],"
					    << inlines_json << "]}";
				} else {
					oss << "{\"t\":\"Header\",\"c\":[" << level << ",[\"" << JsonEscape(id)
					    << "\",[],[]],[{\"t\":\"Str\",\"c\":\"" << JsonEscape(content) << "\"}]]}";
				}
				block_idx++;
			} else if (element_type == BlockTypes::TYPE_PARAGRAPH) {
				// If has inline children, convert them; otherwise use content
				if (!inline_children.empty()) {
					string inlines_json = PandocInlineConvert::ConvertInlinesToPandocJson(inline_children);
					oss << "{\"t\":\"Para\",\"c\":" << inlines_json << "}";
				} else {
					oss << "{\"t\":\"Para\",\"c\":[{\"t\":\"Str\",\"c\":\"" << JsonEscape(content) << "\"}]}";
				}
				block_idx++;
			} else if (element_type == BlockTypes::TYPE_CODE) {
				auto language = GetElementAttribute(block, "language");
				oss << "{\"t\":\"CodeBlock\",\"c\":[[\"\",[\"" << JsonEscape(language) << "\"],[]],\""
				    << JsonEscape(content) << "\"]}";
				block_idx++;
			} else if (element_type == BlockTypes::TYPE_BLOCKQUOTE) {
				// BlockQuote contains blocks; use inline children to build Para content
				if (!inline_children.empty()) {
					string inlines_json = PandocInlineConvert::ConvertInlinesToPandocJson(inline_children);
					oss << "{\"t\":\"BlockQuote\",\"c\":[{\"t\":\"Para\",\"c\":" << inlines_json << "}]}";
				} else if (!content.empty()) {
					oss << "{\"t\":\"BlockQuote\",\"c\":[{\"t\":\"Para\",\"c\":[{\"t\":\"Str\",\"c\":\""
					    << JsonEscape(content) << "\"}]}]}";
				} else {
					oss << "{\"t\":\"BlockQuote\",\"c\":[{\"t\":\"Para\",\"c\":[]}]}";
				}
				block_idx++;
			} else if (element_type == BlockTypes::TYPE_HR) {
				oss << "{\"t\":\"HorizontalRule\"}";
				block_idx++;
			} else if (element_type == BlockTypes::TYPE_RAW) {
				auto format = GetElementAttribute(block, "format");
				if (format.empty())
					format = "html";
				oss << "{\"t\":\"RawBlock\",\"c\":[\"" << JsonEscape(format) << "\",\"" << JsonEscape(content)
				    << "\"]}";
				block_idx++;
			} else if (element_type == BlockTypes::TYPE_LIST) {
				// Use recursive list converter that handles nested lists properly
				int32_t list_level = GetElementLevel(block);
				oss << ConvertListToPandocJson(blocks_list, block_idx, list_level);
				// block_idx is advanced by ConvertListToPandocJson
			} else if (element_type == BlockTypes::TYPE_DIV) {
				// Use recursive div converter
				int32_t div_level = GetElementLevel(block);
				oss << ConvertDivToPandocJson(blocks_list, block_idx, div_level);
				// block_idx is advanced by ConvertDivToPandocJson
			} else if (element_type == BlockTypes::TYPE_TABLE) {
				// Table content is stored as JSON - output directly
				oss << "{\"t\":\"Table\",\"c\":" << content << "}";
				block_idx++;
			} else if (element_type == BlockTypes::TYPE_IMAGE) {
				// Image is an inline element - wrap in Para
				// Image format: {"t":"Image","c":[["",[]],[alt_inlines],["url","title"]]}
				auto src = GetElementAttribute(block, "src");
				auto alt = GetElementAttribute(block, "alt");
				auto title = GetElementAttribute(block, "title");
				oss << "{\"t\":\"Para\",\"c\":[{\"t\":\"Image\",\"c\":[[\"\",[],[]],[{\"t\":\"Str\",\"c\":\""
				    << JsonEscape(alt) << "\"}],[\"" << JsonEscape(src) << "\",\"" << JsonEscape(title) << "\"]]}]}";
				block_idx++;
			} else {
				// Unknown - output as paragraph
				oss << "{\"t\":\"Para\",\"c\":[{\"t\":\"Str\",\"c\":\"" << JsonEscape(content) << "\"}]}";
				block_idx++;
			}
		}

		oss << "]";
		result.SetValue(i, Value(oss.str()));
	}
}

void PandocBlockConvert::ReadPandocAstFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &path_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto path_val = path_vec.GetValue(i);

		// Handle NULL input
		if (path_val.IsNull()) {
			result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), vector<Value>()));
			continue;
		}

		string file_path = path_val.GetValue<string>();

		// Read file content using standard C++ file I/O
		std::ifstream file(file_path);
		if (!file.is_open()) {
			throw IOException("Could not open file: " + file_path);
		}

		std::stringstream buffer;
		buffer << file.rdbuf();
		string json = buffer.str();

		vector<Value> blocks;
		ConvertPandocAstToBlocks(json, blocks);

		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(blocks)));
	}
}

// Helper to build blocks JSON array from duck_blocks
static string BuildBlocksJson(const vector<Value> &blocks_list) {
	std::ostringstream oss;
	oss << "[";
	bool first = true;

	for (idx_t block_idx = 0; block_idx < blocks_list.size();) {
		auto &block = blocks_list[block_idx];
		if (block.IsNull()) {
			block_idx++;
			continue;
		}

		auto kind = GetElementStringField(block, BlockTypes::KIND_IDX);
		if (kind != BlockTypes::KIND_BLOCK) {
			block_idx++;
			continue;
		}

		auto element_type = GetElementStringField(block, BlockTypes::ELEMENT_TYPE_IDX);
		auto content = GetElementStringField(block, BlockTypes::CONTENT_IDX);

		if (element_type == BlockTypes::TYPE_LIST_ITEM) {
			block_idx++;
			continue;
		}

		// Skip metadata blocks - they don't belong in Pandoc's blocks array
		if (element_type == BlockTypes::TYPE_METADATA) {
			block_idx++;
			continue;
		}

		vector<Value> inline_children;
		for (idx_t j = block_idx + 1; j < blocks_list.size(); j++) {
			auto &child = blocks_list[j];
			if (child.IsNull())
				continue;
			auto child_kind = GetElementStringField(child, BlockTypes::KIND_IDX);
			if (child_kind == BlockTypes::KIND_BLOCK)
				break;
			if (child_kind == BlockTypes::KIND_INLINE) {
				inline_children.push_back(child);
			}
		}

		if (!first)
			oss << ",";
		first = false;

		if (element_type == BlockTypes::TYPE_HEADING) {
			auto level_str = GetElementAttribute(block, "heading_level");
			int level = level_str.empty() ? 1 : std::stoi(level_str);
			auto id = GetElementAttribute(block, "id");
			if (!inline_children.empty()) {
				string inlines_json = PandocInlineConvert::ConvertInlinesToPandocJson(inline_children);
				oss << "{\"t\":\"Header\",\"c\":[" << level << ",[\"" << JsonEscape(id) << "\",[],[]]," << inlines_json
				    << "]}";
			} else {
				oss << "{\"t\":\"Header\",\"c\":[" << level << ",[\"" << JsonEscape(id)
				    << "\",[],[]],[{\"t\":\"Str\",\"c\":\"" << JsonEscape(content) << "\"}]]}";
			}
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_PARAGRAPH) {
			if (!inline_children.empty()) {
				string inlines_json = PandocInlineConvert::ConvertInlinesToPandocJson(inline_children);
				oss << "{\"t\":\"Para\",\"c\":" << inlines_json << "}";
			} else {
				oss << "{\"t\":\"Para\",\"c\":[{\"t\":\"Str\",\"c\":\"" << JsonEscape(content) << "\"}]}";
			}
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_CODE) {
			auto language = GetElementAttribute(block, "language");
			oss << "{\"t\":\"CodeBlock\",\"c\":[[\"\",[\"" << JsonEscape(language) << "\"],[]],\""
			    << JsonEscape(content) << "\"]}";
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_BLOCKQUOTE) {
			// BlockQuote contains blocks; use inline children to build Para content
			if (!inline_children.empty()) {
				string inlines_json = PandocInlineConvert::ConvertInlinesToPandocJson(inline_children);
				oss << "{\"t\":\"BlockQuote\",\"c\":[{\"t\":\"Para\",\"c\":" << inlines_json << "}]}";
			} else if (!content.empty()) {
				oss << "{\"t\":\"BlockQuote\",\"c\":[{\"t\":\"Para\",\"c\":[{\"t\":\"Str\",\"c\":\""
				    << JsonEscape(content) << "\"}]}]}";
			} else {
				oss << "{\"t\":\"BlockQuote\",\"c\":[{\"t\":\"Para\",\"c\":[]}]}";
			}
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_HR) {
			oss << "{\"t\":\"HorizontalRule\"}";
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_RAW) {
			auto format = GetElementAttribute(block, "format");
			if (format.empty())
				format = "html";
			oss << "{\"t\":\"RawBlock\",\"c\":[\"" << JsonEscape(format) << "\",\"" << JsonEscape(content) << "\"]}";
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_LIST) {
			// Use recursive list converter that handles nested lists properly
			int32_t list_level = GetElementLevel(block);
			oss << ConvertListToPandocJson(blocks_list, block_idx, list_level);
			// block_idx is advanced by ConvertListToPandocJson
		} else if (element_type == BlockTypes::TYPE_DIV) {
			// Use recursive div converter
			int32_t div_level = GetElementLevel(block);
			oss << ConvertDivToPandocJson(blocks_list, block_idx, div_level);
			// block_idx is advanced by ConvertDivToPandocJson
		} else if (element_type == BlockTypes::TYPE_TABLE) {
			// Table content is stored as JSON - output directly
			oss << "{\"t\":\"Table\",\"c\":" << content << "}";
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_IMAGE) {
			// Image is an inline element - wrap in Para
			auto src = GetElementAttribute(block, "src");
			auto alt = GetElementAttribute(block, "alt");
			auto title = GetElementAttribute(block, "title");
			oss << "{\"t\":\"Para\",\"c\":[{\"t\":\"Image\",\"c\":[[\"\",[],[]],[{\"t\":\"Str\",\"c\":\""
			    << JsonEscape(alt) << "\"}],[\"" << JsonEscape(src) << "\",\"" << JsonEscape(title) << "\"]]}]}";
			block_idx++;
		} else {
			oss << "{\"t\":\"Para\",\"c\":[{\"t\":\"Str\",\"c\":\"" << JsonEscape(content) << "\"}]}";
			block_idx++;
		}
	}
	oss << "]";
	return oss.str();
}

// Get the Pandoc AST struct type
static LogicalType GetPandocAstType() {
	child_list_t<LogicalType> struct_children;
	struct_children.push_back(make_pair("pandoc-api-version", LogicalType::LIST(LogicalType::INTEGER)));
	struct_children.push_back(make_pair("meta", LogicalType::JSON()));
	struct_children.push_back(make_pair("blocks", LogicalType::JSON()));
	return LogicalType::STRUCT(std::move(struct_children));
}

// duck_blocks_to_pandoc_ast - creates complete Pandoc AST as a struct
static void DuckBlocksToPandocAstFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		// Build API version list
		vector<Value> api_version_vals = {Value::INTEGER(1), Value::INTEGER(20)};
		Value api_version = Value::LIST(LogicalType::INTEGER, api_version_vals);

		// Empty meta
		Value meta = Value("{}");

		if (blocks_val.IsNull()) {
			// Empty blocks
			child_list_t<Value> struct_values;
			struct_values.push_back(make_pair("pandoc-api-version", api_version));
			struct_values.push_back(make_pair("meta", meta));
			struct_values.push_back(make_pair("blocks", Value("[]")));
			result.SetValue(i, Value::STRUCT(std::move(struct_values)));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);

		// Build blocks JSON using helper
		string blocks_json = BuildBlocksJson(blocks_list);

		// Create struct
		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("pandoc-api-version", api_version));
		struct_values.push_back(make_pair("meta", meta));
		struct_values.push_back(make_pair("blocks", Value(blocks_json)));
		result.SetValue(i, Value::STRUCT(std::move(struct_values)));
	}
}

// ============================================================================
// Table function: pandoc_ast(blocks, meta := {}, api_version := [1,20])
// Returns a single row with Pandoc AST fields for clean JSON output
// meta is a MAP(VARCHAR, VARCHAR) that gets converted to Pandoc MetaInlines format
// ============================================================================

struct PandocAstBindData : public TableFunctionData {
	vector<Value> blocks;
	string meta_json = "{}";
	vector<int32_t> api_version = {1, 20};
	bool done = false;
};

// Convert a simple string value to Pandoc MetaInlines format
// e.g., "My Title" -> {"t":"MetaInlines","c":[{"t":"Str","c":"My Title"}]}
static string ConvertToMetaInlines(const string &value) {
	std::ostringstream oss;
	oss << "{\"t\":\"MetaInlines\",\"c\":[{\"t\":\"Str\",\"c\":\"" << JsonEscape(value) << "\"}]}";
	return oss.str();
}

// Convert MAP(VARCHAR, VARCHAR) to Pandoc meta JSON
static string ConvertMetaMapToJson(const Value &meta_map) {
	if (meta_map.IsNull()) {
		return "{}";
	}

	auto &map_entries = MapValue::GetChildren(meta_map);
	if (map_entries.empty()) {
		return "{}";
	}

	std::ostringstream oss;
	oss << "{";
	bool first = true;
	for (auto &entry : map_entries) {
		if (entry.IsNull())
			continue;
		auto &kv = StructValue::GetChildren(entry);
		if (kv.size() < 2 || kv[0].IsNull() || kv[1].IsNull())
			continue;

		string key = kv[0].GetValue<string>();
		string value = kv[1].GetValue<string>();

		if (!first)
			oss << ",";
		first = false;

		oss << "\"" << JsonEscape(key) << "\":" << ConvertToMetaInlines(value);
	}
	oss << "}";
	return oss.str();
}

static unique_ptr<FunctionData> PandocAstBind(ClientContext &context, TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<PandocAstBindData>();

	// Get the blocks parameter (first positional argument)
	if (!input.inputs.empty() && !input.inputs[0].IsNull()) {
		auto &blocks_val = input.inputs[0];
		auto &blocks_list = ListValue::GetChildren(blocks_val);
		for (auto &block : blocks_list) {
			result->blocks.push_back(block);
		}
	}

	// Process named parameters
	for (auto &kv : input.named_parameters) {
		if (kv.first == "meta") {
			if (!kv.second.IsNull()) {
				result->meta_json = ConvertMetaMapToJson(kv.second);
			}
		} else if (kv.first == "api_version") {
			if (!kv.second.IsNull()) {
				auto &version_list = ListValue::GetChildren(kv.second);
				result->api_version.clear();
				for (auto &v : version_list) {
					result->api_version.push_back(v.GetValue<int32_t>());
				}
			}
		}
	}

	// Define output columns with Pandoc-compatible names
	names.push_back("pandoc-api-version");
	return_types.push_back(LogicalType::LIST(LogicalType::INTEGER));

	names.push_back("meta");
	return_types.push_back(LogicalType::JSON());

	names.push_back("blocks");
	return_types.push_back(LogicalType::JSON());

	return std::move(result);
}

static void PandocAstFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &bind_data = data_p.bind_data->CastNoConst<PandocAstBindData>();

	if (bind_data.done) {
		return;
	}

	// Build API version from bind data
	vector<Value> api_version_vals;
	for (auto v : bind_data.api_version) {
		api_version_vals.push_back(Value::INTEGER(v));
	}
	Value api_version = Value::LIST(LogicalType::INTEGER, api_version_vals);

	// Build blocks JSON
	string blocks_json = BuildBlocksJson(bind_data.blocks);

	// Output single row
	CompatSetOutputCardinality(output, 1);
	output.data[0].SetValue(0, api_version);
	output.data[1].SetValue(0, Value(bind_data.meta_json));
	output.data[2].SetValue(0, Value(blocks_json));

	bind_data.done = true;
}

// write_pandoc_ast - writes duck_blocks to a file as Pandoc JSON AST
static void WritePandocAstFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &path_vec = args.data[0];
	auto &blocks_vec = args.data[1];
	auto count = args.size();

	string api_version = "[1,20]";

	for (idx_t i = 0; i < count; i++) {
		auto path_val = path_vec.GetValue(i);
		auto blocks_val = blocks_vec.GetValue(i);

		if (path_val.IsNull()) {
			result.SetValue(i, Value(false));
			continue;
		}

		string file_path = path_val.GetValue<string>();
		std::ofstream file(file_path);
		if (!file.is_open()) {
			throw IOException("Could not open file for writing: " + file_path);
		}

		if (blocks_val.IsNull()) {
			file << "{\"pandoc-api-version\":" << api_version << ",\"meta\":{},\"blocks\":[]}";
			file.close();
			result.SetValue(i, Value(true));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);
		// Use BuildBlocksJson helper which handles nested lists properly
		string blocks_json = BuildBlocksJson(blocks_list);

		file << "{\"pandoc-api-version\":" << api_version << ",\"meta\":{},\"blocks\":" << blocks_json << "}";
		file.close();
		result.SetValue(i, Value(true));
	}
}

void PandocBlockConvert::Register(ExtensionLoader &loader) {
	auto duck_block_list_type = BlockTypes::DuckBlockListType();

	// pandoc_ast_to_blocks(json VARCHAR) -> LIST(duck_block)
	auto ast_to_blocks_func =
	    ScalarFunction("pandoc_ast_to_blocks", {LogicalType::VARCHAR}, duck_block_list_type, PandocAstToBlocksFun);
	loader.RegisterFunction(ast_to_blocks_func);

	// duck_blocks_to_pandoc_blocks(blocks LIST(duck_block)) -> VARCHAR (JSON array of Pandoc blocks)
	auto blocks_to_ast_func = ScalarFunction("duck_blocks_to_pandoc_blocks", {duck_block_list_type},
	                                         LogicalType::VARCHAR, DuckBlocksToPandocBlocksFun);
	loader.RegisterFunction(blocks_to_ast_func);

	// read_pandoc_ast(file_path VARCHAR) -> LIST(duck_block)
	auto read_pandoc_ast_func =
	    ScalarFunction("read_pandoc_ast", {LogicalType::VARCHAR}, duck_block_list_type, ReadPandocAstFun);
	loader.RegisterFunction(read_pandoc_ast_func);

	// duck_blocks_to_pandoc_ast(blocks LIST(duck_block)) -> STRUCT(pandoc-api-version, meta, blocks)
	// Creates complete Pandoc AST as a struct for proper JSON serialization
	auto duck_blocks_to_ast_func = ScalarFunction("duck_blocks_to_pandoc_ast", {duck_block_list_type},
	                                              GetPandocAstType(), DuckBlocksToPandocAstFun);
	loader.RegisterFunction(duck_blocks_to_ast_func);

	// write_pandoc_ast(file_path VARCHAR, blocks LIST(duck_block)) -> BOOLEAN
	// Writes duck_blocks directly to a file as Pandoc JSON AST
	auto write_pandoc_ast_func = ScalarFunction("write_pandoc_ast", {LogicalType::VARCHAR, duck_block_list_type},
	                                            LogicalType::BOOLEAN, WritePandocAstFun);
	loader.RegisterFunction(write_pandoc_ast_func);

	// pandoc_ast(blocks, meta := {}, api_version := [1,20]) -> TABLE(pandoc-api-version, meta, blocks)
	// Table function for clean JSON output with COPY FORMAT JSON
	// meta is MAP(VARCHAR, VARCHAR) - simple key-value pairs converted to Pandoc MetaInlines
	TableFunction pandoc_ast_table_func("pandoc_ast", {duck_block_list_type}, PandocAstFunction, PandocAstBind);
	pandoc_ast_table_func.named_parameters["meta"] = LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR);
	pandoc_ast_table_func.named_parameters["api_version"] = LogicalType::LIST(LogicalType::INTEGER);
	loader.RegisterFunction(pandoc_ast_table_func);
}

} // namespace duckdb
