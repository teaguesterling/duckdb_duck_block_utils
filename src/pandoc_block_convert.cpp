#include "pandoc_block_convert.hpp"
#include "pandoc_convert_util.hpp"
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

// Helper to create a single duck_block Value (for block).
// `level` is NULL for top-level blocks; blocks nested inside a Div carry
// their nesting level so the emit side can reconstruct the tree (issue #11:
// div children were dropped on emit because every parsed block had a NULL
// level).
static Value CreateDocBlock(const string &block_type, const string &content, const map<string, string> &attrs,
                            int32_t order, const string &encoding = "text", const Value &level = Value()) {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
	struct_values.push_back(make_pair("element_type", Value(block_type)));
	struct_values.push_back(make_pair("content", Value(content)));
	struct_values.push_back(make_pair("level", level));
	struct_values.push_back(make_pair("encoding", Value(encoding)));
	struct_values.push_back(make_pair("attributes", CreateAttrsMap(attrs)));
	struct_values.push_back(make_pair("element_order", Value(order)));
	return Value::STRUCT(std::move(struct_values));
}

// kind='value' elements model Pandoc's recursive MetaValue tree. They are appended
// AFTER the document's blocks so that blocks[1] keeps pointing at the first content
// block; consumers must filter on `kind` rather than index blindly, which is already
// true for inlines and merely less obvious.
static Value CreateDocValue(const string &value_type, const string &content, const map<string, string> &attrs,
                            int32_t order, const Value &level) {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_VALUE)));
	struct_values.push_back(make_pair("element_type", Value(value_type)));
	struct_values.push_back(make_pair("content", Value(content)));
	struct_values.push_back(make_pair("level", level));
	struct_values.push_back(make_pair("encoding", Value("text")));
	struct_values.push_back(make_pair("attributes", CreateAttrsMap(attrs)));
	struct_values.push_back(make_pair("element_order", Value(order)));
	return Value::STRUCT(std::move(struct_values));
}

using namespace duckdb_yyjson;

static string ValToJsonString(yyjson_val *val) {
	if (!val) {
		return "";
	}
	size_t len = 0;
	char *str = yyjson_val_write(val, 0, &len);
	string res(str ? str : "", len);
	if (str) {
		free(str);
	}
	return res;
}

// A parsed Pandoc attr triple: ["id", ["class", ...], [["key","value"], ...]]
struct PandocAttr {
	string id;
	vector<string> classes;
	vector<std::pair<string, string>> key_values;
};

static void ParsePandocAttrVal(yyjson_val *attr_val, PandocAttr &attr) {
	if (!attr_val || !yyjson_is_arr(attr_val)) {
		return;
	}
	// Element 0: id
	yyjson_val *id_val = yyjson_arr_get(attr_val, 0);
	if (id_val && yyjson_is_str(id_val)) {
		attr.id = string(yyjson_get_str(id_val), yyjson_get_len(id_val));
	}
	// Element 1: classes
	yyjson_val *classes_val = yyjson_arr_get(attr_val, 1);
	if (classes_val && yyjson_is_arr(classes_val)) {
		size_t idx, max;
		yyjson_val *cls;
		yyjson_arr_foreach(classes_val, idx, max, cls) {
			if (yyjson_is_str(cls)) {
				attr.classes.emplace_back(yyjson_get_str(cls), yyjson_get_len(cls));
			}
		}
	}
	// Element 2: key-values
	yyjson_val *kvs_val = yyjson_arr_get(attr_val, 2);
	if (kvs_val && yyjson_is_arr(kvs_val)) {
		size_t idx, max;
		yyjson_val *kv;
		yyjson_arr_foreach(kvs_val, idx, max, kv) {
			if (yyjson_is_arr(kv) && yyjson_arr_size(kv) >= 2) {
				yyjson_val *k = yyjson_arr_get(kv, 0);
				yyjson_val *v = yyjson_arr_get(kv, 1);
				if (k && yyjson_is_str(k) && v && yyjson_is_str(v)) {
					attr.key_values.emplace_back(string(yyjson_get_str(k), yyjson_get_len(k)),
					                             string(yyjson_get_str(v), yyjson_get_len(v)));
				}
			}
		}
	}
}

static bool IsReservedAttrKey(const string &key) {
	return key == "id" || key == "class" || key == "heading_level" || key == "language" || key == "list_type" ||
	       key == "format" || key == "src" || key == "alt" || key == "title" || key == "href" || key == "quote_type" ||
	       key == "display";
}

static void StorePandocAttr(const PandocAttr &attr, map<string, string> &attrs) {
	if (!attr.id.empty()) {
		attrs["id"] = attr.id;
	}
	if (!attr.classes.empty()) {
		string joined;
		for (auto &cls : attr.classes) {
			if (!joined.empty()) {
				joined += " ";
			}
			joined += cls;
		}
		attrs["class"] = joined;
	}
	for (auto &kv : attr.key_values) {
		if (IsReservedAttrKey(kv.first) || attrs.find(kv.first) != attrs.end()) {
			continue;
		}
		attrs[kv.first] = kv.second;
	}
}

static string ExtractInlinesTextVal(yyjson_val *inlines_arr) {
	if (!inlines_arr) {
		return "";
	}
	if (yyjson_is_str(inlines_arr)) {
		return string(yyjson_get_str(inlines_arr), yyjson_get_len(inlines_arr));
	}
	string result;
	auto process_item = [&](yyjson_val *item) {
		if (!yyjson_is_obj(item)) {
			return;
		}
		yyjson_val *t_val = yyjson_obj_get(item, "t");
		if (!t_val || !yyjson_is_str(t_val)) {
			return;
		}
		const char *t = yyjson_get_str(t_val);
		yyjson_val *c_val = yyjson_obj_get(item, "c");
		if (strcmp(t, "Str") == 0) {
			if (c_val && yyjson_is_str(c_val)) {
				result.append(yyjson_get_str(c_val), yyjson_get_len(c_val));
			}
		} else if (strcmp(t, "Space") == 0 || strcmp(t, "SoftBreak") == 0) {
			result += " ";
		} else if (strcmp(t, "LineBreak") == 0) {
			result += "\n";
		}
	};

	if (yyjson_is_arr(inlines_arr)) {
		size_t idx, max;
		yyjson_val *item;
		yyjson_arr_foreach(inlines_arr, idx, max, item) {
			process_item(item);
		}
	} else if (yyjson_is_obj(inlines_arr)) {
		process_item(inlines_arr);
	}
	return result;
}

static bool InlinesAreTextOnly(const vector<Value> &inlines) {
	for (auto &el : inlines) {
		if (el.IsNull()) {
			continue;
		}
		auto &children = StructValue::GetChildren(el);
		if (children[BlockTypes::ELEMENT_TYPE_IDX].IsNull()) {
			continue;
		}
		auto element_type = children[BlockTypes::ELEMENT_TYPE_IDX].GetValue<string>();
		if (element_type != BlockTypes::INLINE_TEXT && element_type != BlockTypes::INLINE_SPACE &&
		    element_type != BlockTypes::INLINE_SOFTBREAK && element_type != BlockTypes::INLINE_LINEBREAK) {
			return false;
		}
	}
	return true;
}

