#include "pandoc_inline_convert.hpp"
#include "pandoc_convert_util.hpp"
#include "block_types.hpp"
#include "inline_builders.hpp"
#include "duckdb/common/types/value.hpp"

#include <sstream>

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

// Helper to create a single duck_block Value (for inline)
static Value CreateDocInline(const string &inline_type, const string &content, int32_t level,
                             const map<string, string> &attrs, int32_t order) {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_INLINE)));
	struct_values.push_back(make_pair("element_type", Value(inline_type)));
	struct_values.push_back(make_pair("content", Value(content)));
	struct_values.push_back(make_pair("level", Value(level)));
	struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_TEXT)));
	struct_values.push_back(make_pair("attributes", CreateAttrsMap(attrs)));
	struct_values.push_back(make_pair("element_order", Value(order)));
	return Value::STRUCT(std::move(struct_values));
}

// Simple JSON string parser helpers (DuckDB has JSON extension but we keep it simple)
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

// Split a Pandoc [Attr, [inlines], Target] content array (Link, Image) into its
// inline array and its target array.
//
// The previous hand-rolled version walked three "find the next [" steps, which
// landed on the *classes* array nested inside Attr rather than on the inline
// array, so a Link's text was silently dropped (it only survived the block path
// because that path scanned the raw JSON for Str tokens). Bracket matching here
// is quote-aware, so brackets inside document text cannot unbalance the scan.
static bool SplitAttrInlinesTarget(const string &obj_content, string &inlines, string &target) {
	size_t c_pos = obj_content.find("\"c\":");
	if (c_pos == string::npos) {
		return false;
	}
	size_t outer = obj_content.find("[", c_pos);
	if (outer == string::npos) {
		return false;
	}
	// Element 1: the Attr triple [id, [classes], [[k,v]...]]
	size_t attr_start = obj_content.find("[", outer + 1);
	if (attr_start == string::npos) {
		return false;
	}
	size_t attr_end = PandocFindMatchingBracket(obj_content, attr_start, '[', ']');
	// Element 2: the inline array
	size_t inl_start = obj_content.find("[", attr_end);
	if (inl_start == string::npos) {
		return false;
	}
	size_t inl_end = PandocFindMatchingBracket(obj_content, inl_start, '[', ']');
	inlines = obj_content.substr(inl_start, inl_end - inl_start);
	// Element 3: the Target pair [url, title] (absent for Span)
	size_t tgt_start = obj_content.find("[", inl_end);
	if (tgt_start != string::npos) {
		size_t tgt_end = PandocFindMatchingBracket(obj_content, tgt_start, '[', ']');
		target = obj_content.substr(tgt_start, tgt_end - tgt_start);
	}
	return true;
}

// First JSON string literal in `json`, unescaped enough for a URL
static string FirstJsonString(const string &json) {
	size_t start = json.find("\"");
	if (start == string::npos) {
		return "";
	}
	start++;
	string out;
	for (size_t i = start; i < json.length(); i++) {
		if (json[i] == '\\' && i + 1 < json.length()) {
			i++;
			out += json[i];
			continue;
		}
		if (json[i] == '"') {
			break;
		}
		out += json[i];
	}
	return out;
}

// Plain text of already-flattened inline Values (used for an image's alt text,
// which duck_blocks carries as a string, not as inline children)
static string InlineValuesToText(const vector<Value> &inlines) {
	string out;
	for (auto &el : inlines) {
		if (el.IsNull()) {
			continue;
		}
		auto &ch = StructValue::GetChildren(el);
		string element_type =
		    ch[BlockTypes::ELEMENT_TYPE_IDX].IsNull() ? "" : ch[BlockTypes::ELEMENT_TYPE_IDX].GetValue<string>();
		string content = ch[BlockTypes::CONTENT_IDX].IsNull() ? "" : ch[BlockTypes::CONTENT_IDX].GetValue<string>();
		if (element_type == BlockTypes::INLINE_SPACE || element_type == BlockTypes::INLINE_SOFTBREAK) {
			out += " ";
		} else if (element_type == BlockTypes::INLINE_LINEBREAK) {
			out += "\n";
		} else {
			out += content;
		}
	}
	return out;
}

