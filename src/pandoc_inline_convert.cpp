#include "pandoc_inline_convert.hpp"
#include "duckdb_compat.hpp"
#include "pandoc_convert_util.hpp"
#include "block_types.hpp"
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

using namespace duckdb_yyjson;

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

static void FlattenPandocInlinesVal(yyjson_val *inlines_val, int32_t level, int32_t &order, vector<Value> &result,
                                    idx_t depth) {
	CheckPandocDepth(depth);
	if (!inlines_val) {
		return;
	}

	auto process_item = [&](yyjson_val *item) {
		if (!yyjson_is_obj(item)) {
			return;
		}
		yyjson_val *t_val = yyjson_obj_get(item, "t");
		if (!t_val || !yyjson_is_str(t_val)) {
			return;
		}
		const char *pandoc_type = yyjson_get_str(t_val);
		yyjson_val *c_val = yyjson_obj_get(item, "c");

		string inline_type;
		string content_str;
		map<string, string> attrs;

		if (strcmp(pandoc_type, "Str") == 0) {
			inline_type = BlockTypes::INLINE_TEXT;
			if (c_val && yyjson_is_str(c_val)) {
				content_str = string(yyjson_get_str(c_val), yyjson_get_len(c_val));
			}
		} else if (strcmp(pandoc_type, "Space") == 0) {
			inline_type = BlockTypes::INLINE_SPACE;
			content_str = " ";
		} else if (strcmp(pandoc_type, "SoftBreak") == 0) {
			inline_type = BlockTypes::INLINE_SOFTBREAK;
			content_str = "";
		} else if (strcmp(pandoc_type, "LineBreak") == 0) {
			inline_type = BlockTypes::INLINE_LINEBREAK;
			content_str = "\n";
		} else if (strcmp(pandoc_type, "Strong") == 0) {
			inline_type = BlockTypes::INLINE_BOLD;
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));
			FlattenPandocInlinesVal(c_val, level + 1, order, result, depth + 1);
			return;
		} else if (strcmp(pandoc_type, "Emph") == 0) {
			inline_type = BlockTypes::INLINE_ITALIC;
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));
			FlattenPandocInlinesVal(c_val, level + 1, order, result, depth + 1);
			return;
		} else if (strcmp(pandoc_type, "Underline") == 0) {
			inline_type = BlockTypes::INLINE_UNDERLINE;
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));
			FlattenPandocInlinesVal(c_val, level + 1, order, result, depth + 1);
			return;
		} else if (strcmp(pandoc_type, "Strikeout") == 0) {
			inline_type = BlockTypes::INLINE_STRIKETHROUGH;
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));
			FlattenPandocInlinesVal(c_val, level + 1, order, result, depth + 1);
			return;
		} else if (strcmp(pandoc_type, "Superscript") == 0) {
			inline_type = BlockTypes::INLINE_SUPERSCRIPT;
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));
			FlattenPandocInlinesVal(c_val, level + 1, order, result, depth + 1);
			return;
		} else if (strcmp(pandoc_type, "Subscript") == 0) {
			inline_type = BlockTypes::INLINE_SUBSCRIPT;
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));
			FlattenPandocInlinesVal(c_val, level + 1, order, result, depth + 1);
			return;
		} else if (strcmp(pandoc_type, "SmallCaps") == 0) {
			inline_type = BlockTypes::INLINE_SMALLCAPS;
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));
			FlattenPandocInlinesVal(c_val, level + 1, order, result, depth + 1);
			return;
		} else if (strcmp(pandoc_type, "Code") == 0) {
			inline_type = BlockTypes::INLINE_CODE;
			if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 2) {
				yyjson_val *code_val = yyjson_arr_get(c_val, 1);
				if (code_val && yyjson_is_str(code_val)) {
					content_str = string(yyjson_get_str(code_val), yyjson_get_len(code_val));
				}
			}
		} else if (strcmp(pandoc_type, "Math") == 0) {
			inline_type = BlockTypes::INLINE_MATH;
			if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 2) {
				yyjson_val *m_type = yyjson_arr_get(c_val, 0);
				yyjson_val *m_str = yyjson_arr_get(c_val, 1);
				if (m_type && yyjson_is_obj(m_type)) {
					yyjson_val *mt = yyjson_obj_get(m_type, "t");
					if (mt && yyjson_is_str(mt) && strcmp(yyjson_get_str(mt), "InlineMath") == 0) {
						attrs["display"] = "inline";
					} else {
						attrs["display"] = "block";
					}
				}
				if (m_str && yyjson_is_str(m_str)) {
					content_str = string(yyjson_get_str(m_str), yyjson_get_len(m_str));
				}
			}
		} else if (strcmp(pandoc_type, "Link") == 0) {
			inline_type = BlockTypes::INLINE_LINK;
			yyjson_val *inlines = nullptr;
			if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 3) {
				inlines = yyjson_arr_get(c_val, 1);
				yyjson_val *target = yyjson_arr_get(c_val, 2);
				if (target && yyjson_is_arr(target) && yyjson_arr_size(target) >= 1) {
					yyjson_val *u = yyjson_arr_get(target, 0);
					if (u && yyjson_is_str(u)) {
						attrs["href"] = string(yyjson_get_str(u), yyjson_get_len(u));
					}
					// [url, TITLE] -- same omission as the Image arm, same fix.
					if (yyjson_arr_size(target) >= 2) {
						yyjson_val *t2 = yyjson_arr_get(target, 1);
						if (t2 && yyjson_is_str(t2) && yyjson_get_len(t2) > 0) {
							attrs["title"] = string(yyjson_get_str(t2), yyjson_get_len(t2));
						}
					}
				}
			}
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));
			if (inlines) {
				FlattenPandocInlinesVal(inlines, level + 1, order, result, depth + 1);
			}
			return;
		} else if (strcmp(pandoc_type, "Image") == 0) {
			inline_type = BlockTypes::INLINE_IMAGE;
			string alt_text;
			if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 3) {
				yyjson_val *alt_inlines = yyjson_arr_get(c_val, 1);
				yyjson_val *target = yyjson_arr_get(c_val, 2);
				if (target && yyjson_is_arr(target) && yyjson_arr_size(target) >= 1) {
					yyjson_val *s = yyjson_arr_get(target, 0);
					if (s && yyjson_is_str(s)) {
						attrs["src"] = string(yyjson_get_str(s), yyjson_get_len(s));
					}
					// Pandoc's target is [url, TITLE]. The title was read by nobody and
					// written by nobody, so `<img title="...">` survived neither
					// direction -- an absence that looks exactly like a document
					// without one.
					if (yyjson_arr_size(target) >= 2) {
						yyjson_val *t2 = yyjson_arr_get(target, 1);
						if (t2 && yyjson_is_str(t2) && yyjson_get_len(t2) > 0) {
							attrs["title"] = string(yyjson_get_str(t2), yyjson_get_len(t2));
						}
					}
				}
				if (alt_inlines) {
					vector<Value> alt_values;
					int32_t alt_order = 0;
					FlattenPandocInlinesVal(alt_inlines, level + 1, alt_order, alt_values, depth + 1);
					alt_text = InlineValuesToText(alt_values);
					if (!alt_text.empty()) {
						attrs["alt"] = alt_text;
					}
				}
			}
			result.push_back(CreateDocInline(inline_type, alt_text, level, attrs, order++));
			return;
		} else if (strcmp(pandoc_type, "RawInline") == 0) {
			inline_type = BlockTypes::INLINE_RAW;
			if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 2) {
				yyjson_val *fmt = yyjson_arr_get(c_val, 0);
				yyjson_val *str = yyjson_arr_get(c_val, 1);
				if (fmt && yyjson_is_str(fmt)) {
					attrs["format"] = string(yyjson_get_str(fmt), yyjson_get_len(fmt));
				}
				if (str && yyjson_is_str(str)) {
					content_str = string(yyjson_get_str(str), yyjson_get_len(str));
				}
			}
		} else if (strcmp(pandoc_type, "Quoted") == 0) {
			inline_type = BlockTypes::INLINE_QUOTED;
			yyjson_val *inlines = nullptr;
			if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 2) {
				yyjson_val *q_type = yyjson_arr_get(c_val, 0);
				if (q_type && yyjson_is_obj(q_type)) {
					yyjson_val *qt = yyjson_obj_get(q_type, "t");
					if (qt && yyjson_is_str(qt) && strcmp(yyjson_get_str(qt), "SingleQuote") == 0) {
						attrs["quote_type"] = "single";
					} else {
						attrs["quote_type"] = "double";
					}
				}
				inlines = yyjson_arr_get(c_val, 1);
			}
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));
			if (inlines) {
				FlattenPandocInlinesVal(inlines, level + 1, order, result, depth + 1);
			}
			return;
		} else if (strcmp(pandoc_type, "Cite") == 0) {
			inline_type = BlockTypes::INLINE_CITE;
		} else if (strcmp(pandoc_type, "Note") == 0) {
			inline_type = BlockTypes::INLINE_NOTE;
		} else if (strcmp(pandoc_type, "Span") == 0) {
			inline_type = BlockTypes::INLINE_SPAN;
			yyjson_val *inlines = nullptr;
			if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 2) {
				inlines = yyjson_arr_get(c_val, 1);
			}
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));
			if (inlines) {
				FlattenPandocInlinesVal(inlines, level + 1, order, result, depth + 1);
			}
			return;
		} else {
			// Preserve the words: emit a marker recording what this was, then recurse
			// into the constructor's children rather than replacing them with a
			// "[Type]" placeholder. Keeps the gap visible AND keeps the text.
			attrs[BlockTypes::ATTR_SOURCE_TYPE] = string(pandoc_type);
			result.push_back(CreateDocInline(BlockTypes::INLINE_GENERIC, "", level, attrs, order++));
			if (c_val) {
				FlattenPandocInlinesVal(c_val, level + 1, order, result, depth + 1);
			}
			return;
		}

		if (!inline_type.empty()) {
			result.push_back(CreateDocInline(inline_type, content_str, level, attrs, order++));
		}
	};

	if (yyjson_is_arr(inlines_val)) {
		size_t idx, max;
		yyjson_val *item;
		yyjson_arr_foreach(inlines_val, idx, max, item) {
			process_item(item);
		}
	} else if (yyjson_is_obj(inlines_val)) {
		process_item(inlines_val);
	}
}