static void ProcessPandocBlockVal(yyjson_val *block_val, int32_t &order, vector<Value> &result, idx_t depth,
                                  int32_t parent_div_level) {
	CheckPandocDepth(depth);
	if (!block_val || !yyjson_is_obj(block_val)) {
		return;
	}

	const int32_t effective_level = (parent_div_level == 0) ? 1 : parent_div_level + 1;
	// Every element carries an EXPLICIT structural level -- there are no NULLs.
	// `level` is depth in a depth-first ordering, and level plus adjacency together
	// describe the whole document tree, which is why it cannot be optional.
	// (Teague, 2026-08-31: this was always the rule; the NULL-at-top-level
	// normalisation was never approved. Spec 3.0 restores it.)
	const Value block_level = Value(effective_level);

	yyjson_val *t_val = yyjson_obj_get(block_val, "t");
	if (!t_val || !yyjson_is_str(t_val)) {
		return;
	}
	const char *pandoc_type = yyjson_get_str(t_val);
	yyjson_val *c_val = yyjson_obj_get(block_val, "c");

	map<string, string> attrs;
	string content;
	string block_type;
	string encoding = "text";
	yyjson_val *inlines_val_p = nullptr;

	if (strcmp(pandoc_type, "Header") == 0) {
		block_type = BlockTypes::TYPE_HEADING;
		if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 3) {
			yyjson_val *level_val = yyjson_arr_get(c_val, 0);
			yyjson_val *attr_val = yyjson_arr_get(c_val, 1);
			yyjson_val *inlines_val = yyjson_arr_get(c_val, 2);

			int32_t level = yyjson_is_num(level_val) ? yyjson_get_int(level_val) : 1;
			attrs["heading_level"] = std::to_string(level);

			PandocAttr pattr;
			ParsePandocAttrVal(attr_val, pattr);
			StorePandocAttr(pattr, attrs);

			content = ExtractInlinesTextVal(inlines_val);
			inlines_val_p = inlines_val;
		}
	} else if (strcmp(pandoc_type, "Para") == 0 || strcmp(pandoc_type, "Plain") == 0) {
		// Plain and Para are DIFFERENT constructors and this collapsed them, which is
		// how tight-vs-loose list items were lost. Plain is a block-level text run
		// with no paragraph semantics; Para is a paragraph.
		block_type = (strcmp(pandoc_type, "Plain") == 0) ? BlockTypes::TYPE_PLAIN : BlockTypes::TYPE_PARAGRAPH;
		if (c_val) {
			content = ExtractInlinesTextVal(c_val);
			inlines_val_p = c_val;
		}
	} else if (strcmp(pandoc_type, "CodeBlock") == 0) {
		block_type = BlockTypes::TYPE_CODE;
		if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 2) {
			yyjson_val *attr_val = yyjson_arr_get(c_val, 0);
			yyjson_val *code_val = yyjson_arr_get(c_val, 1);

			PandocAttr pattr;
			ParsePandocAttrVal(attr_val, pattr);
			if (!pattr.classes.empty()) {
				attrs["language"] = pattr.classes[0];
			}
			StorePandocAttr(pattr, attrs);

			if (code_val && yyjson_is_str(code_val)) {
				content = string(yyjson_get_str(code_val), yyjson_get_len(code_val));
			}
		}
	} else if (strcmp(pandoc_type, "BlockQuote") == 0) {
		// STRUCTURAL. Was encoding='json', which put raw Pandoc AST on the screen in
		// every renderer that showed `content` -- three of them did.
		block_type = BlockTypes::TYPE_BLOCKQUOTE;
		result.push_back(CreateDocBlock(block_type, "", attrs, order++, encoding, block_level));
		if (c_val && yyjson_is_arr(c_val)) {
			size_t idx, max;
			yyjson_val *child_block;
			yyjson_arr_foreach(c_val, idx, max, child_block) {
				ProcessPandocBlockVal(child_block, order, result, depth + 1, effective_level);
			}
		}
		return;
	} else if (strcmp(pandoc_type, "BulletList") == 0 || strcmp(pandoc_type, "OrderedList") == 0) {
		// STRUCTURAL, not opaque JSON. This used to store the whole Pandoc `c` as
		// encoding='json', which made decoding every consumer's problem and left the
		// same four defects in three independent implementations -- and, unnoticed by
		// any of them, exported back to an EMPTY BulletList, because the exporter
		// walks children and there were none. See "encoding='json' does not say whose
		// json" in docs/duck_blocks_spec.md.
		//
		// Emits list -> list_item at level+1 -> the item's own blocks at level+2, the
		// shape the builders already produce and the exporter already understands.
		const bool is_ordered = (strcmp(pandoc_type, "OrderedList") == 0);
		block_type = BlockTypes::TYPE_LIST;
		attrs["list_type"] = is_ordered ? "ordered" : "bullet";
		// `ordered` is the attribute spec v1.0 documents for this; `list_type` arrived
		// later with this reader and nothing ever said which was canonical. Emitting
		// only list_type meant a consumer written against the PUBLISHED v1 spec read
		// nothing at all from a Pandoc-produced list. Both are emitted; v1's name is
		// the canonical one.
		attrs["ordered"] = is_ordered ? "true" : "false";

		yyjson_val *items_arr = c_val;
		if (is_ordered && c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 2) {
			// OrderedList c = [ListAttributes, [[Block]]] where
			// ListAttributes = [start, {t: style}, {t: delim}]. The start number lived
			// only inside the JSON, so a consumer reading attributes could not find it
			// and every ordered list restarted at 1.
			yyjson_val *list_attrs = yyjson_arr_get(c_val, 0);
			if (list_attrs && yyjson_is_arr(list_attrs) && yyjson_arr_size(list_attrs) >= 3) {
				yyjson_val *start_val = yyjson_arr_get(list_attrs, 0);
				if (start_val && yyjson_is_int(start_val)) {
					attrs["start"] = to_string(yyjson_get_int(start_val));
				}
				for (idx_t k = 1; k < 3; k++) {
					yyjson_val *spec = yyjson_arr_get(list_attrs, k);
					if (!spec || !yyjson_is_obj(spec)) {
						continue;
					}
					yyjson_val *tag = yyjson_obj_get(spec, "t");
					if (tag && yyjson_is_str(tag)) {
						attrs[k == 1 ? "number_style" : "number_delim"] = yyjson_get_str(tag);
					}
				}
			}
			items_arr = yyjson_arr_get(c_val, 1);
		}

		result.push_back(CreateDocBlock(block_type, "", attrs, order++, encoding, block_level));

		if (items_arr && yyjson_is_arr(items_arr)) {
			size_t idx, max;
			yyjson_val *item_val;
			yyjson_arr_foreach(items_arr, idx, max, item_val) {
				map<string, string> item_attrs;
				result.push_back(CreateDocBlock(BlockTypes::TYPE_LIST_ITEM, "", item_attrs, order++, "text",
				                                Value(effective_level + 1)));
				if (item_val && yyjson_is_arr(item_val)) {
					size_t bidx, bmax;
					yyjson_val *item_block;
					yyjson_arr_foreach(item_val, bidx, bmax, item_block) {
						ProcessPandocBlockVal(item_block, order, result, depth + 1, effective_level + 1);
					}
				}
			}
		}
		return;
	} else if (strcmp(pandoc_type, "DefinitionList") == 0) {
		// DefinitionList c = [([Inline], [[Block]])] -- term/definitions pairs. Kept as
		// JSON like BulletList and OrderedList: the shape has no flat text rendering.
		block_type = BlockTypes::TYPE_DEFLIST;
		encoding = "json";
		content = ValToJsonString(c_val);
	} else if (strcmp(pandoc_type, "Table") == 0) {
		block_type = BlockTypes::TYPE_TABLE;
		encoding = "json";
		content = ValToJsonString(c_val);
	} else if (strcmp(pandoc_type, "LineBlock") == 0) {
		// LineBlock c = [[Inline]] -- an array OF ARRAYS, one per line. Deliberately
		// does not set inlines_val_p: handing an array-of-arrays to the inline
		// converter would misparse it. Line structure lives in content as
		// newline-separated text.
		block_type = BlockTypes::TYPE_LINEBLOCK;
		if (c_val && yyjson_is_arr(c_val)) {
			string joined;
			size_t idx, max;
			yyjson_val *line;
			yyjson_arr_foreach(c_val, idx, max, line) {
				if (idx > 0) {
					joined += "\n";
				}
				joined += ExtractInlinesTextVal(line);
			}
			content = joined;
		}
	} else if (strcmp(pandoc_type, "HorizontalRule") == 0) {
		block_type = BlockTypes::TYPE_HR;
		content = "";
	} else if (strcmp(pandoc_type, "RawBlock") == 0) {
		block_type = BlockTypes::TYPE_RAW;
		if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 2) {
			yyjson_val *fmt = yyjson_arr_get(c_val, 0);
			yyjson_val *str = yyjson_arr_get(c_val, 1);
			if (fmt && yyjson_is_str(fmt)) {
				attrs["format"] = string(yyjson_get_str(fmt), yyjson_get_len(fmt));
			}
			if (str && yyjson_is_str(str)) {
				content = string(yyjson_get_str(str), yyjson_get_len(str));
			}
		}
	} else if (strcmp(pandoc_type, "Figure") == 0) {
		// Figure c = [Attr, Caption, [Block]] where Caption = [ShortCaption?, [Block]].
		// A figure carries TWO block lists, so the flat duck_block list must keep them
		// distinguishable: content blocks are emitted first at level+1, then a
		// `caption` container at level+1 whose own children are the caption blocks.
		// Content-before-caption so a renderer walking the list in order emits the
		// image before the words describing it.
		block_type = BlockTypes::TYPE_FIGURE;
		if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 3) {
			yyjson_val *attr_val = yyjson_arr_get(c_val, 0);
			yyjson_val *caption_val = yyjson_arr_get(c_val, 1);
			yyjson_val *blocks_arr = yyjson_arr_get(c_val, 2);

			PandocAttr pattr;
			ParsePandocAttrVal(attr_val, pattr);
			StorePandocAttr(pattr, attrs);

			result.push_back(CreateDocBlock(block_type, "", attrs, order++, encoding, block_level));

			if (blocks_arr && yyjson_is_arr(blocks_arr)) {
				size_t idx, max;
				yyjson_val *child_block;
				yyjson_arr_foreach(blocks_arr, idx, max, child_block) {
					ProcessPandocBlockVal(child_block, order, result, depth + 1, effective_level);
				}
			}

			// Emit the caption container only when the caption actually has blocks,
			// then recurse so caption formatting (bold, links) survives as real
			// inline children rather than being flattened to text.
			if (caption_val && yyjson_is_arr(caption_val) && yyjson_arr_size(caption_val) >= 2) {
				yyjson_val *short_val = yyjson_arr_get(caption_val, 0);
				yyjson_val *cap_blocks = yyjson_arr_get(caption_val, 1);
				if (cap_blocks && yyjson_is_arr(cap_blocks) && yyjson_arr_size(cap_blocks) > 0) {
					map<string, string> cap_attrs;
					if (short_val && yyjson_is_arr(short_val)) {
						string short_text = ExtractInlinesTextVal(short_val);
						if (!short_text.empty()) {
							cap_attrs["short_caption"] = short_text;
						}
					}
					// The caption container is a SIBLING of the content blocks -- both
					// are children of the figure -- so it sits at effective_level + 1,
					// and its own children are recursed one deeper again.
					result.push_back(CreateDocBlock(BlockTypes::TYPE_CAPTION, "", cap_attrs, order++, "text",
					                                Value(effective_level + 1)));
					size_t idx, max;
					yyjson_val *cap_block;
					yyjson_arr_foreach(cap_blocks, idx, max, cap_block) {
						ProcessPandocBlockVal(cap_block, order, result, depth + 1, effective_level + 1);
					}
				}
			}
			return;
		}
	} else if (strcmp(pandoc_type, "Div") == 0) {
		block_type = BlockTypes::TYPE_DIV;
		if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 2) {
			yyjson_val *attr_val = yyjson_arr_get(c_val, 0);
			yyjson_val *blocks_arr = yyjson_arr_get(c_val, 1);

			PandocAttr pattr;
			ParsePandocAttrVal(attr_val, pattr);
			StorePandocAttr(pattr, attrs);

			result.push_back(CreateDocBlock(block_type, "", attrs, order++, encoding, block_level));

			if (blocks_arr && yyjson_is_arr(blocks_arr)) {
				size_t idx, max;
				yyjson_val *child_block;
				yyjson_arr_foreach(blocks_arr, idx, max, child_block) {
					ProcessPandocBlockVal(child_block, order, result, depth + 1, effective_level);
				}
			}
			return;
		}
	} else {
		// Never drop a constructor silently: preserve it verbatim so document length is
		// stable and the gap stays visible instead of invisible. Serialises the whole
		// constructor object (not just `c`) so export can reconstitute it including `t`.
		block_type = BlockTypes::TYPE_GENERIC;
		encoding = "json";
		attrs["source_type"] = string(pandoc_type);
		content = ValToJsonString(block_val);
	}

	if (block_type.empty()) {
		return;
	}

	const int32_t block_order = order++;
	vector<Value> inline_children;
	if (inlines_val_p) {
		const int32_t order_before_children = order;
		PandocInlineConvert::ConvertPandocInlinesValToDbInlines(inlines_val_p, effective_level + 1, order,
		                                                        inline_children, depth);
		if (InlinesAreTextOnly(inline_children)) {
			inline_children.clear();
			order = order_before_children;
		} else {
			content.clear();
		}
	}

	result.push_back(CreateDocBlock(block_type, content, attrs, block_order, encoding, block_level));
	for (auto &child : inline_children) {
		result.push_back(child);
	}
}

// Walk one MetaValue into kind='value' elements. `list` and `map` nest their children
// via `level`, exactly as `div` and `figure` do. Recursive, so it honours the depth cap.
static void ProcessPandocMetaVal(const string &key, yyjson_val *val, int32_t &order, vector<Value> &result,
                                 int32_t level, idx_t depth) {
	CheckPandocDepth(depth);
	if (!val || !yyjson_is_obj(val)) {
		return;
	}
	yyjson_val *t_val = yyjson_obj_get(val, "t");
	if (!t_val || !yyjson_is_str(t_val)) {
		return;
	}
	const char *mt = yyjson_get_str(t_val);
	yyjson_val *c_val = yyjson_obj_get(val, "c");

	map<string, string> attrs;
	if (!key.empty()) {
		attrs["key"] = key;
	}

	if (strcmp(mt, "MetaString") == 0) {
		string s;
		if (c_val && yyjson_is_str(c_val)) {
			s = string(yyjson_get_str(c_val), yyjson_get_len(c_val));
		}
		result.push_back(CreateDocValue(BlockTypes::VALUE_STRING, s, attrs, order++, Value(level)));
	} else if (strcmp(mt, "MetaBool") == 0) {
		const bool b = c_val && yyjson_is_true(c_val);
		result.push_back(CreateDocValue(BlockTypes::VALUE_BOOL, b ? "true" : "false", attrs, order++, Value(level)));
	} else if (strcmp(mt, "MetaInlines") == 0) {
		result.push_back(CreateDocValue(BlockTypes::VALUE_INLINES, "", attrs, order++, Value(level)));
		if (c_val) {
			PandocInlineConvert::ConvertPandocInlinesValToDbInlines(c_val, level + 1, order, result, depth);
		}
	} else if (strcmp(mt, "MetaBlocks") == 0) {
		result.push_back(CreateDocValue(BlockTypes::VALUE_BLOCKS, "", attrs, order++, Value(level)));
		if (c_val && yyjson_is_arr(c_val)) {
			size_t i, n;
			yyjson_val *b;
			yyjson_arr_foreach(c_val, i, n, b) {
				ProcessPandocBlockVal(b, order, result, depth + 1, level);
			}
		}
	} else if (strcmp(mt, "MetaList") == 0) {
		result.push_back(CreateDocValue(BlockTypes::VALUE_LIST, "", attrs, order++, Value(level)));
		if (c_val && yyjson_is_arr(c_val)) {
			size_t i, n;
			yyjson_val *e;
			yyjson_arr_foreach(c_val, i, n, e) {
				ProcessPandocMetaVal("", e, order, result, level + 1, depth + 1);
			}
		}
	} else if (strcmp(mt, "MetaMap") == 0) {
		result.push_back(CreateDocValue(BlockTypes::VALUE_MAP, "", attrs, order++, Value(level)));
		if (c_val && yyjson_is_obj(c_val)) {
			size_t i, n;
			yyjson_val *k, *v;
			yyjson_obj_foreach(c_val, i, n, k, v) {
				ProcessPandocMetaVal(string(yyjson_get_str(k), yyjson_get_len(k)), v, order, result, level + 1,
				                     depth + 1);
			}
		}
	} else {
		// Same no-silent-drops rule as blocks and inlines: an unrecognised MetaValue is
		// preserved verbatim rather than discarded.
		attrs["source_type"] = string(mt);
		result.push_back(CreateDocValue(BlockTypes::TYPE_GENERIC, ValToJsonString(val), attrs, order++, Value(level)));
	}
}