// Recursively flatten Pandoc inlines.
// `depth` is the nesting depth (1 at the top level); input nested deeper than
// PANDOC_MAX_NESTING_DEPTH is rejected with a clean error so a deeply nested
// document cannot exhaust the call stack.
static void FlattenPandocInlines(const string &json, int32_t level, int32_t &order, vector<Value> &result,
                                 idx_t depth) {
	CheckPandocDepth(depth);
	// Find array elements - simplified parser for Pandoc inline format
	// Each inline is {"t": "Type", "c": content} or just {"t": "Type"}

	size_t pos = 0;
	while (pos < json.length()) {
		// Find next object
		size_t obj_start = json.find("{\"t\":", pos);
		if (obj_start == string::npos)
			break;

		// Find the type
		size_t type_start = json.find("\"", obj_start + 5);
		if (type_start == string::npos)
			break;
		type_start++;

		size_t type_end = json.find("\"", type_start);
		if (type_end == string::npos)
			break;

		string pandoc_type = json.substr(type_start, type_end - type_start);

		// Find the content if any
		string content_str = "";
		size_t c_pos = json.find("\"c\":", type_end);

		// Find end of this object (matching braces)
		int brace_count = 1;
		size_t obj_end = obj_start + 1;
		while (obj_end < json.length() && brace_count > 0) {
			if (json[obj_end] == '{')
				brace_count++;
			else if (json[obj_end] == '}')
				brace_count--;
			obj_end++;
		}

		string obj_content = json.substr(obj_start, obj_end - obj_start);

		// Map Pandoc type to our inline type
		string inline_type;
		map<string, string> attrs;

		if (pandoc_type == "Str") {
			inline_type = BlockTypes::INLINE_TEXT;
			content_str = ExtractJsonString(obj_content, "c");
		} else if (pandoc_type == "Space") {
			inline_type = BlockTypes::INLINE_SPACE;
			content_str = " ";
		} else if (pandoc_type == "SoftBreak") {
			inline_type = BlockTypes::INLINE_SOFTBREAK;
			content_str = "";
		} else if (pandoc_type == "LineBreak") {
			inline_type = BlockTypes::INLINE_LINEBREAK;
			content_str = "\n";
		} else if (pandoc_type == "Strong") {
			inline_type = BlockTypes::INLINE_BOLD;
			// Add container, then recurse for children
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));

			// Find children array in "c"
			size_t c_start = obj_content.find("\"c\":");
			if (c_start != string::npos) {
				size_t arr_start = obj_content.find("[", c_start);
				size_t arr_end = obj_content.rfind("]");
				if (arr_start != string::npos && arr_end != string::npos) {
					string children = obj_content.substr(arr_start, arr_end - arr_start + 1);
					FlattenPandocInlines(children, level + 1, order, result, depth + 1);
				}
			}
			pos = obj_end;
			continue;
		} else if (pandoc_type == "Emph") {
			inline_type = BlockTypes::INLINE_ITALIC;
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));

			size_t c_start = obj_content.find("\"c\":");
			if (c_start != string::npos) {
				size_t arr_start = obj_content.find("[", c_start);
				size_t arr_end = obj_content.rfind("]");
				if (arr_start != string::npos && arr_end != string::npos) {
					string children = obj_content.substr(arr_start, arr_end - arr_start + 1);
					FlattenPandocInlines(children, level + 1, order, result, depth + 1);
				}
			}
			pos = obj_end;
			continue;
		} else if (pandoc_type == "Strikeout") {
			inline_type = BlockTypes::INLINE_STRIKETHROUGH;
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));

			size_t c_start = obj_content.find("\"c\":");
			if (c_start != string::npos) {
				size_t arr_start = obj_content.find("[", c_start);
				size_t arr_end = obj_content.rfind("]");
				if (arr_start != string::npos && arr_end != string::npos) {
					string children = obj_content.substr(arr_start, arr_end - arr_start + 1);
					FlattenPandocInlines(children, level + 1, order, result, depth + 1);
				}
			}
			pos = obj_end;
			continue;
		} else if (pandoc_type == "Superscript") {
			inline_type = BlockTypes::INLINE_SUPERSCRIPT;
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));

			size_t c_start = obj_content.find("\"c\":");
			if (c_start != string::npos) {
				size_t arr_start = obj_content.find("[", c_start);
				size_t arr_end = obj_content.rfind("]");
				if (arr_start != string::npos && arr_end != string::npos) {
					string children = obj_content.substr(arr_start, arr_end - arr_start + 1);
					FlattenPandocInlines(children, level + 1, order, result, depth + 1);
				}
			}
			pos = obj_end;
			continue;
		} else if (pandoc_type == "Subscript") {
			inline_type = BlockTypes::INLINE_SUBSCRIPT;
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));

			size_t c_start = obj_content.find("\"c\":");
			if (c_start != string::npos) {
				size_t arr_start = obj_content.find("[", c_start);
				size_t arr_end = obj_content.rfind("]");
				if (arr_start != string::npos && arr_end != string::npos) {
					string children = obj_content.substr(arr_start, arr_end - arr_start + 1);
					FlattenPandocInlines(children, level + 1, order, result, depth + 1);
				}
			}
			pos = obj_end;
			continue;
		} else if (pandoc_type == "SmallCaps") {
			inline_type = BlockTypes::INLINE_SMALLCAPS;
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));

			size_t c_start = obj_content.find("\"c\":");
			if (c_start != string::npos) {
				size_t arr_start = obj_content.find("[", c_start);
				size_t arr_end = obj_content.rfind("]");
				if (arr_start != string::npos && arr_end != string::npos) {
					string children = obj_content.substr(arr_start, arr_end - arr_start + 1);
					FlattenPandocInlines(children, level + 1, order, result, depth + 1);
				}
			}
			pos = obj_end;
			continue;
		} else if (pandoc_type == "Code") {
			inline_type = BlockTypes::INLINE_CODE;
			// Code has [Attr, string] format - extract the string (second element)
			// Find the string after attributes
			size_t c_start = obj_content.find("\"c\":");
			if (c_start != string::npos) {
				// Skip to second element (the code string)
				size_t first_bracket = obj_content.find("[", c_start);
				if (first_bracket != string::npos) {
					// Find the code string - it's after the attr array
					size_t attr_end = obj_content.find("],", first_bracket);
					if (attr_end != string::npos) {
						size_t str_start = obj_content.find("\"", attr_end);
						if (str_start != string::npos) {
							str_start++;
							size_t str_end = obj_content.find("\"", str_start);
							while (str_end != string::npos && str_end > 0 && obj_content[str_end - 1] == '\\') {
								str_end = obj_content.find("\"", str_end + 1);
							}
							if (str_end != string::npos) {
								content_str = obj_content.substr(str_start, str_end - str_start);
							}
						}
					}
				}
			}
		} else if (pandoc_type == "Math") {
			inline_type = BlockTypes::INLINE_MATH;
			// Math has [MathType, string] - extract string
			size_t c_start = obj_content.find("\"c\":");
			if (c_start != string::npos) {
				// Check for InlineMath vs DisplayMath
				if (obj_content.find("InlineMath") != string::npos) {
					attrs["display"] = "inline";
				} else {
					attrs["display"] = "block";
				}
				// Find the math string
				size_t last_quote = obj_content.rfind("\"");
				if (last_quote != string::npos && last_quote > 0) {
					size_t str_start = obj_content.rfind("\"", last_quote - 1);
					if (str_start != string::npos) {
						content_str = obj_content.substr(str_start + 1, last_quote - str_start - 1);
					}
				}
			}
		} else if (pandoc_type == "Link") {
			inline_type = BlockTypes::INLINE_LINK;
			// Link has [Attr, [inlines], Target] where Target is [url, title]
			string link_inlines, link_target;
			if (SplitAttrInlinesTarget(obj_content, link_inlines, link_target)) {
				if (!link_target.empty()) {
					attrs["href"] = FirstJsonString(link_target);
				}
			}
			// Container first, then its text as deeper-level children
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));
			if (!link_inlines.empty()) {
				FlattenPandocInlines(link_inlines, level + 1, order, result, depth + 1);
			}
			pos = obj_end;
			continue;
		} else if (pandoc_type == "Image") {
			inline_type = BlockTypes::INLINE_IMAGE;
			// Image has [Attr, [alt inlines], Target]. duck_blocks carry alt text
			// as a string (both `content` and the `alt` attribute, matching
			// db_inline_image), not as inline children, so flatten it here --
			// otherwise the alt text is dropped outright.
			string alt_inlines, img_target;
			string alt_text;
			if (SplitAttrInlinesTarget(obj_content, alt_inlines, img_target)) {
				if (!img_target.empty()) {
					attrs["src"] = FirstJsonString(img_target);
				}
				if (!alt_inlines.empty()) {
					vector<Value> alt_values;
					int32_t alt_order = 0;
					FlattenPandocInlines(alt_inlines, level + 1, alt_order, alt_values, depth + 1);
					alt_text = InlineValuesToText(alt_values);
					if (!alt_text.empty()) {
						attrs["alt"] = alt_text;
					}
				}
			}
			result.push_back(CreateDocInline(inline_type, alt_text, level, attrs, order++));
			pos = obj_end;
			continue;
		} else if (pandoc_type == "RawInline") {
			inline_type = BlockTypes::INLINE_RAW;
			// RawInline has [format, string]
			size_t c_start = obj_content.find("\"c\":");
			if (c_start != string::npos) {
				size_t first_str = obj_content.find("\"", c_start + 4);
				if (first_str != string::npos) {
					first_str++;
					size_t first_end = obj_content.find("\"", first_str);
					if (first_end != string::npos) {
						attrs["format"] = obj_content.substr(first_str, first_end - first_str);
						size_t second_str = obj_content.find("\"", first_end + 1);
						if (second_str != string::npos) {
							second_str++;
							size_t second_end = obj_content.find("\"", second_str);
							if (second_end != string::npos) {
								content_str = obj_content.substr(second_str, second_end - second_str);
							}
						}
					}
				}
			}
		} else if (pandoc_type == "Quoted") {
			inline_type = BlockTypes::INLINE_QUOTED;
			if (obj_content.find("SingleQuote") != string::npos) {
				attrs["quote_type"] = "single";
			} else {
				attrs["quote_type"] = "double";
			}
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));

			// Recurse for quoted content
			size_t c_start = obj_content.find("\"c\":");
			if (c_start != string::npos) {
				size_t arr_start = obj_content.find("[", c_start);
				if (arr_start != string::npos) {
					arr_start = obj_content.find("[", arr_start + 1); // Skip quote type
					if (arr_start != string::npos) {
						size_t arr_end = obj_content.rfind("]");
						if (arr_end != string::npos) {
							string children = obj_content.substr(arr_start, arr_end - arr_start + 1);
							FlattenPandocInlines(children, level + 1, order, result, depth + 1);
						}
					}
				}
			}
			pos = obj_end;
			continue;
		} else if (pandoc_type == "Cite") {
			inline_type = BlockTypes::INLINE_CITE;
			// Simplified - just mark as cite
		} else if (pandoc_type == "Note") {
			inline_type = BlockTypes::INLINE_NOTE;
			// Note contains blocks, not inlines - store as marker
		} else if (pandoc_type == "Span") {
			inline_type = BlockTypes::INLINE_SPAN;
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));

			size_t c_start = obj_content.find("\"c\":");
			if (c_start != string::npos) {
				size_t arr_start = obj_content.find("[", c_start);
				if (arr_start != string::npos) {
					arr_start = obj_content.find("[", arr_start + 1); // Skip attr
					if (arr_start != string::npos) {
						arr_start = obj_content.find("[", arr_start + 1); // Find inlines
						if (arr_start != string::npos) {
							size_t arr_end = obj_content.rfind("]");
							if (arr_end != string::npos) {
								string children = obj_content.substr(arr_start, arr_end - arr_start + 1);
								FlattenPandocInlines(children, level + 1, order, result, depth + 1);
							}
						}
					}
				}
			}
			pos = obj_end;
			continue;
		} else {
			// Unknown type - treat as text with the type name
			inline_type = BlockTypes::INLINE_TEXT;
			content_str = "[" + pandoc_type + "]";
		}

		if (!inline_type.empty()) {
			result.push_back(CreateDocInline(inline_type, content_str, level, attrs, order++));
		}

		pos = obj_end;
	}
}