void PandocInlineConvert::ConvertPandocInlinesValToDbInlines(yyjson_val *inlines_val, int32_t base_level,
                                                             int32_t &order, vector<Value> &result, idx_t depth) {
	FlattenPandocInlinesVal(inlines_val, base_level, order, result, depth);
}

void PandocInlineConvert::ConvertPandocInlinesToDbInlines(const string &json, int32_t base_level, int32_t &order,
                                                          vector<Value> &result, idx_t depth) {
	if (json.empty()) {
		return;
	}
	yyjson_doc *doc = yyjson_read(json.c_str(), json.size(), 0);
	if (!doc) {
		return;
	}
	yyjson_val *root = yyjson_doc_get_root(doc);
	FlattenPandocInlinesVal(root, base_level, order, result, depth);
	yyjson_doc_free(doc);
}

void PandocInlineConvert::PandocInlinesToDbInlinesFun(DataChunk &args, ExpressionState &state, Vector &result) {
	WarnPandocDeprecated("pandoc_inlines_to_db_inlines");
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

		ConvertPandocInlinesToDbInlines(json, 1, order, inlines, 1);

		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), inlines));
	}
}

// One attribute lookup. The Link and Image arms below each carried their own copy of
// this loop reading a single key -- and the Image copy read `src` while never reading
// `alt`, which is how every INLINE image lost its alt text on export.
static string DbAttrLookup(const Value &attrs, const char *key) {
	if (attrs.IsNull()) {
		return "";
	}
	for (auto &entry : MapValue::GetChildren(attrs)) {
		if (entry.IsNull()) {
			continue;
		}
		auto &kv = StructValue::GetChildren(entry);
		if (kv.size() >= 2 && !kv[0].IsNull() && kv[0].GetValue<string>() == key && !kv[1].IsNull()) {
			return kv[1].GetValue<string>();
		}
	}
	return "";
}