void PandocBlockConvert::ConvertPandocAstToBlocks(const string &json, vector<Value> &blocks) {
	if (json.empty()) {
		return;
	}
	yyjson_doc *doc = yyjson_read(json.c_str(), json.size(), 0);
	if (!doc) {
		return;
	}
	yyjson_val *root = yyjson_doc_get_root(doc);
	if (!root) {
		yyjson_doc_free(doc);
		return;
	}

	int32_t order = 0;
	yyjson_val *blocks_val = root;
	if (yyjson_is_obj(root)) {
		yyjson_val *b = yyjson_obj_get(root, "blocks");
		if (b) {
			blocks_val = b;
		}
	}

	if (yyjson_is_arr(blocks_val)) {
		size_t idx, max;
		yyjson_val *block_val;
		yyjson_arr_foreach(blocks_val, idx, max, block_val) {
			ProcessPandocBlockVal(block_val, order, blocks, 1, 0);
		}
	} else if (yyjson_is_obj(blocks_val)) {
		ProcessPandocBlockVal(blocks_val, order, blocks, 1, 0);
	}

	// Document metadata, AFTER the blocks so blocks[1] still points at the first
	// content block. Previously dropped entirely: title, tags, author and draft all
	// round-tripped to {}.
	if (yyjson_is_obj(root)) {
		yyjson_val *meta = yyjson_obj_get(root, "meta");
		if (meta && yyjson_is_obj(meta)) {
			size_t i, n;
			yyjson_val *k, *v;
			yyjson_obj_foreach(meta, i, n, k, v) {
				ProcessPandocMetaVal(string(yyjson_get_str(k), yyjson_get_len(k)), v, order, blocks, 1, 1);
			}
		}
	}

	yyjson_doc_free(doc);
}

void PandocBlockConvert::PandocAstToBlocksFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &json_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto json_val = json_vec.GetValue(i);

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

static string GetElementStringField(const Value &element, idx_t field_idx) {
	auto &children = StructValue::GetChildren(element);
	if (children[field_idx].IsNull()) {
		return "";
	}
	return children[field_idx].GetValue<string>();
}

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

static int32_t GetElementLevel(const Value &element) {
	auto &children = StructValue::GetChildren(element);
	if (children[BlockTypes::LEVEL_IDX].IsNull()) {
		return 1;
	}
	return children[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
}

static yyjson_mut_val *CreatePandocAttrVal(yyjson_mut_doc *doc, const Value &element, const string &fallback_class) {
	auto id = GetElementAttribute(element, "id");
	auto classes = GetElementAttribute(element, "class");
	if (classes.empty()) {
		classes = fallback_class;
	}

	yyjson_mut_val *attr_arr = yyjson_mut_arr(doc);
	yyjson_mut_arr_add_strncpy(doc, attr_arr, id.data(), id.size());

	yyjson_mut_val *classes_arr = yyjson_mut_arr(doc);
	size_t start = 0;
	while (start < classes.length()) {
		size_t space = classes.find(' ', start);
		size_t len = (space == string::npos) ? string::npos : space - start;
		string cls = classes.substr(start, len);
		if (!cls.empty()) {
			yyjson_mut_arr_add_strncpy(doc, classes_arr, cls.data(), cls.size());
		}
		if (space == string::npos) {
			break;
		}
		start = space + 1;
	}
	yyjson_mut_arr_add_val(attr_arr, classes_arr);

	yyjson_mut_val *kvs_arr = yyjson_mut_arr(doc);
	auto &children = StructValue::GetChildren(element);
	auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
	if (!attrs.IsNull()) {
		auto &map_entries = MapValue::GetChildren(attrs);
		for (auto &entry : map_entries) {
			if (entry.IsNull()) {
				continue;
			}
			auto &kv = StructValue::GetChildren(entry);
			if (kv.size() < 2 || kv[0].IsNull() || kv[1].IsNull()) {
				continue;
			}
			string key = kv[0].GetValue<string>();
			if (IsReservedAttrKey(key)) {
				continue;
			}
			string val = kv[1].GetValue<string>();
			yyjson_mut_val *pair_arr = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_strncpy(doc, pair_arr, key.data(), key.size());
			yyjson_mut_arr_add_strncpy(doc, pair_arr, val.data(), val.size());
			yyjson_mut_arr_add_val(kvs_arr, pair_arr);
		}
	}
	yyjson_mut_arr_add_val(attr_arr, kvs_arr);
	return attr_arr;
}

static yyjson_mut_val *ConvertListToPandocVal(yyjson_mut_doc *doc, const vector<Value> &blocks_list, idx_t &start_idx,
                                              int32_t list_level, idx_t depth);
static void ConvertContainerChildrenToPandocVal(yyjson_mut_doc *doc, const vector<Value> &blocks_list, idx_t &start_idx,
                                                int32_t parent_level, idx_t depth, yyjson_mut_val *target_arr,
                                                yyjson_mut_val *switch_arr, const char *switch_type);
static yyjson_mut_val *ConvertDivToPandocVal(yyjson_mut_doc *doc, const vector<Value> &blocks_list, idx_t &start_idx,
                                             int32_t div_level, idx_t depth);

// A block child no walk enumerates. Emitted as a Div classed with its element_type,
// carrying whatever content it had -- visible and correctly nested rather than
// silently skipped.
//
// Shared by the container walk and the list walk deliberately. Those two had
// SEPARATE terminal arms, and that duplication is the root of this whole class:
// a type added to one dispatch is invisible to the other, which is how table,
// deflist and lineblock came to be dropped inside containers, and code blocks,
// blockquotes and horizontal rules inside list items.
static yyjson_mut_val *ConvertUnhandledChildToPandocVal(yyjson_mut_doc *doc, const vector<Value> &blocks_list,
                                                        idx_t &idx, idx_t depth, const Value &child,
                                                        const string &child_type, const string &content,
                                                        const vector<Value> &inline_children, int32_t child_level) {
	yyjson_mut_val *fallback = yyjson_mut_obj(doc);
	yyjson_mut_obj_add_str(doc, fallback, "t", "Div");
	yyjson_mut_val *fc_arr = yyjson_mut_arr(doc);
	yyjson_mut_arr_add_val(fc_arr, CreatePandocAttrVal(doc, child, child_type));
	yyjson_mut_val *fb_blocks = yyjson_mut_arr(doc);
	if (!content.empty() || !inline_children.empty()) {
		yyjson_mut_val *para_obj = yyjson_mut_obj(doc);
		yyjson_mut_obj_add_str(doc, para_obj, "t", "Plain");
		if (!inline_children.empty()) {
			idx_t inl_end = 0;
			yyjson_mut_obj_add_val(
			    doc, para_obj, "c",
			    PandocInlineConvert::ConvertDbInlinesToPandocVal(doc, inline_children, 0, child_level + 1, inl_end, 1));
		} else {
			yyjson_mut_val *inl_arr = yyjson_mut_arr(doc);
			yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
			yyjson_mut_obj_add_strncpy(doc, str_obj, "c", content.data(), content.size());
			yyjson_mut_arr_add_val(inl_arr, str_obj);
			yyjson_mut_obj_add_val(doc, para_obj, "c", inl_arr);
		}
		yyjson_mut_arr_add_val(fb_blocks, para_obj);
	}
	// The child's OWN descendants. Without this the fallback kept an element's own
	// text and dropped everything below it -- a blockquote inside a list item came
	// through as an empty Div and its quoted paragraph vanished, the same silent
	// loss one level deeper. Delegating means the fallback needs to know nothing
	// about what the subtree contains.
	ConvertContainerChildrenToPandocVal(doc, blocks_list, idx, child_level, depth + 1, fb_blocks, nullptr, nullptr);
	yyjson_mut_arr_add_val(fc_arr, fb_blocks);
	yyjson_mut_obj_add_val(doc, fallback, "c", fc_arr);
	return fallback;
}

static yyjson_mut_val *ConvertListToPandocVal(yyjson_mut_doc *doc, const vector<Value> &blocks_list, idx_t &start_idx,
                                              int32_t list_level, idx_t depth) {
	CheckPandocDepth(depth);
	auto &list_block = blocks_list[start_idx];
	auto list_type = GetElementAttribute(list_block, "list_type");
	auto ordered_attr = GetElementAttribute(list_block, "ordered");
	bool is_ordered = (list_type == "ordered") || (ordered_attr == "true");
	const char *pandoc_type = is_ordered ? "OrderedList" : "BulletList";

	struct ListItem {
		string content;
		// A tight item's child is `plain`, a loose item's is `paragraph`. Pandoc
		// spells that Plain vs Para, so the flag has to survive to the emit below or
		// the distinction dies on the way out -- which is exactly how it was lost
		// before `plain` existed.
		bool tight = true;
		vector<string> extra_paragraphs;
		// Block children this walk does not enumerate -- a code block, blockquote or
		// horizontal rule inside a list item, all legal Pandoc and all silently
		// dropped before. Carried through rather than skipped.
		vector<yyjson_mut_val *> extra_blocks;
		vector<Value> inlines;
		yyjson_mut_val *nested_list_val = nullptr;
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

		if (child_kind == BlockTypes::KIND_BLOCK) {
			if (child_type == BlockTypes::TYPE_LIST_ITEM && child_level == list_level + 1) {
				if (in_item) {
					items.push_back(current_item);
				}
				current_item = ListItem();
				current_item.content = GetElementStringField(child, BlockTypes::CONTENT_IDX);
				in_item = true;
				j++;
			} else if (child_type == BlockTypes::TYPE_LIST &&
			           (child_level == list_level + 1 || child_level == list_level + 2)) {
				// A nested list is a child of the LIST_ITEM, so it sits at list_level + 2.
				// This only accepted list_level + 1, so every nested list from the Pandoc
				// reader fell past it -- to a bare `j++` before today, meaning nested
				// lists were dropped on export entirely. The +1 case is kept for a list
				// directly under a list, which the builders can produce.
				if (in_item) {
					current_item.nested_list_val = ConvertListToPandocVal(doc, blocks_list, j, child_level, depth + 1);
				} else {
					j++;
				}
			} else if ((child_type == BlockTypes::TYPE_PARAGRAPH || child_type == BlockTypes::TYPE_PLAIN) &&
			           child_level == list_level + 2 && in_item) {
				// Two legal item shapes, so accept both. The builders hang the words on
				// list_item itself; the Pandoc reader emits list_item -> paragraph ->
				// inlines, because a Pandoc list item holds BLOCKS. Reading only the
				// builder shape is what made a Pandoc list export as an empty
				// BulletList -- right number of items, every one of them blank.
				//
				// EVERY paragraph, not just the first. A Pandoc list item holds a list
				// of blocks and <li><p>a</p><p>b</p></li> is ordinary in EPUB and HTML;
				// keeping only the first silently dropped the rest on export. Found by
				// testing the multi-block case panduck asked about before answering
				// them, rather than after.
				auto para_text = GetElementStringField(child, BlockTypes::CONTENT_IDX);
				if (current_item.content.empty() && current_item.extra_paragraphs.empty()) {
					current_item.tight = (child_type == BlockTypes::TYPE_PLAIN);
					current_item.content = para_text;
				} else {
					current_item.extra_paragraphs.push_back(para_text);
				}
				j++;
			} else if (child_level <= list_level) {
				break;
			} else if (in_item && child_level == list_level + 2) {
				// NEVER SILENTLY DROP -- this was a bare `j++`, so a code block,
				// blockquote or horizontal rule inside a list item vanished. Same
				// defect as the container walk had, one function over, which is why
				// the fallback is now shared rather than written twice.
				auto child_content = GetElementStringField(child, BlockTypes::CONTENT_IDX);
				vector<Value> child_inlines;
				for (idx_t k = j + 1; k < blocks_list.size(); k++) {
					auto &inl = blocks_list[k];
					if (inl.IsNull()) {
						continue;
					}
					if (GetElementStringField(inl, BlockTypes::KIND_IDX) != BlockTypes::KIND_INLINE ||
					    GetElementLevel(inl) <= child_level) {
						break;
					}
					child_inlines.push_back(inl);
				}
				current_item.extra_blocks.push_back(ConvertUnhandledChildToPandocVal(
				    doc, blocks_list, j, depth, child, child_type, child_content, child_inlines, child_level));
			} else {
				j++;
			}
		} else if (child_kind == BlockTypes::KIND_INLINE && in_item) {
			// level+2 is the builder shape, level+3 the Pandoc one (under a paragraph).
			if (child_level == list_level + 2 || child_level == list_level + 3) {
				current_item.inlines.push_back(child);
			}
			j++;
		} else {
			j++;
		}
	}

	if (in_item) {
		items.push_back(current_item);
	}

	start_idx = j;

	yyjson_mut_val *root_obj = yyjson_mut_obj(doc);
	yyjson_mut_obj_add_str(doc, root_obj, "t", pandoc_type);

	yyjson_mut_val *c_outer = nullptr;
	yyjson_mut_val *items_arr = yyjson_mut_arr(doc);

	if (is_ordered) {
		// Honour the ListAttributes the reader preserved rather than hardcoding
		// [1, Decimal, Period]. A list starting at 3, or using roman numerals or
		// parens, used to silently come back as "1." -- the numbers lived only
		// inside the opaque JSON, so nothing downstream could see them.
		c_outer = yyjson_mut_arr(doc);
		yyjson_mut_val *order_spec = yyjson_mut_arr(doc);
		yyjson_mut_arr_add_int(doc, order_spec, ParseInt32OrDefault(GetElementAttribute(list_block, "start"), 1));
		auto number_style = GetElementAttribute(list_block, "number_style");
		auto number_delim = GetElementAttribute(list_block, "number_delim");
		yyjson_mut_val *style_obj = yyjson_mut_obj(doc);
		yyjson_mut_obj_add_strcpy(doc, style_obj, "t", number_style.empty() ? "Decimal" : number_style.c_str());
		yyjson_mut_arr_add_val(order_spec, style_obj);
		yyjson_mut_val *delim_obj = yyjson_mut_obj(doc);
		yyjson_mut_obj_add_strcpy(doc, delim_obj, "t", number_delim.empty() ? "Period" : number_delim.c_str());
		yyjson_mut_arr_add_val(order_spec, delim_obj);
		yyjson_mut_arr_add_val(c_outer, order_spec);
		yyjson_mut_arr_add_val(c_outer, items_arr);
		yyjson_mut_obj_add_val(doc, root_obj, "c", c_outer);
	} else {
		yyjson_mut_obj_add_val(doc, root_obj, "c", items_arr);
	}

	for (auto &item : items) {
		yyjson_mut_val *item_blocks = yyjson_mut_arr(doc);
		yyjson_mut_val *plain_obj = yyjson_mut_obj(doc);
		yyjson_mut_obj_add_str(doc, plain_obj, "t", item.tight ? "Plain" : "Para");

		if (!item.inlines.empty()) {
			idx_t inl_end = 0;
			yyjson_mut_val *inl_arr =
			    PandocInlineConvert::ConvertDbInlinesToPandocVal(doc, item.inlines, 0, list_level + 2, inl_end, 1);
			yyjson_mut_obj_add_val(doc, plain_obj, "c", inl_arr);
		} else if (!item.content.empty()) {
			yyjson_mut_val *inl_arr = yyjson_mut_arr(doc);
			yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
			yyjson_mut_obj_add_strncpy(doc, str_obj, "c", item.content.data(), item.content.size());
			yyjson_mut_arr_add_val(inl_arr, str_obj);
			yyjson_mut_obj_add_val(doc, plain_obj, "c", inl_arr);
		} else {
			yyjson_mut_obj_add_val(doc, plain_obj, "c", yyjson_mut_arr(doc));
		}
		yyjson_mut_arr_add_val(item_blocks, plain_obj);

		// A multi-block item's remaining paragraphs. Pandoc's own reader emits Para
		// for these, so emit Para rather than Plain to stay closer to the input.
		for (auto &extra : item.extra_paragraphs) {
			yyjson_mut_val *para_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, para_obj, "t", "Para");
			yyjson_mut_val *pinl = yyjson_mut_arr(doc);
			yyjson_mut_val *pstr = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, pstr, "t", "Str");
			yyjson_mut_obj_add_strncpy(doc, pstr, "c", extra.data(), extra.size());
			yyjson_mut_arr_add_val(pinl, pstr);
			yyjson_mut_obj_add_val(doc, para_obj, "c", pinl);
			yyjson_mut_arr_add_val(item_blocks, para_obj);
		}

		for (auto *extra : item.extra_blocks) {
			yyjson_mut_arr_add_val(item_blocks, extra);
		}

		if (item.nested_list_val) {
			yyjson_mut_arr_add_val(item_blocks, item.nested_list_val);
		}
		yyjson_mut_arr_add_val(items_arr, item_blocks);
	}

	return root_obj;
}