void PandocInlineConvert::ConvertPandocInlinesToDbInlines(const string &json, int32_t base_level, int32_t &order,
                                                          vector<Value> &result, idx_t depth) {
	FlattenPandocInlines(json, base_level, order, result, depth);
}

void PandocInlineConvert::PandocInlinesToDbInlinesFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &json_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto json_val = json_vec.GetValue(i);

		if (json_val.IsNull()) {
			result.SetValue(i, Value(LogicalType::LIST(BlockTypes::DuckBlockType())));
			continue;
		}

		string json = json_val.GetValue<string>();
		vector<Value> inlines;
		int32_t order = 0;

		FlattenPandocInlines(json, 1, order, inlines, 1);

		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), inlines));
	}
}

// Convert doc_inlines back to Pandoc JSON.
// `depth` bounds the recursion into nested container inlines so a deeply
// nested inline list cannot exhaust the call stack.
static string DocInlinesToPandocJson(const vector<Value> &inlines, idx_t start_idx, int32_t target_level,
                                     idx_t &end_idx, idx_t depth) {
	CheckPandocDepth(depth);
	std::ostringstream json;
	json << "[";

	bool first = true;
	idx_t i = start_idx;

	while (i < inlines.size()) {
		auto &inline_val = inlines[i];
		auto children = StructValue::GetChildren(inline_val);

		string inline_type = children[BlockTypes::ELEMENT_TYPE_IDX].GetValue<string>();
		string content =
		    children[BlockTypes::CONTENT_IDX].IsNull() ? "" : children[BlockTypes::CONTENT_IDX].GetValue<string>();
		int32_t level =
		    children[BlockTypes::LEVEL_IDX].IsNull() ? 1 : children[BlockTypes::LEVEL_IDX].GetValue<int32_t>();

		// If this inline is at a lower level, we've exited the current scope
		if (level < target_level) {
			break;
		}

		// Skip if above target level (shouldn't happen in normal traversal)
		if (level > target_level) {
			i++;
			continue;
		}

		if (!first)
			json << ",";
		first = false;

		// Escape content for JSON (handles all control characters)
		string escaped_content = PandocJsonEscape(content);

		// Map our types back to Pandoc
		if (inline_type == BlockTypes::INLINE_TEXT) {
			json << "{\"t\":\"Str\",\"c\":\"" << escaped_content << "\"}";
		} else if (inline_type == BlockTypes::INLINE_SPACE) {
			json << "{\"t\":\"Space\"}";
		} else if (inline_type == BlockTypes::INLINE_SOFTBREAK) {
			json << "{\"t\":\"SoftBreak\"}";
		} else if (inline_type == BlockTypes::INLINE_LINEBREAK) {
			json << "{\"t\":\"LineBreak\"}";
		} else if (inline_type == BlockTypes::INLINE_BOLD) {
			// Find nested content at level+1
			idx_t nested_end = i + 1;
			string nested = DocInlinesToPandocJson(inlines, i + 1, level + 1, nested_end, depth + 1);
			// If no nested children but has content, create Str from content
			if (nested == "[]" && !content.empty()) {
				json << "{\"t\":\"Strong\",\"c\":[{\"t\":\"Str\",\"c\":\"" << escaped_content << "\"}]}";
			} else {
				json << "{\"t\":\"Strong\",\"c\":" << nested << "}";
			}
			i = nested_end - 1; // Adjust for loop increment
		} else if (inline_type == BlockTypes::INLINE_ITALIC) {
			idx_t nested_end = i + 1;
			string nested = DocInlinesToPandocJson(inlines, i + 1, level + 1, nested_end, depth + 1);
			if (nested == "[]" && !content.empty()) {
				json << "{\"t\":\"Emph\",\"c\":[{\"t\":\"Str\",\"c\":\"" << escaped_content << "\"}]}";
			} else {
				json << "{\"t\":\"Emph\",\"c\":" << nested << "}";
			}
			i = nested_end - 1;
		} else if (inline_type == BlockTypes::INLINE_STRIKETHROUGH) {
			idx_t nested_end = i + 1;
			string nested = DocInlinesToPandocJson(inlines, i + 1, level + 1, nested_end, depth + 1);
			if (nested == "[]" && !content.empty()) {
				json << "{\"t\":\"Strikeout\",\"c\":[{\"t\":\"Str\",\"c\":\"" << escaped_content << "\"}]}";
			} else {
				json << "{\"t\":\"Strikeout\",\"c\":" << nested << "}";
			}
			i = nested_end - 1;
		} else if (inline_type == BlockTypes::INLINE_SUPERSCRIPT) {
			idx_t nested_end = i + 1;
			string nested = DocInlinesToPandocJson(inlines, i + 1, level + 1, nested_end, depth + 1);
			if (nested == "[]" && !content.empty()) {
				json << "{\"t\":\"Superscript\",\"c\":[{\"t\":\"Str\",\"c\":\"" << escaped_content << "\"}]}";
			} else {
				json << "{\"t\":\"Superscript\",\"c\":" << nested << "}";
			}
			i = nested_end - 1;
		} else if (inline_type == BlockTypes::INLINE_SUBSCRIPT) {
			idx_t nested_end = i + 1;
			string nested = DocInlinesToPandocJson(inlines, i + 1, level + 1, nested_end, depth + 1);
			if (nested == "[]" && !content.empty()) {
				json << "{\"t\":\"Subscript\",\"c\":[{\"t\":\"Str\",\"c\":\"" << escaped_content << "\"}]}";
			} else {
				json << "{\"t\":\"Subscript\",\"c\":" << nested << "}";
			}
			i = nested_end - 1;
		} else if (inline_type == BlockTypes::INLINE_SMALLCAPS) {
			idx_t nested_end = i + 1;
			string nested = DocInlinesToPandocJson(inlines, i + 1, level + 1, nested_end, depth + 1);
			if (nested == "[]" && !content.empty()) {
				json << "{\"t\":\"SmallCaps\",\"c\":[{\"t\":\"Str\",\"c\":\"" << escaped_content << "\"}]}";
			} else {
				json << "{\"t\":\"SmallCaps\",\"c\":" << nested << "}";
			}
			i = nested_end - 1;
		} else if (inline_type == BlockTypes::INLINE_CODE) {
			json << "{\"t\":\"Code\",\"c\":[[\"\",[],[]],\"" << escaped_content << "\"]}";
		} else if (inline_type == BlockTypes::INLINE_MATH) {
			auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
			string display = "InlineMath";
			// Check attributes for display mode
			json << "{\"t\":\"Math\",\"c\":[{\"t\":\"" << display << "\"},\"" << escaped_content << "\"]}";
		} else if (inline_type == BlockTypes::INLINE_LINK) {
			auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
			string href = "";
			// Extract href from attributes
			if (!attrs.IsNull()) {
				auto &map_entries = MapValue::GetChildren(attrs);
				for (auto &entry : map_entries) {
					if (entry.IsNull())
						continue;
					auto &kv = StructValue::GetChildren(entry);
					if (kv.size() >= 2 && !kv[0].IsNull() && kv[0].GetValue<string>() == "href") {
						if (!kv[1].IsNull()) {
							href = kv[1].GetValue<string>();
						}
						break;
					}
				}
			}

			idx_t nested_end = i + 1;
			string nested = DocInlinesToPandocJson(inlines, i + 1, level + 1, nested_end, depth + 1);
			// If no nested children but has content, create Str from content
			if (nested == "[]" && !content.empty()) {
				json << "{\"t\":\"Link\",\"c\":[[\"\",[],[]],[{\"t\":\"Str\",\"c\":\"" << escaped_content << "\"}],[\""
				     << href << "\",\"\"]]}";
			} else {
				json << "{\"t\":\"Link\",\"c\":[[\"\",[],[]]," << nested << ",[\"" << href << "\",\"\"]]}";
			}
			i = nested_end - 1;
		} else if (inline_type == BlockTypes::INLINE_IMAGE) {
			auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
			string src = "";
			if (!attrs.IsNull()) {
				auto &map_entries = MapValue::GetChildren(attrs);
				for (auto &entry : map_entries) {
					if (entry.IsNull())
						continue;
					auto &kv = StructValue::GetChildren(entry);
					if (kv.size() >= 2 && !kv[0].IsNull() && kv[0].GetValue<string>() == "src") {
						if (!kv[1].IsNull()) {
							src = kv[1].GetValue<string>();
						}
						break;
					}
				}
			}
			json << "{\"t\":\"Image\",\"c\":[[\"\",[],[]],[],[\"" << src << "\",\"\"]]}";
		} else if (inline_type == BlockTypes::INLINE_RAW) {
			auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
			string format = "html";
			if (!attrs.IsNull()) {
				auto &map_entries = MapValue::GetChildren(attrs);
				for (auto &entry : map_entries) {
					if (entry.IsNull())
						continue;
					auto &kv = StructValue::GetChildren(entry);
					if (kv.size() >= 2 && !kv[0].IsNull() && kv[0].GetValue<string>() == "format") {
						if (!kv[1].IsNull()) {
							format = kv[1].GetValue<string>();
						}
						break;
					}
				}
			}
			json << "{\"t\":\"RawInline\",\"c\":[\"" << format << "\",\"" << escaped_content << "\"]}";
		} else if (inline_type == BlockTypes::INLINE_QUOTED) {
			auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
			string quote_type = "DoubleQuote";
			if (!attrs.IsNull()) {
				auto &map_entries = MapValue::GetChildren(attrs);
				for (auto &entry : map_entries) {
					if (entry.IsNull())
						continue;
					auto &kv = StructValue::GetChildren(entry);
					if (kv.size() >= 2 && !kv[0].IsNull() && kv[0].GetValue<string>() == "quote_type") {
						if (!kv[1].IsNull() && kv[1].GetValue<string>() == "single") {
							quote_type = "SingleQuote";
							break;
						}
					}
				}
			}
			idx_t nested_end = i + 1;
			string nested = DocInlinesToPandocJson(inlines, i + 1, level + 1, nested_end, depth + 1);
			json << "{\"t\":\"Quoted\",\"c\":[{\"t\":\"" << quote_type << "\"}," << nested << "]}";
			i = nested_end - 1;
		} else if (inline_type == BlockTypes::INLINE_SPAN) {
			idx_t nested_end = i + 1;
			string nested = DocInlinesToPandocJson(inlines, i + 1, level + 1, nested_end, depth + 1);
			json << "{\"t\":\"Span\",\"c\":[[\"\",[],[]]," << nested << "]}";
			i = nested_end - 1;
		} else {
			// Unknown - emit as Str
			json << "{\"t\":\"Str\",\"c\":\"[" << inline_type << "]\"}";
		}

		i++;
	}

	end_idx = i;
	json << "]";
	return json.str();
}