yyjson_mut_val *PandocInlineConvert::ConvertDbInlinesToPandocVal(yyjson_mut_doc *doc, const vector<Value> &inlines,
                                                                 idx_t start_idx, int32_t target_level, idx_t &end_idx,
                                                                 idx_t depth) {
	CheckPandocDepth(depth);
	yyjson_mut_val *arr = yyjson_mut_arr(doc);
	idx_t i = start_idx;

	while (i < inlines.size()) {
		auto &inline_val = inlines[i];
		auto children = StructValue::GetChildren(inline_val);

		string inline_type = children[BlockTypes::ELEMENT_TYPE_IDX].GetValue<string>();
		string content =
		    children[BlockTypes::CONTENT_IDX].IsNull() ? "" : children[BlockTypes::CONTENT_IDX].GetValue<string>();
		int32_t level =
		    children[BlockTypes::LEVEL_IDX].IsNull() ? 1 : children[BlockTypes::LEVEL_IDX].GetValue<int32_t>();

		if (level < target_level) {
			break;
		}
		if (level > target_level) {
			i++;
			continue;
		}

		if (inline_type == BlockTypes::INLINE_TEXT) {
			yyjson_mut_val *obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, obj, "t", "Str");
			yyjson_mut_obj_add_strncpy(doc, obj, "c", content.data(), content.size());
			yyjson_mut_arr_add_val(arr, obj);
		} else if (inline_type == BlockTypes::INLINE_SPACE) {
			yyjson_mut_val *obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, obj, "t", "Space");
			yyjson_mut_arr_add_val(arr, obj);
		} else if (inline_type == BlockTypes::INLINE_SOFTBREAK) {
			yyjson_mut_val *obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, obj, "t", "SoftBreak");
			yyjson_mut_arr_add_val(arr, obj);
		} else if (inline_type == BlockTypes::INLINE_LINEBREAK) {
			yyjson_mut_val *obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, obj, "t", "LineBreak");
			yyjson_mut_arr_add_val(arr, obj);
		} else if (inline_type == BlockTypes::INLINE_BOLD || inline_type == BlockTypes::INLINE_ITALIC ||
		           inline_type == BlockTypes::INLINE_STRIKETHROUGH || inline_type == BlockTypes::INLINE_SUPERSCRIPT ||
		           inline_type == BlockTypes::INLINE_SUBSCRIPT || inline_type == BlockTypes::INLINE_SMALLCAPS ||
		           inline_type == BlockTypes::INLINE_UNDERLINE) {
			const char *p_type = "Strong";
			if (inline_type == BlockTypes::INLINE_ITALIC) {
				p_type = "Emph";
			} else if (inline_type == BlockTypes::INLINE_UNDERLINE) {
				p_type = "Underline";
			} else if (inline_type == BlockTypes::INLINE_STRIKETHROUGH) {
				p_type = "Strikeout";
			} else if (inline_type == BlockTypes::INLINE_SUPERSCRIPT) {
				p_type = "Superscript";
			} else if (inline_type == BlockTypes::INLINE_SUBSCRIPT) {
				p_type = "Subscript";
			} else if (inline_type == BlockTypes::INLINE_SMALLCAPS) {
				p_type = "SmallCaps";
			}

			idx_t nested_end = i + 1;
			yyjson_mut_val *nested = ConvertDbInlinesToPandocVal(doc, inlines, i + 1, level + 1, nested_end, depth + 1);
			if (yyjson_mut_arr_size(nested) == 0 && !content.empty()) {
				nested = yyjson_mut_arr(doc);
				yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
				yyjson_mut_obj_add_strncpy(doc, str_obj, "c", content.data(), content.size());
				yyjson_mut_arr_add_val(nested, str_obj);
			}
			yyjson_mut_val *obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, obj, "t", p_type);
			yyjson_mut_obj_add_val(doc, obj, "c", nested);
			yyjson_mut_arr_add_val(arr, obj);
			i = nested_end - 1;
		} else if (inline_type == BlockTypes::INLINE_CODE) {
			yyjson_mut_val *obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, obj, "t", "Code");
			yyjson_mut_val *c_arr = yyjson_mut_arr(doc);
			yyjson_mut_val *attr_arr = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_str(doc, attr_arr, "");
			yyjson_mut_arr_add_val(attr_arr, yyjson_mut_arr(doc));
			yyjson_mut_arr_add_val(attr_arr, yyjson_mut_arr(doc));
			yyjson_mut_arr_add_val(c_arr, attr_arr);
			yyjson_mut_arr_add_strncpy(doc, c_arr, content.data(), content.size());
			yyjson_mut_obj_add_val(doc, obj, "c", c_arr);
			yyjson_mut_arr_add_val(arr, obj);
		} else if (inline_type == BlockTypes::INLINE_MATH) {
			yyjson_mut_val *obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, obj, "t", "Math");
			yyjson_mut_val *c_arr = yyjson_mut_arr(doc);
			yyjson_mut_val *m_type = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, m_type, "t", "InlineMath");
			yyjson_mut_arr_add_val(c_arr, m_type);
			yyjson_mut_arr_add_strncpy(doc, c_arr, content.data(), content.size());
			yyjson_mut_obj_add_val(doc, obj, "c", c_arr);
			yyjson_mut_arr_add_val(arr, obj);
		} else if (inline_type == BlockTypes::INLINE_LINK) {
			auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
			string href = DbAttrLookup(attrs, "href");
			string link_title = DbAttrLookup(attrs, "title");
			idx_t nested_end = i + 1;
			yyjson_mut_val *nested = ConvertDbInlinesToPandocVal(doc, inlines, i + 1, level + 1, nested_end, depth + 1);
			if (yyjson_mut_arr_size(nested) == 0 && !content.empty()) {
				nested = yyjson_mut_arr(doc);
				yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
				yyjson_mut_obj_add_strncpy(doc, str_obj, "c", content.data(), content.size());
				yyjson_mut_arr_add_val(nested, str_obj);
			}
			yyjson_mut_val *obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, obj, "t", "Link");
			yyjson_mut_val *c_arr = yyjson_mut_arr(doc);
			yyjson_mut_val *attr_arr = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_str(doc, attr_arr, "");
			yyjson_mut_arr_add_val(attr_arr, yyjson_mut_arr(doc));
			yyjson_mut_arr_add_val(attr_arr, yyjson_mut_arr(doc));
			yyjson_mut_arr_add_val(c_arr, attr_arr);
			yyjson_mut_arr_add_val(c_arr, nested);
			yyjson_mut_val *target_arr = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_strncpy(doc, target_arr, href.data(), href.size());
			yyjson_mut_arr_add_strncpy(doc, target_arr, link_title.data(), link_title.size());
			yyjson_mut_arr_add_val(c_arr, target_arr);
			yyjson_mut_obj_add_val(doc, obj, "c", c_arr);
			yyjson_mut_arr_add_val(arr, obj);
			i = nested_end - 1;
		} else if (inline_type == BlockTypes::INLINE_IMAGE) {
			auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
			string src = DbAttrLookup(attrs, "src");
			string img_title = DbAttrLookup(attrs, "title");
			// THE ALT. This arm wrote an EMPTY array unconditionally, so every inline
			// image lost its alt text on export -- while the BLOCK image arm read it
			// correctly. That asymmetry is the only reason the roundtrip sweep passed:
			// it probes `image` in block position, and a real document has its images
			// inside paragraphs.
			//
			// Three sources in precedence order, because the reader writes the alt into
			// BOTH content and attributes['alt'] and a foreign producer may write
			// either: nested children, then content, then the attribute.
			idx_t img_end = i + 1;
			yyjson_mut_val *alt = ConvertDbInlinesToPandocVal(doc, inlines, i + 1, level + 1, img_end, depth + 1);
			if (yyjson_mut_arr_size(alt) == 0) {
				string alt_text = content.empty() ? DbAttrLookup(attrs, "alt") : content;
				if (!alt_text.empty()) {
					yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
					yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
					yyjson_mut_obj_add_strncpy(doc, str_obj, "c", alt_text.data(), alt_text.size());
					yyjson_mut_arr_add_val(alt, str_obj);
				}
			}
			yyjson_mut_val *obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, obj, "t", "Image");
			yyjson_mut_val *c_arr = yyjson_mut_arr(doc);
			yyjson_mut_val *attr_arr = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_str(doc, attr_arr, "");
			yyjson_mut_arr_add_val(attr_arr, yyjson_mut_arr(doc));
			yyjson_mut_arr_add_val(attr_arr, yyjson_mut_arr(doc));
			yyjson_mut_arr_add_val(c_arr, attr_arr);
			yyjson_mut_arr_add_val(c_arr, alt);
			yyjson_mut_val *target_arr = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_strncpy(doc, target_arr, src.data(), src.size());
			yyjson_mut_arr_add_strncpy(doc, target_arr, img_title.data(), img_title.size());
			yyjson_mut_arr_add_val(c_arr, target_arr);
			yyjson_mut_obj_add_val(doc, obj, "c", c_arr);
			yyjson_mut_arr_add_val(arr, obj);
		} else if (inline_type == BlockTypes::INLINE_RAW) {
			auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
			string format = "html";
			if (!attrs.IsNull()) {
				auto &map_entries = MapValue::GetChildren(attrs);
				for (auto &entry : map_entries) {
					if (entry.IsNull()) {
						continue;
					}
					auto &kv = StructValue::GetChildren(entry);
					if (kv.size() >= 2 && !kv[0].IsNull() && kv[0].GetValue<string>() == "format") {
						if (!kv[1].IsNull()) {
							format = kv[1].GetValue<string>();
						}
						break;
					}
				}
			}
			yyjson_mut_val *obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, obj, "t", "RawInline");
			yyjson_mut_val *c_arr = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_strncpy(doc, c_arr, format.data(), format.size());
			yyjson_mut_arr_add_strncpy(doc, c_arr, content.data(), content.size());
			yyjson_mut_obj_add_val(doc, obj, "c", c_arr);
			yyjson_mut_arr_add_val(arr, obj);
		} else if (inline_type == BlockTypes::INLINE_QUOTED) {
			auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
			const char *quote_type = "DoubleQuote";
			if (!attrs.IsNull()) {
				auto &map_entries = MapValue::GetChildren(attrs);
				for (auto &entry : map_entries) {
					if (entry.IsNull()) {
						continue;
					}
					auto &kv = StructValue::GetChildren(entry);
					if (kv.size() >= 2 && !kv[0].IsNull() && kv[0].GetValue<string>() == "quote_type") {
						if (!kv[1].IsNull() && kv[1].GetValue<string>() == "single") {
							quote_type = "SingleQuote";
						}
						break;
					}
				}
			}
			idx_t nested_end = i + 1;
			yyjson_mut_val *nested = ConvertDbInlinesToPandocVal(doc, inlines, i + 1, level + 1, nested_end, depth + 1);
			yyjson_mut_val *obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, obj, "t", "Quoted");
			yyjson_mut_val *c_arr = yyjson_mut_arr(doc);
			yyjson_mut_val *q_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, q_obj, "t", quote_type);
			yyjson_mut_arr_add_val(c_arr, q_obj);
			yyjson_mut_arr_add_val(c_arr, nested);
			yyjson_mut_obj_add_val(doc, obj, "c", c_arr);
			yyjson_mut_arr_add_val(arr, obj);
			i = nested_end - 1;
		} else if (inline_type == BlockTypes::INLINE_SPAN) {
			idx_t nested_end = i + 1;
			yyjson_mut_val *nested = ConvertDbInlinesToPandocVal(doc, inlines, i + 1, level + 1, nested_end, depth + 1);
			yyjson_mut_val *obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, obj, "t", "Span");
			yyjson_mut_val *c_arr = yyjson_mut_arr(doc);
			yyjson_mut_val *attr_arr = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_str(doc, attr_arr, "");
			yyjson_mut_arr_add_val(attr_arr, yyjson_mut_arr(doc));
			yyjson_mut_arr_add_val(attr_arr, yyjson_mut_arr(doc));
			yyjson_mut_arr_add_val(c_arr, attr_arr);
			yyjson_mut_arr_add_val(c_arr, nested);
			yyjson_mut_obj_add_val(doc, obj, "c", c_arr);
			yyjson_mut_arr_add_val(arr, obj);
			i = nested_end - 1;
		} else if (inline_type == BlockTypes::INLINE_GENERIC) {
			// The EXPORT half of the no-silent-drops rule. This used to fall through
			// to the "[generic]" placeholder below, which destroyed the children's
			// text -- the same defect as the "[Underline]" placeholder on the import
			// side, surviving here because nothing round-tripped an unmapped inline.
			//
			// Emitted as a Span carrying source_type as a class: a real Pandoc
			// constructor, so the text survives AND what it stood in for is still
			// recorded. Re-importing yields `span` rather than `generic`, which is
			// lossy in TYPE but not in content or identity.
			idx_t nested_end = i + 1;
			yyjson_mut_val *nested = ConvertDbInlinesToPandocVal(doc, inlines, i + 1, level + 1, nested_end, depth + 1);
			string source_type;
			auto &gen_attrs = children[BlockTypes::ATTRIBUTES_IDX];
			if (!gen_attrs.IsNull()) {
				for (auto &entry : MapValue::GetChildren(gen_attrs)) {
					if (entry.IsNull()) {
						continue;
					}
					auto &kv = StructValue::GetChildren(entry);
					if (kv.size() >= 2 && !kv[0].IsNull() && kv[0].GetValue<string>() == BlockTypes::ATTR_SOURCE_TYPE) {
						if (!kv[1].IsNull()) {
							source_type = kv[1].GetValue<string>();
						}
						break;
					}
				}
			}
			yyjson_mut_val *obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, obj, "t", "Span");
			yyjson_mut_val *c_arr = yyjson_mut_arr(doc);
			yyjson_mut_val *attr_arr = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_str(doc, attr_arr, "");
			yyjson_mut_val *classes = yyjson_mut_arr(doc);
			if (!source_type.empty()) {
				yyjson_mut_arr_add_strncpy(doc, classes, source_type.data(), source_type.size());
			}
			yyjson_mut_arr_add_val(attr_arr, classes);
			yyjson_mut_arr_add_val(attr_arr, yyjson_mut_arr(doc));
			yyjson_mut_arr_add_val(c_arr, attr_arr);
			yyjson_mut_arr_add_val(c_arr, nested);
			yyjson_mut_obj_add_val(doc, obj, "c", c_arr);
			yyjson_mut_arr_add_val(arr, obj);
			i = nested_end - 1;
		} else {
			yyjson_mut_val *obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, obj, "t", "Str");
			string placeholder = "[" + inline_type + "]";
			yyjson_mut_obj_add_strncpy(doc, obj, "c", placeholder.data(), placeholder.size());
			yyjson_mut_arr_add_val(arr, obj);
		}
		i++;
	}

	end_idx = i;
	return arr;
}