static yyjson_mut_val *ConvertFigureToPandocVal(yyjson_mut_doc *doc, const vector<Value> &blocks_list, idx_t &start_idx,
                                                int32_t fig_level, idx_t depth);

// Walks a container's children -- everything at a level deeper than the container --
// converting each into `target_arr`. Shared by Div and Figure so the eight child block
// types are handled in one place rather than duplicated per container.
static void ConvertContainerChildrenToPandocVal(yyjson_mut_doc *doc, const vector<Value> &blocks_list, idx_t &start_idx,
                                                int32_t parent_level, idx_t depth, yyjson_mut_val *target_arr,
                                                yyjson_mut_val *switch_arr, const char *switch_type) {
	yyjson_mut_val *child_blocks_arr = target_arr;
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

		// Figure separates content blocks from caption blocks in a single walk: on
		// meeting the switch block at parent_level + 1, output redirects and the
		// switching block itself is not emitted. Div passes nullptr and is unaffected.
		if (switch_type && switch_arr && child_kind == BlockTypes::KIND_BLOCK && child_type == switch_type &&
		    child_level == parent_level + 1) {
			child_blocks_arr = switch_arr;
			j++;
			continue;
		}

		if (child_level <= parent_level && child_kind == BlockTypes::KIND_BLOCK) {
			break;
		}
		if (child_kind == BlockTypes::KIND_INLINE || child_type == BlockTypes::TYPE_LIST_ITEM) {
			j++;
			continue;
		}

		if (child_kind == BlockTypes::KIND_BLOCK) {
			auto content = GetElementStringField(child, BlockTypes::CONTENT_IDX);

			vector<Value> inline_children;
			for (idx_t k = j + 1; k < blocks_list.size(); k++) {
				auto &inl = blocks_list[k];
				if (inl.IsNull()) {
					continue;
				}
				auto inl_kind = GetElementStringField(inl, BlockTypes::KIND_IDX);
				if (inl_kind == BlockTypes::KIND_BLOCK) {
					break;
				}
				if (inl_kind == BlockTypes::KIND_INLINE) {
					inline_children.push_back(inl);
				}
			}

			if (child_type == BlockTypes::TYPE_PARAGRAPH || child_type == BlockTypes::TYPE_PLAIN) {
				// `plain` rides the paragraph path and differs only in the constructor
				// emitted. Found by sweeping every container after adding the type
				// rather than by reasoning about which paths reach it: a `plain` inside
				// a div, blockquote or figure was DROPPED ENTIRELY, because this branch
				// tested one type name and the fallthrough consumed the rest.
				yyjson_mut_val *para_obj = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, para_obj, "t", child_type == BlockTypes::TYPE_PLAIN ? "Plain" : "Para");
				if (!inline_children.empty()) {
					idx_t inl_end = 0;
					yyjson_mut_val *inl_arr = PandocInlineConvert::ConvertDbInlinesToPandocVal(
					    doc, inline_children, 0, child_level + 1, inl_end, 1);
					yyjson_mut_obj_add_val(doc, para_obj, "c", inl_arr);
				} else {
					yyjson_mut_val *inl_arr = yyjson_mut_arr(doc);
					yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
					yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
					yyjson_mut_obj_add_strncpy(doc, str_obj, "c", content.data(), content.size());
					yyjson_mut_arr_add_val(inl_arr, str_obj);
					yyjson_mut_obj_add_val(doc, para_obj, "c", inl_arr);
				}
				yyjson_mut_arr_add_val(child_blocks_arr, para_obj);
				j++;
			} else if (child_type == BlockTypes::TYPE_HEADING) {
				auto level_str = GetElementAttribute(child, "heading_level");
				int level = ParseInt32OrDefault(level_str, 1);
				yyjson_mut_val *header_obj = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, header_obj, "t", "Header");
				yyjson_mut_val *hc_arr = yyjson_mut_arr(doc);
				yyjson_mut_arr_add_int(doc, hc_arr, level);
				yyjson_mut_arr_add_val(hc_arr, CreatePandocAttrVal(doc, child, ""));
				if (!inline_children.empty()) {
					idx_t inl_end = 0;
					yyjson_mut_val *inl_arr = PandocInlineConvert::ConvertDbInlinesToPandocVal(
					    doc, inline_children, 0, child_level + 1, inl_end, 1);
					yyjson_mut_arr_add_val(hc_arr, inl_arr);
				} else {
					yyjson_mut_val *inl_arr = yyjson_mut_arr(doc);
					yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
					yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
					yyjson_mut_obj_add_strncpy(doc, str_obj, "c", content.data(), content.size());
					yyjson_mut_arr_add_val(inl_arr, str_obj);
					yyjson_mut_arr_add_val(hc_arr, inl_arr);
				}
				yyjson_mut_obj_add_val(doc, header_obj, "c", hc_arr);
				yyjson_mut_arr_add_val(child_blocks_arr, header_obj);
				j++;
			} else if (child_type == BlockTypes::TYPE_CODE) {
				auto language = GetElementAttribute(child, "language");
				yyjson_mut_val *code_obj = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, code_obj, "t", "CodeBlock");
				yyjson_mut_val *cc_arr = yyjson_mut_arr(doc);
				yyjson_mut_arr_add_val(cc_arr, CreatePandocAttrVal(doc, child, language));
				yyjson_mut_arr_add_strncpy(doc, cc_arr, content.data(), content.size());
				yyjson_mut_obj_add_val(doc, code_obj, "c", cc_arr);
				yyjson_mut_arr_add_val(child_blocks_arr, code_obj);
				j++;
			} else if (child_type == BlockTypes::TYPE_BLOCKQUOTE) {
				auto child_encoding = GetElementStringField(child, BlockTypes::ENCODING_IDX);
				yyjson_mut_val *bq_obj = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, bq_obj, "t", "BlockQuote");
				if (child_encoding == BlockTypes::ENCODING_JSON && !content.empty()) {
					yyjson_doc *sub_doc = yyjson_read(content.c_str(), content.size(), 0);
					if (sub_doc) {
						yyjson_mut_val *imported = yyjson_val_mut_copy(doc, yyjson_doc_get_root(sub_doc));
						yyjson_mut_obj_add_val(doc, bq_obj, "c", imported);
						yyjson_doc_free(sub_doc);
					} else {
						yyjson_mut_obj_add_val(doc, bq_obj, "c", yyjson_mut_arr(doc));
					}
				} else if (!inline_children.empty()) {
					yyjson_mut_val *bqc_arr = yyjson_mut_arr(doc);
					yyjson_mut_val *para_obj = yyjson_mut_obj(doc);
					yyjson_mut_obj_add_str(doc, para_obj, "t", "Para");
					idx_t inl_end = 0;
					yyjson_mut_val *inl_arr = PandocInlineConvert::ConvertDbInlinesToPandocVal(
					    doc, inline_children, 0, child_level + 1, inl_end, 1);
					yyjson_mut_obj_add_val(doc, para_obj, "c", inl_arr);
					yyjson_mut_arr_add_val(bqc_arr, para_obj);
					yyjson_mut_obj_add_val(doc, bq_obj, "c", bqc_arr);
				} else if (!content.empty()) {
					yyjson_mut_val *bqc_arr = yyjson_mut_arr(doc);
					yyjson_mut_val *para_obj = yyjson_mut_obj(doc);
					yyjson_mut_obj_add_str(doc, para_obj, "t", "Para");
					yyjson_mut_val *inl_arr = yyjson_mut_arr(doc);
					yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
					yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
					yyjson_mut_obj_add_strncpy(doc, str_obj, "c", content.data(), content.size());
					yyjson_mut_arr_add_val(inl_arr, str_obj);
					yyjson_mut_obj_add_val(doc, para_obj, "c", inl_arr);
					yyjson_mut_arr_add_val(bqc_arr, para_obj);
					yyjson_mut_obj_add_val(doc, bq_obj, "c", bqc_arr);
				} else {
					yyjson_mut_val *bqc_arr = yyjson_mut_arr(doc);
					yyjson_mut_val *para_obj = yyjson_mut_obj(doc);
					yyjson_mut_obj_add_str(doc, para_obj, "t", "Para");
					yyjson_mut_obj_add_val(doc, para_obj, "c", yyjson_mut_arr(doc));
					yyjson_mut_arr_add_val(bqc_arr, para_obj);
					yyjson_mut_obj_add_val(doc, bq_obj, "c", bqc_arr);
				}
				yyjson_mut_arr_add_val(child_blocks_arr, bq_obj);
				j++;
			} else if (child_type == BlockTypes::TYPE_HR) {
				yyjson_mut_val *hr_obj = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, hr_obj, "t", "HorizontalRule");
				yyjson_mut_arr_add_val(child_blocks_arr, hr_obj);
				j++;
			} else if (child_type == BlockTypes::TYPE_LIST) {
				yyjson_mut_val *list_obj = ConvertListToPandocVal(doc, blocks_list, j, child_level, depth + 1);
				yyjson_mut_arr_add_val(child_blocks_arr, list_obj);
			} else if (child_type == BlockTypes::TYPE_DIV) {
				yyjson_mut_val *nested_div_obj = ConvertDivToPandocVal(doc, blocks_list, j, child_level, depth + 1);
				yyjson_mut_arr_add_val(child_blocks_arr, nested_div_obj);
			} else if ((child_type == BlockTypes::TYPE_TABLE || child_type == BlockTypes::TYPE_DEFLIST) &&
			           GetElementStringField(child, BlockTypes::ENCODING_IDX) == BlockTypes::ENCODING_JSON &&
			           !content.empty()) {
				// These store their whole Pandoc tuple as JSON, so splice it back exactly
				// as the top-level branches do. Without this a table or definition list
				// inside a div, blockquote or figure vanished.
				yyjson_mut_val *obj = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, obj, "t",
				                       child_type == BlockTypes::TYPE_TABLE ? "Table" : "DefinitionList");
				yyjson_doc *sub_doc = yyjson_read(content.c_str(), content.size(), 0);
				if (sub_doc) {
					yyjson_mut_obj_add_val(doc, obj, "c", yyjson_val_mut_copy(doc, yyjson_doc_get_root(sub_doc)));
					yyjson_doc_free(sub_doc);
				} else {
					yyjson_mut_obj_add_val(doc, obj, "c", yyjson_mut_arr(doc));
				}
				yyjson_mut_arr_add_val(child_blocks_arr, obj);
				j++;
			} else {
				// NEVER SILENTLY DROP. This was a bare `j++`, so any block type the chain
				// did not enumerate vanished inside every container -- lineblock, deflist
				// and table all were, long before today.
				yyjson_mut_arr_add_val(child_blocks_arr,
				                       ConvertUnhandledChildToPandocVal(doc, blocks_list, j, depth, child, child_type,
				                                                        content, inline_children, child_level));
			}
		} else {
			j++;
		}
	}

	start_idx = j;
}