void PandocInlineConvert::DbInlinesToPandocFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &list_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto list_val = list_vec.GetValue(i);

		if (list_val.IsNull()) {
			result.SetValue(i, Value("[]"));
			continue;
		}

		auto &inlines = ListValue::GetChildren(list_val);
		idx_t end_idx = 0;
		string json = DocInlinesToPandocJson(inlines, 0, 1, end_idx, 1);
		result.SetValue(i, Value(json));
	}
}

// Public method to convert inline Values to Pandoc JSON
string PandocInlineConvert::ConvertInlinesToPandocJson(const vector<Value> &inlines) {
	if (inlines.empty()) {
		return "[]";
	}
	// Determine the level of the first inline and use that as target level
	auto &first = inlines[0];
	auto first_children = StructValue::GetChildren(first);
	int32_t target_level =
	    first_children[BlockTypes::LEVEL_IDX].IsNull() ? 1 : first_children[BlockTypes::LEVEL_IDX].GetValue<int32_t>();

	idx_t end_idx = 0;
	return DocInlinesToPandocJson(inlines, 0, target_level, end_idx, 1);
}

// Render inlines to text/markdown.
// `depth` bounds the recursion into nested formatting so a deeply nested
// document cannot exhaust the call stack.
static string RenderInlinesToText(const string &json, const string &mode, idx_t depth) {
	CheckPandocDepth(depth);
	std::ostringstream out;

	// Simple rendering - parse the JSON and output text
	size_t pos = 0;
	while (pos < json.length()) {
		size_t obj_start = json.find("{\"t\":", pos);
		if (obj_start == string::npos)
			break;

		size_t type_start = json.find("\"", obj_start + 5);
		if (type_start == string::npos)
			break;
		type_start++;

		size_t type_end = json.find("\"", type_start);
		if (type_end == string::npos)
			break;

		string pandoc_type = json.substr(type_start, type_end - type_start);

		// Find end of object
		int brace_count = 1;
		size_t obj_end = obj_start + 1;
		while (obj_end < json.length() && brace_count > 0) {
			if (json[obj_end] == '{')
				brace_count++;
			else if (json[obj_end] == '}')
				brace_count--;
			obj_end++;
		}

		string obj_content = json.substr(obj_start, obj_end - obj_start);

		if (pandoc_type == "Str") {
			out << ExtractJsonString(obj_content, "c");
		} else if (pandoc_type == "Space") {
			out << " ";
		} else if (pandoc_type == "SoftBreak") {
			out << (mode == "text" ? " " : "\n");
		} else if (pandoc_type == "LineBreak") {
			out << "\n";
		} else if (pandoc_type == "Strong") {
			if (mode == "markdown")
				out << "**";
			// Recurse for children
			size_t c_start = obj_content.find("\"c\":");
			if (c_start != string::npos) {
				size_t arr_start = obj_content.find("[", c_start);
				size_t arr_end = obj_content.rfind("]");
				if (arr_start != string::npos && arr_end != string::npos) {
					out << RenderInlinesToText(obj_content.substr(arr_start, arr_end - arr_start + 1), mode, depth + 1);
				}
			}
			if (mode == "markdown")
				out << "**";
		} else if (pandoc_type == "Emph") {
			if (mode == "markdown")
				out << "*";
			size_t c_start = obj_content.find("\"c\":");
			if (c_start != string::npos) {
				size_t arr_start = obj_content.find("[", c_start);
				size_t arr_end = obj_content.rfind("]");
				if (arr_start != string::npos && arr_end != string::npos) {
					out << RenderInlinesToText(obj_content.substr(arr_start, arr_end - arr_start + 1), mode, depth + 1);
				}
			}
			if (mode == "markdown")
				out << "*";
		} else if (pandoc_type == "Code") {
			if (mode == "markdown")
				out << "`";
			// Extract code content
			size_t c_start = obj_content.find("\"c\":");
			if (c_start != string::npos) {
				size_t attr_end = obj_content.find("],", c_start);
				if (attr_end != string::npos) {
					size_t str_start = obj_content.find("\"", attr_end);
					if (str_start != string::npos) {
						str_start++;
						size_t str_end = obj_content.rfind("\"");
						if (str_end > str_start) {
							out << obj_content.substr(str_start, str_end - str_start);
						}
					}
				}
			}
			if (mode == "markdown")
				out << "`";
		} else if (pandoc_type == "Link") {
			// Extract link text and URL
			size_t c_start = obj_content.find("\"c\":");
			if (c_start != string::npos && mode == "markdown") {
				out << "[";
				// Find text inlines
				size_t arr_start = obj_content.find("[", c_start);
				if (arr_start != string::npos) {
					arr_start = obj_content.find("[", arr_start + 1);
					if (arr_start != string::npos) {
						arr_start = obj_content.find("[", arr_start + 1);
						if (arr_start != string::npos) {
							int bracket_count = 1;
							size_t arr_end = arr_start + 1;
							while (arr_end < obj_content.length() && bracket_count > 0) {
								if (obj_content[arr_end] == '[')
									bracket_count++;
								else if (obj_content[arr_end] == ']')
									bracket_count--;
								arr_end++;
							}
							out << RenderInlinesToText(obj_content.substr(arr_start, arr_end - arr_start), mode,
							                           depth + 1);
						}
					}
				}
				out << "](";
				// Find URL
				size_t last_bracket = obj_content.rfind("[");
				if (last_bracket != string::npos) {
					size_t url_start = obj_content.find("\"", last_bracket);
					if (url_start != string::npos) {
						url_start++;
						size_t url_end = obj_content.find("\"", url_start);
						if (url_end != string::npos) {
							out << obj_content.substr(url_start, url_end - url_start);
						}
					}
				}
				out << ")";
			}
		}

		pos = obj_end;
	}

	return out.str();
}

