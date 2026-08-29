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
	const Value block_level = (parent_div_level == 0) ? Value() : Value(effective_level);

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
		block_type = BlockTypes::TYPE_PARAGRAPH;
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
		block_type = BlockTypes::TYPE_BLOCKQUOTE;
		encoding = "json";
		content = ValToJsonString(c_val);
	} else if (strcmp(pandoc_type, "BulletList") == 0 || strcmp(pandoc_type, "OrderedList") == 0) {
		block_type = BlockTypes::TYPE_LIST;
		attrs["list_type"] = (strcmp(pandoc_type, "BulletList") == 0) ? "bullet" : "ordered";
		encoding = "json";
		content = ValToJsonString(c_val);
	} else if (strcmp(pandoc_type, "Table") == 0) {
		block_type = BlockTypes::TYPE_TABLE;
		encoding = "json";
		content = ValToJsonString(c_val);
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
		return;
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
	yyjson_mut_arr_add_strn(doc, attr_arr, id.data(), id.size());

	yyjson_mut_val *classes_arr = yyjson_mut_arr(doc);
	size_t start = 0;
	while (start < classes.length()) {
		size_t space = classes.find(' ', start);
		size_t len = (space == string::npos) ? string::npos : space - start;
		string cls = classes.substr(start, len);
		if (!cls.empty()) {
			yyjson_mut_arr_add_strn(doc, classes_arr, cls.data(), cls.size());
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
			yyjson_mut_arr_add_strn(doc, pair_arr, key.data(), key.size());
			yyjson_mut_arr_add_strn(doc, pair_arr, val.data(), val.size());
			yyjson_mut_arr_add_val(kvs_arr, pair_arr);
		}
	}
	yyjson_mut_arr_add_val(attr_arr, kvs_arr);
	return attr_arr;
}

static yyjson_mut_val *ConvertListToPandocVal(yyjson_mut_doc *doc, const vector<Value> &blocks_list, idx_t &start_idx,
                                              int32_t list_level, idx_t depth);
static yyjson_mut_val *ConvertDivToPandocVal(yyjson_mut_doc *doc, const vector<Value> &blocks_list, idx_t &start_idx,
                                             int32_t div_level, idx_t depth);

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
			} else if (child_type == BlockTypes::TYPE_LIST && child_level == list_level + 1) {
				if (in_item) {
					current_item.nested_list_val = ConvertListToPandocVal(doc, blocks_list, j, child_level, depth + 1);
				} else {
					j++;
				}
			} else if (child_level <= list_level) {
				break;
			} else {
				j++;
			}
		} else if (child_kind == BlockTypes::KIND_INLINE && in_item) {
			if (child_level == list_level + 2) {
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
		c_outer = yyjson_mut_arr(doc);
		yyjson_mut_val *order_spec = yyjson_mut_arr(doc);
		yyjson_mut_arr_add_int(doc, order_spec, 1);
		yyjson_mut_val *style_obj = yyjson_mut_obj(doc);
		yyjson_mut_obj_add_str(doc, style_obj, "t", "Decimal");
		yyjson_mut_arr_add_val(order_spec, style_obj);
		yyjson_mut_val *delim_obj = yyjson_mut_obj(doc);
		yyjson_mut_obj_add_str(doc, delim_obj, "t", "Period");
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
		yyjson_mut_obj_add_str(doc, plain_obj, "t", "Plain");

		if (!item.inlines.empty()) {
			idx_t inl_end = 0;
			yyjson_mut_val *inl_arr =
			    PandocInlineConvert::ConvertDbInlinesToPandocVal(doc, item.inlines, 0, list_level + 2, inl_end, 1);
			yyjson_mut_obj_add_val(doc, plain_obj, "c", inl_arr);
		} else if (!item.content.empty()) {
			yyjson_mut_val *inl_arr = yyjson_mut_arr(doc);
			yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
			yyjson_mut_obj_add_strn(doc, str_obj, "c", item.content.data(), item.content.size());
			yyjson_mut_arr_add_val(inl_arr, str_obj);
			yyjson_mut_obj_add_val(doc, plain_obj, "c", inl_arr);
		} else {
			yyjson_mut_obj_add_val(doc, plain_obj, "c", yyjson_mut_arr(doc));
		}
		yyjson_mut_arr_add_val(item_blocks, plain_obj);

		if (item.nested_list_val) {
			yyjson_mut_arr_add_val(item_blocks, item.nested_list_val);
		}
		yyjson_mut_arr_add_val(items_arr, item_blocks);
	}

	return root_obj;
}