static yyjson_mut_val *ConvertDivToPandocVal(yyjson_mut_doc *doc, const vector<Value> &blocks_list, idx_t &start_idx,
                                             int32_t div_level, idx_t depth) {
	CheckPandocDepth(depth);
	auto &div_block = blocks_list[start_idx];

	yyjson_mut_val *div_obj = yyjson_mut_obj(doc);
	yyjson_mut_obj_add_str(doc, div_obj, "t", "Div");

	yyjson_mut_val *c_arr = yyjson_mut_arr(doc);
	yyjson_mut_arr_add_val(c_arr, CreatePandocAttrVal(doc, div_block, ""));

	yyjson_mut_val *child_blocks_arr = yyjson_mut_arr(doc);
	yyjson_mut_arr_add_val(c_arr, child_blocks_arr);
	yyjson_mut_obj_add_val(doc, div_obj, "c", c_arr);

	ConvertContainerChildrenToPandocVal(doc, blocks_list, start_idx, div_level, depth, child_blocks_arr, nullptr,
	                                    nullptr);
	return div_obj;
}

// Does the container at `idx` own block children (the structural shape), as opposed
// to carrying its text directly (the builder shape)? Both are legal, so the exporter
// has to tell them apart rather than assume one.
static bool HasBlockChildren(const vector<Value> &blocks_list, idx_t idx) {
	const int32_t own_level = GetElementLevel(blocks_list[idx]);
	for (idx_t j = idx + 1; j < blocks_list.size(); j++) {
		auto &child = blocks_list[j];
		if (child.IsNull()) {
			continue;
		}
		if (GetElementStringField(child, BlockTypes::KIND_IDX) != BlockTypes::KIND_BLOCK) {
			continue;
		}
		return GetElementLevel(child) > own_level;
	}
	return false;
}

// BlockQuote c = [Block] -- no Attr, so the children array IS `c`. Needed once the
// reader stopped storing the quote as opaque JSON: without it a structural quote
// exported as an empty BlockQuote followed by its own children as SIBLINGS, which
// silently lifts quoted text out of the quote and into the body.
static yyjson_mut_val *ConvertBlockquoteToPandocVal(yyjson_mut_doc *doc, const vector<Value> &blocks_list,
                                                    idx_t &start_idx, int32_t quote_level, idx_t depth) {
	CheckPandocDepth(depth);
	yyjson_mut_val *bq_obj = yyjson_mut_obj(doc);
	yyjson_mut_obj_add_str(doc, bq_obj, "t", "BlockQuote");
	yyjson_mut_val *child_blocks_arr = yyjson_mut_arr(doc);
	yyjson_mut_obj_add_val(doc, bq_obj, "c", child_blocks_arr);
	ConvertContainerChildrenToPandocVal(doc, blocks_list, start_idx, quote_level, depth, child_blocks_arr, nullptr,
	                                    nullptr);
	return bq_obj;
}

// Figure c = [Attr, Caption, [Block]] where Caption = [ShortCaption?, [Block]].
// Children were emitted as content blocks followed by a `caption` container, so one
// walk with a switch at that container reconstitutes both lists.
static yyjson_mut_val *ConvertFigureToPandocVal(yyjson_mut_doc *doc, const vector<Value> &blocks_list, idx_t &start_idx,
                                                int32_t fig_level, idx_t depth) {
	CheckPandocDepth(depth);
	auto &fig_block = blocks_list[start_idx];

	// short_caption is stored on the caption child, so read it before the walk consumes it.
	string short_text;
	for (idx_t k = start_idx + 1; k < blocks_list.size(); k++) {
		auto &c = blocks_list[k];
		if (c.IsNull()) {
			continue;
		}
		if (GetElementStringField(c, BlockTypes::KIND_IDX) == BlockTypes::KIND_BLOCK &&
		    GetElementLevel(c) <= fig_level) {
			break;
		}
		if (GetElementStringField(c, BlockTypes::ELEMENT_TYPE_IDX) == BlockTypes::TYPE_CAPTION) {
			short_text = GetElementAttribute(c, "short_caption");
			break;
		}
	}

	yyjson_mut_val *content_arr = yyjson_mut_arr(doc);
	yyjson_mut_val *caption_arr = yyjson_mut_arr(doc);
	ConvertContainerChildrenToPandocVal(doc, blocks_list, start_idx, fig_level, depth, content_arr, caption_arr,
	                                    BlockTypes::TYPE_CAPTION);

	yyjson_mut_val *fig_obj = yyjson_mut_obj(doc);
	yyjson_mut_obj_add_str(doc, fig_obj, "t", "Figure");
	yyjson_mut_val *c_arr = yyjson_mut_arr(doc);
	yyjson_mut_arr_add_val(c_arr, CreatePandocAttrVal(doc, fig_block, ""));

	yyjson_mut_val *cap_arr = yyjson_mut_arr(doc);
	if (short_text.empty()) {
		yyjson_mut_arr_add_val(cap_arr, yyjson_mut_null(doc));
	} else {
		// ShortCaption is Maybe [Inline], so wrap the stored text in a single Str.
		yyjson_mut_val *short_arr = yyjson_mut_arr(doc);
		yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
		yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
		yyjson_mut_obj_add_strncpy(doc, str_obj, "c", short_text.data(), short_text.size());
		yyjson_mut_arr_add_val(short_arr, str_obj);
		yyjson_mut_arr_add_val(cap_arr, short_arr);
	}
	yyjson_mut_arr_add_val(cap_arr, caption_arr);

	yyjson_mut_arr_add_val(c_arr, cap_arr);
	yyjson_mut_arr_add_val(c_arr, content_arr);
	yyjson_mut_obj_add_val(doc, fig_obj, "c", c_arr);
	return fig_obj;
}