void PandocInlineConvert::PandocInlinesToTextFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &json_vec = args.data[0];
	auto count = args.size();

	string mode = "markdown";
	if (args.ColumnCount() > 1) {
		auto mode_val = args.data[1].GetValue(0);
		if (!mode_val.IsNull()) {
			mode = mode_val.GetValue<string>();
		}
	}

	for (idx_t i = 0; i < count; i++) {
		auto json_val = json_vec.GetValue(i);

		if (json_val.IsNull()) {
			result.SetValue(i, Value(""));
			continue;
		}

		string json = json_val.GetValue<string>();
		string text = RenderInlinesToText(json, mode, 1);
		result.SetValue(i, Value(text));
	}
}

// Overload that accepts nested lists and flattens them
static void DbInlinesToPandocNestedFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &list_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto list_val = list_vec.GetValue(i);

		if (list_val.IsNull()) {
			result.SetValue(i, Value("[]"));
			continue;
		}

		// Flatten the nested list
		auto &outer_list = ListValue::GetChildren(list_val);
		vector<Value> flattened;
		for (auto &inner : outer_list) {
			if (inner.IsNull())
				continue;
			auto &inner_list = ListValue::GetChildren(inner);
			for (auto &elem : inner_list) {
				flattened.push_back(elem);
			}
		}

		string json = PandocInlineConvert::ConvertInlinesToPandocJson(flattened);
		result.SetValue(i, Value(json));
	}
}