static yyjson_mut_val *ConvertDivToPandocVal(yyjson_mut_doc *doc, const vector<Value> &blocks_list, idx_t &start_idx,
                                             int32_t div_level, idx_t depth) {
	CheckPandocDepth(depth);
	auto &div_block = blocks_list[start_idx];

	yyjson_mut_val *div_obj = yyjson_mut_obj(doc);
	yyjson_mut_obj_add_str(doc, div_obj, "t", "Div");

	yyjson_mut_val *c_arr = yyjson_mut_arr(doc);
	yyjson_mut_val *attr_val = CreatePandocAttrVal(doc, div_block, "");
	yyjson_mut_arr_add_val(c_arr, attr_val);

	yyjson_mut_val *child_blocks_arr = yyjson_mut_arr(doc);
	yyjson_mut_arr_add_val(c_arr, child_blocks_arr);
	yyjson_mut_obj_add_val(doc, div_obj, "c", c_arr);

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

		if (child_level <= div_level && child_kind == BlockTypes::KIND_BLOCK) {
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

			if (child_type == BlockTypes::TYPE_PARAGRAPH) {
				yyjson_mut_val *para_obj = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_str(doc, para_obj, "t", "Para");
				if (!inline_children.empty()) {
					idx_t inl_end = 0;
					yyjson_mut_val *inl_arr = PandocInlineConvert::ConvertDbInlinesToPandocVal(
					    doc, inline_children, 0, child_level + 1, inl_end, 1);
					yyjson_mut_obj_add_val(doc, para_obj, "c", inl_arr);
				} else {
					yyjson_mut_val *inl_arr = yyjson_mut_arr(doc);
					yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
					yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
					yyjson_mut_obj_add_strn(doc, str_obj, "c", content.data(), content.size());
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
					yyjson_mut_obj_add_strn(doc, str_obj, "c", content.data(), content.size());
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
				yyjson_mut_arr_add_strn(doc, cc_arr, content.data(), content.size());
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
					yyjson_mut_obj_add_strn(doc, str_obj, "c", content.data(), content.size());
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
			} else {
				j++;
			}
		} else {
			j++;
		}
	}

	start_idx = j;
	return div_obj;
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
				yyjson_mut_obj_add_strn(doc, str_obj, "c", content.data(), content.size());
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
				yyjson_mut_obj_add_strn(doc, str_obj, "c", content.data(), content.size());
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
			yyjson_mut_arr_add_strn(doc, cc_arr, content.data(), content.size());
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
				yyjson_mut_obj_add_strn(doc, str_obj, "c", content.data(), content.size());
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
			yyjson_mut_arr_add_strn(doc, rc_arr, format.data(), format.size());
			yyjson_mut_arr_add_strn(doc, rc_arr, content.data(), content.size());
			yyjson_mut_obj_add_val(doc, raw_obj, "c", rc_arr);
			yyjson_mut_arr_add_val(blocks_arr, raw_obj);
			block_idx++;
		} else if (element_type == BlockTypes::TYPE_LIST) {
			int32_t list_level = GetElementLevel(block);
			yyjson_mut_val *list_obj = ConvertListToPandocVal(doc, blocks_list, block_idx, list_level, 1);
			yyjson_mut_arr_add_val(blocks_arr, list_obj);
		} else if (element_type == BlockTypes::TYPE_DIV) {
			int32_t div_level = GetElementLevel(block);
			yyjson_mut_val *div_obj = ConvertDivToPandocVal(doc, blocks_list, block_idx, div_level, 1);
			yyjson_mut_arr_add_val(blocks_arr, div_obj);
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
			yyjson_mut_obj_add_strn(doc, str_obj, "c", alt.data(), alt.size());
			yyjson_mut_arr_add_val(alt_arr, str_obj);
			yyjson_mut_arr_add_val(ic_arr, alt_arr);

			yyjson_mut_val *tgt_arr = yyjson_mut_arr(doc);
			yyjson_mut_arr_add_strn(doc, tgt_arr, src.data(), src.size());
			yyjson_mut_arr_add_strn(doc, tgt_arr, title.data(), title.size());
			yyjson_mut_arr_add_val(ic_arr, tgt_arr);

			yyjson_mut_obj_add_val(doc, img_obj, "c", ic_arr);
			yyjson_mut_arr_add_val(pc_arr, img_obj);
			yyjson_mut_obj_add_val(doc, para_obj, "c", pc_arr);
			yyjson_mut_arr_add_val(blocks_arr, para_obj);
			block_idx++;
		} else {
			yyjson_mut_val *para_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, para_obj, "t", "Para");
			yyjson_mut_val *inl_arr = yyjson_mut_arr(doc);
			yyjson_mut_val *str_obj = yyjson_mut_obj(doc);
			yyjson_mut_obj_add_str(doc, str_obj, "t", "Str");
			yyjson_mut_obj_add_strn(doc, str_obj, "c", content.data(), content.size());
			yyjson_mut_arr_add_val(inl_arr, str_obj);
			yyjson_mut_obj_add_val(doc, para_obj, "c", inl_arr);
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

static void DuckBlocksToPandocAstFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);

		vector<Value> api_version_vals = {Value::INTEGER(1), Value::INTEGER(20)};
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
	vector<int32_t> api_version = {1, 20};
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
		yyjson_mut_obj_add_strn(doc, str_obj, "c", value.data(), value.size());
		yyjson_mut_arr_add_val(c_arr, str_obj);
		yyjson_mut_obj_add_val(doc, meta_obj, "c", c_arr);

		yyjson_mut_obj_add_val(doc, root, key.c_str(), meta_obj);
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