static string BuildBlocksJson(const vector<Value> &blocks_list) {
	yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val *blocks_arr = yyjson_mut_arr(doc);
	yyjson_mut_doc_set_root(doc, blocks_arr);

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

		if (element_type == BlockTypes::TYPE_LIST_ITEM || element_type == BlockTypes::TYPE_METADATA) {
			block_idx++;
			continue;
		}

		vector<Value> inline_children;
		for (idx_t j = block_idx + 1; j < blocks_list.size(); j++) {
			auto &child = blocks_list[j];
			if (child.IsNull()) {
				continue;
			}
			auto child_kind = GetElementStringField(child, BlockTypes::KIND_IDX);
			if (child_kind == BlockTypes::KIND_BLOCK) {
				break;
			}
			if (child_kind == BlockTypes::KIND_INLINE) {
				inline_children.push_back(child);
			}
		}

		if (element_type == BlockTypes::TYPE_HEADING) {
			auto level_str = GetElementAttribute(block, "heading_level");
			int level = ParseInt32OrDefault(level_str, 1);
			yyjson_mut_val *header_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, header_obj, "t", "Header");
			yyjson_mut_val *hc_arr = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_int(doc, hc_arr, level);
			yyjson_mut_arr_add_val(hc_arr, CreatePandocAttrVal(doc, block, ""));
			if (!inline_children.empty()) {
				idx_t inl_end = 0;
				yyjson_mut_val *inl_arr =
				    PandocInlineConvert::ConvertDbInlinesToPandocVal(doc, inline_children, 0, 2, inl_end, 1);
				yyjson_mut_arr_add_val(hc_arr, inl_arr);
			} else {
				yyjson_mut_val *inl_arr = yyjson_mut_arr(doc);
				yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
				yyjson_mut_obj_add_strncpy(doc, str_obj, "c", content.data(), content.size());
				yyjson_mut_arr_add_val(inl_arr, str_obj);
				yyjson_mut_arr_add_val(hc_arr, inl_arr);
			}
			yyjson_mut_obj_add_val(doc, header_obj, "c", hc_arr);
			yyjson_mut_arr_add_val(blocks_arr, header_obj);
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_PARAGRAPH) {
			yyjson_mut_val *para_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, para_obj, "t", "Para");
			if (!inline_children.empty()) {
				idx_t inl_end = 0;
				yyjson_mut_val *inl_arr =
				    PandocInlineConvert::ConvertDbInlinesToPandocVal(doc, inline_children, 0, 2, inl_end, 1);
				yyjson_mut_obj_add_val(doc, para_obj, "c", inl_arr);
			} else {
				yyjson_mut_val *inl_arr = yyjson_mut_arr(doc);
				yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
				yyjson_mut_obj_add_strncpy(doc, str_obj, "c", content.data(), content.size());
				yyjson_mut_arr_add_val(inl_arr, str_obj);
				yyjson_mut_obj_add_val(doc, para_obj, "c", inl_arr);
			}
			yyjson_mut_arr_add_val(blocks_arr, para_obj);
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_CODE) {
			auto language = GetElementAttribute(block, "language");
			yyjson_mut_val *code_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, code_obj, "t", "CodeBlock");
			yyjson_mut_val *cc_arr = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_val(cc_arr, CreatePandocAttrVal(doc, block, language));
			yyjson_mut_arr_add_strncpy(doc, cc_arr, content.data(), content.size());
			yyjson_mut_obj_add_val(doc, code_obj, "c", cc_arr);
			yyjson_mut_arr_add_val(blocks_arr, code_obj);
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_BLOCKQUOTE &&
		           GetElementStringField(block, BlockTypes::ENCODING_IDX) == BlockTypes::ENCODING_JSON &&
		           !content.empty()) {
			yyjson_mut_val *bq_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, bq_obj, "t", "BlockQuote");
			yyjson_doc *sub_doc = yyjson_read(content.c_str(), content.size(), 0);
			if (sub_doc) {
				yyjson_mut_val *imported = yyjson_val_mut_copy(doc, yyjson_doc_get_root(sub_doc));
				yyjson_mut_obj_add_val(doc, bq_obj, "c", imported);
				yyjson_doc_free(sub_doc);
			} else {
				yyjson_mut_obj_add_val(doc, bq_obj, "c", yyjson_mut_arr(doc));
			}
			yyjson_mut_arr_add_val(blocks_arr, bq_obj);
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_BLOCKQUOTE && HasBlockChildren(blocks_list, block_idx)) {
			int32_t quote_level = GetElementLevel(block);
			yyjson_mut_arr_add_val(blocks_arr,
			                       ConvertBlockquoteToPandocVal(doc, blocks_list, block_idx, quote_level, 1));
		} else if (element_type == BlockTypes::TYPE_BLOCKQUOTE) {
			yyjson_mut_val *bq_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, bq_obj, "t", "BlockQuote");
			yyjson_mut_val *bqc_arr = yyjson_mut_arr(doc);
			yyjson_mut_val *para_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, para_obj, "t", "Para");
			if (!inline_children.empty()) {
				idx_t inl_end = 0;
				yyjson_mut_val *inl_arr =
				    PandocInlineConvert::ConvertDbInlinesToPandocVal(doc, inline_children, 0, 2, inl_end, 1);
				yyjson_mut_obj_add_val(doc, para_obj, "c", inl_arr);
			} else if (!content.empty()) {
				yyjson_mut_val *inl_arr = yyjson_mut_arr(doc);
				yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
				yyjson_mut_obj_add_strncpy(doc, str_obj, "c", content.data(), content.size());
				yyjson_mut_arr_add_val(inl_arr, str_obj);
				yyjson_mut_obj_add_val(doc, para_obj, "c", inl_arr);
			} else {
				yyjson_mut_obj_add_val(doc, para_obj, "c", yyjson_mut_arr(doc));
			}
			yyjson_mut_arr_add_val(bqc_arr, para_obj);
			yyjson_mut_obj_add_val(doc, bq_obj, "c", bqc_arr);
			yyjson_mut_arr_add_val(blocks_arr, bq_obj);
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_HR) {
			yyjson_mut_val *hr_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, hr_obj, "t", "HorizontalRule");
			yyjson_mut_arr_add_val(blocks_arr, hr_obj);
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_RAW) {
			auto format = GetElementAttribute(block, "format");
			if (format.empty()) {
				format = "html";
			}
			yyjson_mut_val *raw_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, raw_obj, "t", "RawBlock");
			yyjson_mut_val *rc_arr = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_strncpy(doc, rc_arr, format.data(), format.size());
			yyjson_mut_arr_add_strncpy(doc, rc_arr, content.data(), content.size());
			yyjson_mut_obj_add_val(doc, raw_obj, "c", rc_arr);
			yyjson_mut_arr_add_val(blocks_arr, raw_obj);
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_LIST &&
		           GetElementStringField(block, BlockTypes::ENCODING_IDX) == BlockTypes::ENCODING_JSON &&
		           !content.empty() && !HasBlockChildren(blocks_list, block_idx)) {
			// A spec-1.x list: items packed into content as a JSON array, no children.
			// Nothing produces this any more, but the spec PROMISES stored 1.x block
			// lists keep converting -- and until this branch existed that promise was
			// false: ConvertListToPandocVal walks children, found none, and emitted
			// `[{"t":"BulletList","c":[]}]`. Silent total loss of every item, which is
			// the same defect the 2.0 migration fixed for the reader, still live for
			// stored data. Found by auditing rulings against code rather than trusting
			// what I had written down.
			bool json_ordered = (GetElementAttribute(block, "list_type") == "ordered") ||
			                    (GetElementAttribute(block, "ordered") == "true");
			yyjson_mut_val *list_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, list_obj, "t", json_ordered ? "OrderedList" : "BulletList");
			yyjson_mut_val *items_arr = yyjson_mut_arr(doc);
			yyjson_doc *sub_doc = yyjson_read(content.c_str(), content.size(), 0);
			if (sub_doc) {
				yyjson_val *root = yyjson_doc_get_root(sub_doc);
				if (root && yyjson_is_arr(root)) {
					size_t idx, max;
					yyjson_val *item;
					yyjson_arr_foreach(root, idx, max, item) {
						yyjson_mut_val *item_blocks = yyjson_mut_arr(doc);
						yyjson_mut_val *plain_obj = yyjson_mut_obj(doc);
						yyjson_mut_obj_add_str(doc, plain_obj, "t", "Plain");
						yyjson_mut_val *inl = yyjson_mut_arr(doc);
						if (item && yyjson_is_str(item)) {
							const char *s = yyjson_get_str(item);
							yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
							yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
							yyjson_mut_obj_add_strcpy(doc, str_obj, "c", s ? s : "");
							yyjson_mut_arr_add_val(inl, str_obj);
						}
						yyjson_mut_obj_add_val(doc, plain_obj, "c", inl);
						yyjson_mut_arr_add_val(item_blocks, plain_obj);
						yyjson_mut_arr_add_val(items_arr, item_blocks);
					}
				}
				yyjson_doc_free(sub_doc);
			}
			if (json_ordered) {
				yyjson_mut_val *c_outer = yyjson_mut_arr(doc);
				yyjson_mut_val *spec = yyjson_mut_arr(doc);
				yyjson_mut_arr_add_int(doc, spec, 1);
				yyjson_mut_val *sty = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, sty, "t", "Decimal");
				yyjson_mut_arr_add_val(spec, sty);
				yyjson_mut_val *dlm = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, dlm, "t", "Period");
				yyjson_mut_arr_add_val(spec, dlm);
				yyjson_mut_arr_add_val(c_outer, spec);
				yyjson_mut_arr_add_val(c_outer, items_arr);
				yyjson_mut_obj_add_val(doc, list_obj, "c", c_outer);
			} else {
				yyjson_mut_obj_add_val(doc, list_obj, "c", items_arr);
			}
			yyjson_mut_arr_add_val(blocks_arr, list_obj);
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_LIST) {
			int32_t list_level = GetElementLevel(block);
			yyjson_mut_val *list_obj = ConvertListToPandocVal(doc, blocks_list, block_idx, list_level, 1);
			yyjson_mut_arr_add_val(blocks_arr, list_obj);
		} else if (element_type == BlockTypes::TYPE_DIV) {
			int32_t div_level = GetElementLevel(block);
			yyjson_mut_val *div_obj = ConvertDivToPandocVal(doc, blocks_list, block_idx, div_level, 1);
			yyjson_mut_arr_add_val(blocks_arr, div_obj);
		} else if (element_type == BlockTypes::TYPE_PAGE) {
			// Pandoc has no page constructor. A Div classed `page` carrying the
			// number is its nearest honest equivalent, and reads correctly to
			// anything that understands classes.
			auto page_number = GetElementAttribute(block, "page_number");
			yyjson_mut_val *pg = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, pg, "t", "Div");
			yyjson_mut_val *pg_c = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_val(pg_c, CreatePandocAttrVal(doc, block, "page"));
			yyjson_mut_arr_add_val(pg_c, yyjson_mut_arr(doc)); // a marker owns no blocks
			yyjson_mut_obj_add_val(doc, pg, "c", pg_c);
			yyjson_mut_arr_add_val(blocks_arr, pg);
			// A leaf advances the cursor itself, like TYPE_HR. Container branches
			// advance by reference inside their converter; leaving this out looped
			// forever appending Divs until memory ran out.
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_SECTION) {
			// Pandoc has no Section constructor, so a section degrades to a Div whose
			// class carries the role -- pandoc's nearest honest equivalent, and what
			// its own HTML reader produces for <section>.
			int32_t sec_level = GetElementLevel(block);
			auto role = GetElementAttribute(block, "role");
			yyjson_mut_val *sec_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, sec_obj, "t", "Div");
			yyjson_mut_val *sec_c = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_val(sec_c, CreatePandocAttrVal(doc, block, role.empty() ? "section" : role));
			yyjson_mut_val *sec_children = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_val(sec_c, sec_children);
			yyjson_mut_obj_add_val(doc, sec_obj, "c", sec_c);
			ConvertContainerChildrenToPandocVal(doc, blocks_list, block_idx, sec_level, 1, sec_children, nullptr,
			                                    nullptr);
			yyjson_mut_arr_add_val(blocks_arr, sec_obj);
		} else if (element_type == BlockTypes::TYPE_FIGURE) {
			// A top-level figure has NULL level; treat it as 1 so its children, which
			// sit at 2, are correctly seen as deeper.
			int32_t fig_level = GetElementLevel(block);
			if (fig_level == 0) {
				fig_level = 1;
			}
			yyjson_mut_val *fig_obj = ConvertFigureToPandocVal(doc, blocks_list, block_idx, fig_level, 1);
			yyjson_mut_arr_add_val(blocks_arr, fig_obj);
		} else if (element_type == BlockTypes::TYPE_TABLE) {
			yyjson_mut_val *tbl_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, tbl_obj, "t", "Table");
			if (!content.empty()) {
				yyjson_doc *sub_doc = yyjson_read(content.c_str(), content.size(), 0);
				if (sub_doc) {
					yyjson_mut_val *imported = yyjson_val_mut_copy(doc, yyjson_doc_get_root(sub_doc));
					yyjson_mut_obj_add_val(doc, tbl_obj, "c", imported);
					yyjson_doc_free(sub_doc);
				} else {
					yyjson_mut_obj_add_val(doc, tbl_obj, "c", yyjson_mut_arr(doc));
				}
			} else {
				yyjson_mut_obj_add_val(doc, tbl_obj, "c", yyjson_mut_arr(doc));
			}
			yyjson_mut_arr_add_val(blocks_arr, tbl_obj);
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_IMAGE) {
			auto src = GetElementAttribute(block, "src");
			auto alt = GetElementAttribute(block, "alt");
			auto title = GetElementAttribute(block, "title");

			yyjson_mut_val *para_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, para_obj, "t", "Para");
			yyjson_mut_val *pc_arr = yyjson_mut_arr(doc);
			yyjson_mut_val *img_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, img_obj, "t", "Image");
			yyjson_mut_val *ic_arr = yyjson_mut_arr(doc);

			yyjson_mut_val *attr_arr = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_str(doc, attr_arr, "");
			yyjson_mut_arr_add_val(attr_arr, yyjson_mut_arr(doc));
			yyjson_mut_arr_add_val(attr_arr, yyjson_mut_arr(doc));
			yyjson_mut_arr_add_val(ic_arr, attr_arr);

			yyjson_mut_val *alt_arr = yyjson_mut_arr(doc);
			yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
			yyjson_mut_obj_add_strncpy(doc, str_obj, "c", alt.data(), alt.size());
			yyjson_mut_arr_add_val(alt_arr, str_obj);
			yyjson_mut_arr_add_val(ic_arr, alt_arr);

			yyjson_mut_val *tgt_arr = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_strncpy(doc, tgt_arr, src.data(), src.size());
			yyjson_mut_arr_add_strncpy(doc, tgt_arr, title.data(), title.size());
			yyjson_mut_arr_add_val(ic_arr, tgt_arr);

			yyjson_mut_obj_add_val(doc, img_obj, "c", ic_arr);
			yyjson_mut_arr_add_val(pc_arr, img_obj);
			yyjson_mut_obj_add_val(doc, para_obj, "c", pc_arr);
			yyjson_mut_arr_add_val(blocks_arr, para_obj);
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_PLAIN) {
			// Without this branch a `plain` fell to the terminal Para fallback, losing
			// the very distinction the type exists to carry. Same shape as the deflist
			// and lineblock gaps found by sweeping the exporter earlier today -- a new
			// type needs an export branch or it silently becomes something else.
			yyjson_mut_val *plain_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, plain_obj, "t", "Plain");
			if (!inline_children.empty()) {
				idx_t inl_end = 0;
				yyjson_mut_obj_add_val(
				    doc, plain_obj, "c",
				    PandocInlineConvert::ConvertDbInlinesToPandocVal(doc, inline_children, 0, 2, inl_end, 1));
			} else {
				yyjson_mut_val *inl_arr = yyjson_mut_arr(doc);
				yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
				yyjson_mut_obj_add_strncpy(doc, str_obj, "c", content.data(), content.size());
				yyjson_mut_arr_add_val(inl_arr, str_obj);
				yyjson_mut_obj_add_val(doc, plain_obj, "c", inl_arr);
			}
			yyjson_mut_arr_add_val(blocks_arr, plain_obj);
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_DEFLIST &&
		           GetElementStringField(block, BlockTypes::ENCODING_IDX) == BlockTypes::ENCODING_JSON &&
		           !content.empty()) {
			// Found by sweeping every block type through the exporter after fixing the
			// same defect for `generic`: deflist had NO export branch, so it fell to the
			// terminal Para fallback and came back out as a paragraph whose visible text
			// was its own raw AST. Splicing the stored tuple back makes it lossless.
			yyjson_mut_val *dl_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, dl_obj, "t", "DefinitionList");
			yyjson_doc *sub_doc = yyjson_read(content.c_str(), content.size(), 0);
			if (sub_doc) {
				yyjson_mut_obj_add_val(doc, dl_obj, "c", yyjson_val_mut_copy(doc, yyjson_doc_get_root(sub_doc)));
				yyjson_doc_free(sub_doc);
			} else {
				yyjson_mut_obj_add_val(doc, dl_obj, "c", yyjson_mut_arr(doc));
			}
			yyjson_mut_arr_add_val(blocks_arr, dl_obj);
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_LINEBLOCK) {
			// Same sweep, same fallback. LineBlock c = [[Inline]] -- one inline array per
			// line -- and the reader stores the lines newline-separated in content, so
			// the split is the inverse of the join it did on the way in.
			yyjson_mut_val *lb_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, lb_obj, "t", "LineBlock");
			yyjson_mut_val *lines_arr = yyjson_mut_arr(doc);
			size_t line_start = 0;
			while (line_start <= content.size()) {
				size_t nl = content.find('\n', line_start);
				string line = content.substr(line_start, nl == string::npos ? string::npos : nl - line_start);
				yyjson_mut_val *line_arr = yyjson_mut_arr(doc);
				if (!line.empty()) {
					yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
					yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
					yyjson_mut_obj_add_strncpy(doc, str_obj, "c", line.data(), line.size());
					yyjson_mut_arr_add_val(line_arr, str_obj);
				}
				yyjson_mut_arr_add_val(lines_arr, line_arr);
				if (nl == string::npos) {
					break;
				}
				line_start = nl + 1;
			}
			yyjson_mut_obj_add_val(doc, lb_obj, "c", lines_arr);
			yyjson_mut_arr_add_val(blocks_arr, lb_obj);
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_GENERIC &&
		           GetElementStringField(block, BlockTypes::ENCODING_IDX) == BlockTypes::ENCODING_JSON &&
		           !content.empty()) {
			// The export half the import side already promised. ProcessPandocBlockVal
			// stores the WHOLE constructor object -- `t` included -- and says in its
			// comment that it does so "so export can reconstitute it including `t`".
			// Nothing did. An unmapped block fell through to the Para fallback below,
			// which emitted its stored JSON as a Str: a round trip printed a raw AST
			// blob into the document body as visible text. That is worse than a drop,
			// because the corruption looks like content.
			//
			// Splicing the object back verbatim round-trips the block LOSSLESSLY --
			// stronger than the inline side manages, where `generic` can only degrade
			// to a Span carrying source_type as a class, its children having become
			// separate elements by the time export sees them.
			yyjson_doc *sub_doc = yyjson_read(content.c_str(), content.size(), 0);
			if (sub_doc) {
				yyjson_mut_arr_add_val(blocks_arr, yyjson_val_mut_copy(doc, yyjson_doc_get_root(sub_doc)));
				yyjson_doc_free(sub_doc);
			} else {
				// Unparseable content: falling through would re-print the blob as text,
				// so emit an empty Div tagged with source_type instead. Lossy in
				// content, but it does not fabricate prose the document never had.
				yyjson_mut_val *gen_obj = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, gen_obj, "t", "Div");
				yyjson_mut_val *gc_arr = yyjson_mut_arr(doc);
				yyjson_mut_arr_add_val(gc_arr,
				                       CreatePandocAttrVal(doc, block, GetElementAttribute(block, "source_type")));
				yyjson_mut_arr_add_val(gc_arr, yyjson_mut_arr(doc));
				yyjson_mut_obj_add_val(doc, gen_obj, "c", gc_arr);
				yyjson_mut_arr_add_val(blocks_arr, gen_obj);
			}
			block_idx++;
		} else {
			yyjson_mut_val *para_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, para_obj, "t", "Para");
			if (!inline_children.empty()) {
				idx_t inl_end = 0;
				yyjson_mut_val *inl_arr =
				    PandocInlineConvert::ConvertDbInlinesToPandocVal(doc, inline_children, 0, 2, inl_end, 1);
				yyjson_mut_obj_add_val(doc, para_obj, "c", inl_arr);
			} else {
				yyjson_mut_val *inl_arr = yyjson_mut_arr(doc);
				yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
				yyjson_mut_obj_add_strncpy(doc, str_obj, "c", content.data(), content.size());
				yyjson_mut_arr_add_val(inl_arr, str_obj);
				yyjson_mut_obj_add_val(doc, para_obj, "c", inl_arr);
			}
			yyjson_mut_arr_add_val(blocks_arr, para_obj);
			block_idx++;
		}
	}

	size_t len = 0;
	char *json_out = yyjson_mut_write(doc, 0, &len);
	string res(json_out ? json_out : "[]", len);
	if (json_out) {
		free(json_out);
	}
	yyjson_mut_doc_free(doc);
	return res;
}