void PandocInlineConvert::Register(ExtensionLoader &loader) {
	auto duck_block_list_type = BlockTypes::DuckBlockListType();
	auto duck_block_nested_list_type = LogicalType::LIST(duck_block_list_type);

	// pandoc_inlines_to_db_inlines(json VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("pandoc_inlines_to_db_inlines", {LogicalType::VARCHAR}, duck_block_list_type,
	                                       PandocInlinesToDbInlinesFun));

	// db_inlines_to_pandoc(LIST(duck_block)) -> VARCHAR (JSON)
	loader.RegisterFunction(
	    ScalarFunction("db_inlines_to_pandoc", {duck_block_list_type}, LogicalType::VARCHAR, DbInlinesToPandocFun));

	// db_inlines_to_pandoc(LIST(LIST(duck_block))) -> VARCHAR (JSON) - auto-flattening
	loader.RegisterFunction(ScalarFunction("db_inlines_to_pandoc", {duck_block_nested_list_type}, LogicalType::VARCHAR,
	                                       DbInlinesToPandocNestedFun));

	// pandoc_inlines_to_text(json VARCHAR) -> VARCHAR
	loader.RegisterFunction(
	    ScalarFunction("pandoc_inlines_to_text", {LogicalType::VARCHAR}, LogicalType::VARCHAR, PandocInlinesToTextFun));

	// pandoc_inlines_to_text(json VARCHAR, mode VARCHAR) -> VARCHAR
	loader.RegisterFunction(ScalarFunction("pandoc_inlines_to_text", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                       LogicalType::VARCHAR, PandocInlinesToTextFun));
}

} // namespace duckdb