string PandocInlineConvert::ConvertInlinesToPandocJson(const vector<Value> &inlines) {
	if (inlines.empty()) {
		return "[]";
	}
	auto &first = inlines[0];
	auto first_children = StructValue::GetChildren(first);
	int32_t target_level =
	    first_children[BlockTypes::LEVEL_IDX].IsNull() ? 1 : first_children[BlockTypes::LEVEL_IDX].GetValue<int32_t>();

	idx_t end_idx = 0;
	yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val *arr = ConvertDbInlinesToPandocVal(doc, inlines, 0, target_level, end_idx, 1);
	yyjson_mut_doc_set_root(doc, arr);
	size_t len = 0;
	char *json = yyjson_mut_write(doc, 0, &len);
	string res(json ? json : "[]", len);
	if (json) {
		free(json);
	}
	yyjson_mut_doc_free(doc);
	return res;
}

static string RenderInlinesToTextVal(yyjson_val *inlines_val, const string &mode, idx_t depth) {
	CheckPandocDepth(depth);
	if (!inlines_val) {
		return "";
	}
	std::ostringstream out;

	auto process_item = [&](yyjson_val *item) {
		if (!yyjson_is_obj(item)) {
			return;
		}
		yyjson_val *t_val = yyjson_obj_get(item, "t");
		if (!t_val || !yyjson_is_str(t_val)) {
			return;
		}
		const char *pandoc_type = yyjson_get_str(t_val);
		yyjson_val *c_val = yyjson_obj_get(item, "c");

		if (strcmp(pandoc_type, "Str") == 0) {
			if (c_val && yyjson_is_str(c_val)) {
				out << string(yyjson_get_str(c_val), yyjson_get_len(c_val));
			}
		} else if (strcmp(pandoc_type, "Space") == 0) {
			out << " ";
		} else if (strcmp(pandoc_type, "SoftBreak") == 0) {
			out << (mode == "text" ? " " : "\n");
		} else if (strcmp(pandoc_type, "LineBreak") == 0) {
			out << "\n";
		} else if (strcmp(pandoc_type, "Strong") == 0) {
			if (mode == "markdown") {
				out << "**";
			}
			out << RenderInlinesToTextVal(c_val, mode, depth + 1);
			if (mode == "markdown") {
				out << "**";
			}
		} else if (strcmp(pandoc_type, "Emph") == 0) {
			if (mode == "markdown") {
				out << "*";
			}
			out << RenderInlinesToTextVal(c_val, mode, depth + 1);
			if (mode == "markdown") {
				out << "*";
			}
		} else if (strcmp(pandoc_type, "Code") == 0) {
			if (mode == "markdown") {
				out << "`";
			}
			if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 2) {
				yyjson_val *code_val = yyjson_arr_get(c_val, 1);
				if (code_val && yyjson_is_str(code_val)) {
					out << string(yyjson_get_str(code_val), yyjson_get_len(code_val));
				}
			}
			if (mode == "markdown") {
				out << "`";
			}
		} else if (strcmp(pandoc_type, "Link") == 0) {
			if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 3) {
				yyjson_val *inlines = yyjson_arr_get(c_val, 1);
				yyjson_val *target = yyjson_arr_get(c_val, 2);
				string link_text = RenderInlinesToTextVal(inlines, mode, depth + 1);
				string url;
				if (target && yyjson_is_arr(target) && yyjson_arr_size(target) >= 1) {
					yyjson_val *u = yyjson_arr_get(target, 0);
					if (u && yyjson_is_str(u)) {
						url = string(yyjson_get_str(u), yyjson_get_len(u));
					}
				}
				if (mode == "markdown") {
					out << "[" << link_text << "](" << url << ")";
				} else {
					out << link_text;
				}
			}
		} else if (c_val && yyjson_is_arr(c_val)) {
			// Unrecognised constructor (Underline, or anything a future pandoc adds):
			// recurse into its children so the words survive instead of vanishing.
			// A `c` that is not an inline list simply yields nothing, so this degrades
			// gracefully rather than emitting structural noise.
			out << RenderInlinesToTextVal(c_val, mode, depth + 1);
		}
	};

	if (yyjson_is_arr(inlines_val)) {
		size_t idx, max;
		yyjson_val *item;
		yyjson_arr_foreach(inlines_val, idx, max, item) {
			process_item(item);
		}
	} else if (yyjson_is_obj(inlines_val)) {
		process_item(inlines_val);
	}
	return out.str();
}