static LogicalType GetPandocAstType() {
	child_list_t<LogicalType> struct_children;
	struct_children.push_back(make_pair("pandoc-api-version", LogicalType::LIST(LogicalType::INTEGER)));
	struct_children.push_back(make_pair("meta", LogicalType::JSON()));
	struct_children.push_back(make_pair("blocks", LogicalType::JSON()));
	return LogicalType::STRUCT(std::move(struct_children));
}

// Rebuild one MetaValue from the kind='value' element at `i`, advancing `i` past it
// and everything nested beneath it.
static yyjson_mut_val *BuildMetaValueJson(yyjson_mut_doc *doc, const vector<Value> &blocks_list, idx_t &i,
                                          int32_t my_level, idx_t depth) {
	CheckPandocDepth(depth);
	auto &el = blocks_list[i];
	auto vtype = GetElementStringField(el, BlockTypes::ELEMENT_TYPE_IDX);
	auto content = GetElementStringField(el, BlockTypes::CONTENT_IDX);
	i++; // consume the value element itself

	yyjson_mut_val *obj = yyjson_mut_obj(doc);

	if (vtype == BlockTypes::VALUE_STRING) {
		yyjson_mut_obj_add_str(doc, obj, "t", "MetaString");
		yyjson_mut_obj_add_strncpy(doc, obj, "c", content.data(), content.size());
		return obj;
	}
	if (vtype == BlockTypes::VALUE_BOOL) {
		yyjson_mut_obj_add_str(doc, obj, "t", "MetaBool");
		yyjson_mut_obj_add_bool(doc, obj, "c", content == "true");
		return obj;
	}
	if (vtype == BlockTypes::TYPE_GENERIC) {
		// Preserved verbatim on the way in; hand it back unchanged.
		yyjson_doc *parsed = yyjson_read(content.c_str(), content.size(), 0);
		if (parsed) {
			yyjson_mut_val *copied = yyjson_val_mut_copy(doc, yyjson_doc_get_root(parsed));
			yyjson_doc_free(parsed);
			if (copied) {
				return copied;
			}
		}
		return nullptr;
	}
	if (vtype == BlockTypes::VALUE_INLINES) {
		yyjson_mut_obj_add_str(doc, obj, "t", "MetaInlines");
		vector<Value> inls;
		while (i < blocks_list.size()) {
			auto &child = blocks_list[i];
			if (child.IsNull()) {
				i++;
				continue;
			}
			if (GetElementStringField(child, BlockTypes::KIND_IDX) != BlockTypes::KIND_INLINE) {
				break;
			}
			inls.push_back(child);
			i++;
		}
		idx_t end_idx = 0;
		yyjson_mut_val *arr =
		    inls.empty()
		        ? yyjson_mut_arr(doc)
		        : PandocInlineConvert::ConvertDbInlinesToPandocVal(doc, inls, 0, my_level + 1, end_idx, depth + 1);
		yyjson_mut_obj_add_val(doc, obj, "c", arr);
		return obj;
	}

	// Container shapes: consume every element nested deeper than this one.
	const bool is_map = (vtype == BlockTypes::VALUE_MAP);
	const bool is_blocks = (vtype == BlockTypes::VALUE_BLOCKS);
	yyjson_mut_obj_add_str(doc, obj, "t", is_map ? "MetaMap" : (is_blocks ? "MetaBlocks" : "MetaList"));
	yyjson_mut_val *container = is_map ? yyjson_mut_obj(doc) : yyjson_mut_arr(doc);

	while (i < blocks_list.size()) {
		auto &child = blocks_list[i];
		if (child.IsNull()) {
			i++;
			continue;
		}
		auto child_kind = GetElementStringField(child, BlockTypes::KIND_IDX);
		int32_t child_level = GetElementLevel(child);
		if (child_kind == BlockTypes::KIND_VALUE && child_level <= my_level) {
			break;
		}
		if (child_kind != BlockTypes::KIND_VALUE) {
			// MetaBlocks children are ordinary blocks; anything else here is not ours.
			if (!is_blocks) {
				break;
			}
			i++;
			continue;
		}
		string key = GetElementAttribute(child, "key");
		yyjson_mut_val *v = BuildMetaValueJson(doc, blocks_list, i, child_level, depth + 1);
		if (!v) {
			continue;
		}
		if (is_map) {
			yyjson_mut_obj_add(container, yyjson_mut_strncpy(doc, key.data(), key.size()), v);
		} else {
			yyjson_mut_arr_add_val(container, v);
		}
	}
	yyjson_mut_obj_add_val(doc, obj, "c", container);
	return obj;
}

// Reconstitute the document's `meta` object from its kind='value' elements.
static string BuildMetaJson(const vector<Value> &blocks_list) {
	yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val *root = yyjson_mut_obj(doc);

	idx_t i = 0;
	while (i < blocks_list.size()) {
		auto &el = blocks_list[i];
		if (el.IsNull()) {
			i++;
			continue;
		}
		if (GetElementStringField(el, BlockTypes::KIND_IDX) != BlockTypes::KIND_VALUE || GetElementLevel(el) != 1) {
			i++;
			continue;
		}
		string key = GetElementAttribute(el, "key");
		yyjson_mut_val *v = BuildMetaValueJson(doc, blocks_list, i, 1, 1);
		if (v && !key.empty()) {
			yyjson_mut_obj_add(root, yyjson_mut_strncpy(doc, key.data(), key.size()), v);
		}
	}

	yyjson_mut_doc_set_root(doc, root);
	size_t len = 0;
	char *json = yyjson_mut_write(doc, 0, &len);
	string res(json ? json : "{}", json ? len : 2);
	if (json) {
		free(json);
	}
	yyjson_mut_doc_free(doc);
	return res;
}

static void DuckBlocksToPandocAstFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		// pandoc 3.x rejects anything below [1,23] outright; [1,20] made every export
		// unreadable by the installed pandoc.
		vector<Value> api_version_vals = {Value::INTEGER(1), Value::INTEGER(23), Value::INTEGER(1)};
		Value api_version = Value::LIST(LogicalType::INTEGER, api_version_vals);
		Value meta = Value("{}");

		if (blocks_val.IsNull()) {
			child_list_t<Value> struct_values;
			struct_values.push_back(make_pair("pandoc-api-version", api_version));
			struct_values.push_back(make_pair("meta", meta));
			struct_values.push_back(make_pair("blocks", Value("[]")));
			result.SetValue(i, Value::STRUCT(std::move(struct_values)));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);
		string blocks_json = BuildBlocksJson(blocks_list);
		meta = Value(BuildMetaJson(blocks_list));

		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("pandoc-api-version", api_version));
		struct_values.push_back(make_pair("meta", meta));
		struct_values.push_back(make_pair("blocks", Value(blocks_json)));
		result.SetValue(i, Value::STRUCT(std::move(struct_values)));
	}
}

void PandocBlockConvert::DuckBlocksToPandocBlocksFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		if (blocks_val.IsNull()) {
			result.SetValue(i, Value("[]"));
			continue;
		}

		auto &blocks_list = ListValue::GetChildren(blocks_val);
		result.SetValue(i, Value(BuildBlocksJson(blocks_list)));
	}
}

static yyjson_mut_val *MapToMetaObj(yyjson_mut_doc *doc, const Value &meta_map) {
	yyjson_mut_val *meta_obj = yyjson_mut_obj(doc);
	if (meta_map.IsNull()) {
		return meta_obj;
	}

	auto &map_entries = MapValue::GetChildren(meta_map);
	for (auto &entry : map_entries) {
		if (entry.IsNull()) {
			continue;
		}
		auto &kv = StructValue::GetChildren(entry);
		if (kv.size() < 2 || kv[0].IsNull() || kv[1].IsNull()) {
			continue;
		}

		string key = kv[0].GetValue<string>();
		string value = kv[1].GetValue<string>();

		yyjson_mut_val *meta_val_obj = yyjson_mut_obj(doc);
		yyjson_mut_obj_add_str(doc, meta_val_obj, "t", "MetaInlines");
		yyjson_mut_val *c_arr = yyjson_mut_arr(doc);
		yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
		yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
		yyjson_mut_obj_add_strncpy(doc, str_obj, "c", value.data(), value.size());
		yyjson_mut_arr_add_val(c_arr, str_obj);
		yyjson_mut_obj_add_val(doc, meta_val_obj, "c", c_arr);

		yyjson_mut_val *key_val = yyjson_mut_strncpy(doc, key.data(), key.size());
		yyjson_mut_obj_add(meta_obj, key_val, meta_val_obj);
	}
	return meta_obj;
}

void PandocBlockConvert::ReadPandocAstFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &path_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto path_val = path_vec.GetValue(i);

		if (path_val.IsNull()) {
			result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), vector<Value>()));
			continue;
		}

		string file_path = path_val.GetValue<string>();

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

struct PandocAstBindData : public TableFunctionData {
	vector<Value> blocks;
	string meta_json = "{}";
	vector<int32_t> api_version = {1, 23, 1};
	bool done = false;
};

static string ConvertMetaMapToJson(const Value &meta_map) {
	if (meta_map.IsNull()) {
		return "{}";
	}

	auto &map_entries = MapValue::GetChildren(meta_map);
	if (map_entries.empty()) {
		return "{}";
	}

	yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val *root = yyjson_mut_obj(doc);
	yyjson_mut_doc_set_root(doc, root);

	for (auto &entry : map_entries) {
		if (entry.IsNull()) {
			continue;
		}
		auto &kv = StructValue::GetChildren(entry);
		if (kv.size() < 2 || kv[0].IsNull() || kv[1].IsNull()) {
			continue;
		}

		string key = kv[0].GetValue<string>();
		string value = kv[1].GetValue<string>();

		yyjson_mut_val *meta_obj = yyjson_mut_obj(doc);
		yyjson_mut_obj_add_str(doc, meta_obj, "t", "MetaInlines");
		yyjson_mut_val *c_arr = yyjson_mut_arr(doc);
		yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
		yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
		yyjson_mut_obj_add_strncpy(doc, str_obj, "c", value.data(), value.size());
		yyjson_mut_arr_add_val(c_arr, str_obj);
		yyjson_mut_obj_add_val(doc, meta_obj, "c", c_arr);

		yyjson_mut_val *key_val = yyjson_mut_strncpy(doc, key.data(), key.size());
		yyjson_mut_obj_add(root, key_val, meta_obj);
	}

	size_t len = 0;
	char *json = yyjson_mut_write(doc, 0, &len);
	string res(json ? json : "{}", len);
	if (json) {
		free(json);
	}
	yyjson_mut_doc_free(doc);
	return res;
}

static unique_ptr<FunctionData> PandocAstBind(ClientContext &context, TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<PandocAstBindData>();

	if (!input.inputs.empty() && !input.inputs[0].IsNull()) {
		auto &blocks_val = input.inputs[0];
		auto &blocks_list = ListValue::GetChildren(blocks_val);
		for (auto &block : blocks_list) {
			result->blocks.push_back(block);
		}
	}

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

	vector<Value> api_version_vals;
	for (auto v : bind_data.api_version) {
		api_version_vals.push_back(Value::INTEGER(v));
	}
	Value api_version = Value::LIST(LogicalType::INTEGER, api_version_vals);

	string blocks_json = BuildBlocksJson(bind_data.blocks);

	CompatSetOutputCardinality(output, 1);
	output.data[0].SetValue(0, api_version);
	output.data[1].SetValue(0, Value(bind_data.meta_json));
	output.data[2].SetValue(0, Value(blocks_json));

	bind_data.done = true;
}

static void WritePandocAstFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &path_vec = args.data[0];
	auto &blocks_vec = args.data[1];
	auto count = args.size();

	string api_version = "[1,23,1]";

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

	// pandoc_ast(blocks, meta := {}, api_version := [1,23,1]) -> TABLE(pandoc-api-version, meta, blocks)
	// Table function for clean JSON output with COPY FORMAT JSON
	// meta is MAP(VARCHAR, VARCHAR) - simple key-value pairs converted to Pandoc MetaInlines
	TableFunction pandoc_ast_table_func("pandoc_ast", {duck_block_list_type}, PandocAstFunction, PandocAstBind);
	pandoc_ast_table_func.named_parameters["meta"] = LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR);
	pandoc_ast_table_func.named_parameters["api_version"] = LogicalType::LIST(LogicalType::INTEGER);
	loader.RegisterFunction(pandoc_ast_table_func);
}

} // namespace duckdb