static string RenderInlinesToText(const string &json, const string &mode, idx_t depth) {
	if (json.empty()) {
		return "";
	}
	yyjson_doc *doc = yyjson_read(json.c_str(), json.size(), 0);
	if (!doc) {
		return "";
	}
	yyjson_val *root = yyjson_doc_get_root(doc);
	string res = RenderInlinesToTextVal(root, mode, depth);
	yyjson_doc_free(doc);
	return res;
}

void PandocInlineConvert::DbInlinesToPandocFun(DataChunk &args, ExpressionState &state, Vector &result) {
	WarnPandocDeprecated("duck_blocks_inlines_to_pandoc");
	auto &list_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto list_val = list_vec.GetValue(i);

		if (list_val.IsNull()) {
			result.SetValue(i, Value("[]"));
			continue;
		}

		auto &inlines = ListValue::GetChildren(list_val);
		string json = ConvertInlinesToPandocJson(inlines);
		result.SetValue(i, Value(json));
	}
}

void PandocInlineConvert::PandocInlinesToTextFun(DataChunk &args, ExpressionState &state, Vector &result) {
	WarnPandocDeprecated("pandoc_inlines_to_text");
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

	// Registered through a local rather than inline, so SetFallible can run BEFORE
	// the function is handed over. Every converter here can throw the Pandoc
	// nesting-depth cap (CheckPandocDepth), and DuckDB v2.0 makes that a DECLARED
	// property: an undeclared throw becomes "INTERNAL Error: ... the function is not
	// marked as fallible". It is a RUNTIME contract, so it compiles clean either way
	// and only shows up as a failing error-path test -- and only on a build with
	// assertions on, which is why one CI arch can be green and another red on the
	// same commit. No-op on v1.5.
	auto register_fallible = [&loader](ScalarFunction fun) {
		fun.SetFallible();
		loader.RegisterFunction(fun);
	};

	// pandoc_inlines_to_db_inlines(json VARCHAR) -> LIST(duck_block)
	register_fallible(ScalarFunction("pandoc_inlines_to_db_inlines", {LogicalType::VARCHAR}, duck_block_list_type,
	                                 PandocInlinesToDbInlinesFun));

	// duck_blocks_inlines_to_pandoc(LIST(duck_block)) -> VARCHAR (JSON)
	register_fallible(ScalarFunction("duck_blocks_inlines_to_pandoc", {duck_block_list_type}, LogicalType::VARCHAR,
	                                 DbInlinesToPandocFun));

	// duck_blocks_inlines_to_pandoc(LIST(LIST(duck_block))) -> VARCHAR (JSON) - auto-flattening
	register_fallible(ScalarFunction("duck_blocks_inlines_to_pandoc", {duck_block_nested_list_type},
	                                 LogicalType::VARCHAR, DbInlinesToPandocNestedFun));

	// pandoc_inlines_to_text(json VARCHAR) -> VARCHAR
	register_fallible(
	    ScalarFunction("pandoc_inlines_to_text", {LogicalType::VARCHAR}, LogicalType::VARCHAR, PandocInlinesToTextFun));

	// pandoc_inlines_to_text(json VARCHAR, mode VARCHAR) -> VARCHAR
	register_fallible(ScalarFunction("pandoc_inlines_to_text", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                 LogicalType::VARCHAR, PandocInlinesToTextFun));
}

} // namespace duckdb
