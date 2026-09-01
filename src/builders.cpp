#include "builders.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"

namespace duckdb {

// Helper to create an attributes MAP from a std::map
static Value CreateAttributesMap(const map<string, string> &attrs) {
	vector<Value> keys;
	vector<Value> values;
	for (auto &entry : attrs) {
		keys.push_back(Value(entry.first));
		values.push_back(Value(entry.second));
	}
	return Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, keys, values);
}

// Helper to set all struct fields for a block element
static void SetBlockFields(vector<unique_ptr<Vector>> &entries, idx_t i, const char *element_type, const Value &content,
                           const Value &level, const char *encoding, const Value &attributes) {
	entries[BlockTypes::KIND_IDX]->SetValue(i, Value(BlockTypes::KIND_BLOCK));
	entries[BlockTypes::ELEMENT_TYPE_IDX]->SetValue(i, Value(element_type));
	entries[BlockTypes::CONTENT_IDX]->SetValue(i, content);
	entries[BlockTypes::LEVEL_IDX]->SetValue(i, level);
	entries[BlockTypes::ENCODING_IDX]->SetValue(i, Value(encoding));
	entries[BlockTypes::ATTRIBUTES_IDX]->SetValue(i, attributes);
	entries[BlockTypes::ELEMENT_ORDER_IDX]->SetValue(i, Value(0));
}

Value BuilderFunctions::CreateBlock(const string &block_type, const string &content, const Value &level,
                                    const string &encoding, const map<string, string> &attributes,
                                    int32_t block_order) {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
	struct_values.push_back(make_pair("element_type", Value(block_type)));
	struct_values.push_back(make_pair("content", Value(content)));
	struct_values.push_back(make_pair("level", level));
	struct_values.push_back(make_pair("encoding", Value(encoding)));
	struct_values.push_back(make_pair("attributes", CreateAttributesMap(attributes)));
	struct_values.push_back(make_pair("element_order", Value(block_order)));

	return Value::STRUCT(std::move(struct_values));
}

// ============================================================================
// V2 API: Core utilities
// ============================================================================

Value BuilderFunctions::CreateBlockWithNullContent(const string &block_type, const string &kind, const Value &level,
                                                   const string &encoding, const map<string, string> &attributes) {
	child_list_t<Value> struct_values;
	struct_values.push_back(make_pair("kind", Value(kind)));
	struct_values.push_back(make_pair("element_type", Value(block_type)));
	struct_values.push_back(make_pair("content", Value(LogicalType::VARCHAR))); // Typed NULL content
	// A BLOCK at top level carries a NULL level: `level` is structural nesting
	// DEPTH, and something not nested has no depth. This matches
	// pandoc_ast_to_blocks() and the spec, and is normalised HERE rather than at
	// each call site because three separate places were stamping 1 -- the
	// flattener, assembly, and every v2 builder -- so a fix in one was silently
	// undone by the next. Inlines are unaffected: they legitimately start at 1.
	// Every element carries an EXPLICIT level -- see the note on BuildWithContent.
	// A NULL arriving here means "unspecified", which for a block is depth 1.
	const Value normalised_level = (kind == BlockTypes::KIND_BLOCK && level.IsNull()) ? Value(1) : level;
	struct_values.push_back(make_pair("level", normalised_level));
	struct_values.push_back(make_pair("encoding", Value(encoding)));
	struct_values.push_back(make_pair("attributes", CreateAttributesMap(attributes)));
	struct_values.push_back(make_pair("element_order", Value(0)));

	return Value::STRUCT(std::move(struct_values));
}

// Helper to create a child element with adjusted level and order
static Value CreateChildWithLevelAndOrder(const Value &element, int32_t new_level, int32_t new_order) {
	auto children = StructValue::GetChildren(element);
	children[BlockTypes::LEVEL_IDX] = Value(new_level);
	children[BlockTypes::ELEMENT_ORDER_IDX] = Value(new_order);
	return Value::STRUCT(BlockTypes::DuckBlockType(), std::move(children));
}

vector<Value> BuilderFunctions::BuildWithContent(const Value &parent_block, const Value &content_input,
                                                 int32_t base_level) {
	vector<Value> result;

	// Get parent block children for modification
	auto parent_children = StructValue::GetChildren(parent_block);
	// Every element carries an EXPLICIT structural level -- there are no NULLs.
	// `level` is depth in a depth-first ordering, and level plus adjacency together
	// describe the whole document tree, which is why it cannot be optional.
	// (Teague, 2026-08-31: this was always the rule; the NULL-at-top-level
	// normalisation was never approved. Spec 3.0 restores it.)
	parent_children[BlockTypes::LEVEL_IDX] = Value(base_level);
	parent_children[BlockTypes::ELEMENT_ORDER_IDX] = Value(0);

	if (content_input.IsNull()) {
		// NULL content - keep parent content as-is (already NULL from CreateBlockWithNullContent)
		result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));
	} else if (content_input.type().id() == LogicalTypeId::VARCHAR) {
		// A string argument IS a single text child, so it lands in `content` -- spec
		// v1.0's rule, which covers inline and block containers in one sentence.
		//
		// Spec 2.0 briefly replaced this for BLOCK containers with "a container never
		// carries content", wrapping the text in a paragraph child. That was broader
		// than the defect required: what needed fixing was `list` storing a JSON items
		// array, not the content rule. It also left blocks and inlines on two
		// different rules, since inline containers never stopped following v1.
		// Restored on Teague's ruling to stay close to v1.
		parent_children[BlockTypes::CONTENT_IDX] = content_input;
		result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));
	} else if (content_input.type().id() == LogicalTypeId::STRUCT) {
		// Single duck_block child - parent content stays NULL, add child at level+1
		result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));

		// Add child at level+1
		auto child_children = StructValue::GetChildren(content_input);
		child_children[BlockTypes::LEVEL_IDX] = Value(base_level + 1);
		child_children[BlockTypes::ELEMENT_ORDER_IDX] = Value(0);
		result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(child_children)));
	} else if (content_input.type().id() == LogicalTypeId::LIST) {
		// LIST(duck_block) children - parent content stays NULL, add children at level+1
		result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));

		// Add children at level+1 with sequential order
		auto &children = ListValue::GetChildren(content_input);
		int32_t child_order = 0;
		for (auto &child : children) {
			if (!child.IsNull()) {
				auto child_children = StructValue::GetChildren(child);
				child_children[BlockTypes::LEVEL_IDX] = Value(base_level + 1);
				child_children[BlockTypes::ELEMENT_ORDER_IDX] = Value(child_order++);
				result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(child_children)));
			}
		}
	} else {
		// Unknown type - just return parent as-is
		result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));
	}

	return result;
}

// Helper to flatten a parent block with children into a list
// Returns: [parent at level n, child1 at level n+1 order 0, child2 at level n+1 order 1, ...]
// Preserves relative nesting: if children already have levels, they're adjusted relative to the new base
static vector<Value> FlattenBlockWithChildren(const Value &parent_block, const Value &children_list,
                                              int32_t base_level) {
	vector<Value> result;

	// Add parent block with the specified level.
	//
	// A TOP-LEVEL container carries a NULL level, matching pandoc_ast_to_blocks()
	// and the spec: `level` is structural nesting DEPTH, and a block that is not
	// nested has no depth to record. Stamping 1 here made the builders and the
	// converter disagree about a documented field -- the same class of divergence
	// this extension exists to prevent, sitting inside it.
	//
	// Children are unaffected: level_offset below is computed from `base_level`,
	// not from what is stored on the parent, so they still start at base_level + 1.
	auto parent_children = StructValue::GetChildren(parent_block);
	parent_children[BlockTypes::LEVEL_IDX] = Value(base_level);
	parent_children[BlockTypes::ELEMENT_ORDER_IDX] = Value(0);
	result.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(parent_children)));

	// Add flattened children with adjusted levels to preserve nesting
	if (!children_list.IsNull()) {
		auto &child_blocks = ListValue::GetChildren(children_list);
		if (!child_blocks.empty()) {
			// Find the minimum level among children to calculate the offset
			int32_t min_child_level = INT32_MAX;
			for (auto &child : child_blocks) {
				if (!child.IsNull()) {
					auto child_fields = StructValue::GetChildren(child);
					int32_t child_level = child_fields[BlockTypes::LEVEL_IDX].IsNull()
					                          ? 1
					                          : child_fields[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
					if (child_level < min_child_level) {
						min_child_level = child_level;
					}
				}
			}

			// Calculate level offset: children should start at base_level + 1
			int32_t level_offset = (base_level + 1) - min_child_level;

			int32_t child_order = 0;
			for (auto &child : child_blocks) {
				if (!child.IsNull()) {
					auto child_fields = StructValue::GetChildren(child);
					int32_t child_level = child_fields[BlockTypes::LEVEL_IDX].IsNull()
					                          ? 1
					                          : child_fields[BlockTypes::LEVEL_IDX].GetValue<int32_t>();
					// Adjust level by offset to preserve relative nesting
					result.push_back(CreateChildWithLevelAndOrder(child, child_level + level_offset, child_order++));
				}
			}
		}
	}

	return result;
}

// Helper to flatten LIST(LIST(duck_block)) to LIST(duck_block)
// Each inner list's elements are extracted and concatenated
static Value FlattenNestedList(const Value &nested_list) {
	vector<Value> result;
	if (!nested_list.IsNull()) {
		auto &outer_children = ListValue::GetChildren(nested_list);
		for (auto &inner_list : outer_children) {
			if (!inner_list.IsNull()) {
				auto &inner_children = ListValue::GetChildren(inner_list);
				for (auto &item : inner_children) {
					if (!item.IsNull()) {
						result.push_back(item);
					}
				}
			}
		}
	}
	return Value::LIST(BlockTypes::DuckBlockType(), std::move(result));
}

void BuilderFunctions::DbHeadingFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto &level_vec = args.data[1];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto heading_level = level_vec.GetValue(i);
		map<string, string> attrs;
		if (!heading_level.IsNull()) {
			attrs[BlockTypes::ATTR_HEADING_LEVEL] = std::to_string(heading_level.GetValue<int32_t>());
		}
		SetBlockFields(entries, i, BlockTypes::TYPE_HEADING, content_vec.GetValue(i), Value(),
		               BlockTypes::ENCODING_TEXT, CreateAttributesMap(attrs));
	}
}

void BuilderFunctions::DbParagraphFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetBlockFields(entries, i, BlockTypes::TYPE_PARAGRAPH, content_vec.GetValue(i), Value(),
		               BlockTypes::ENCODING_TEXT, CreateAttributesMap({}));
	}
}

void BuilderFunctions::DbCodeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto &language_vec = args.data[1];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto language = language_vec.GetValue(i);
		map<string, string> attrs;
		if (!language.IsNull()) {
			attrs["language"] = language.GetValue<string>();
		}
		SetBlockFields(entries, i, BlockTypes::TYPE_CODE, content_vec.GetValue(i), Value(), BlockTypes::ENCODING_TEXT,
		               CreateAttributesMap(attrs));
	}
}

void BuilderFunctions::DbBlockquoteFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto &level_vec = args.data[1];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto level = level_vec.GetValue(i);
		if (level.IsNull())
			level = Value(1);
		SetBlockFields(entries, i, BlockTypes::TYPE_BLOCKQUOTE, content_vec.GetValue(i), level,
		               BlockTypes::ENCODING_TEXT, CreateAttributesMap({}));
	}
}

void BuilderFunctions::DbListBlockFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &items_vec = args.data[0];
	auto &ordered_vec = args.data[1];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto items = items_vec.GetValue(i);
		auto ordered = ordered_vec.GetValue(i);

		// Convert items list to JSON array string
		string json = "[";
		if (!items.IsNull()) {
			auto &items_list = ListValue::GetChildren(items);
			bool first = true;
			for (auto &item : items_list) {
				if (!first)
					json += ",";
				first = false;
				string s = item.IsNull() ? "" : item.GetValue<string>();
				json += "\"";
				for (char c : s) {
					if (c == '"')
						json += "\\\"";
					else if (c == '\\')
						json += "\\\\";
					else if (c == '\n')
						json += "\\n";
					else
						json += c;
				}
				json += "\"";
			}
		}
		json += "]";

		map<string, string> attrs;
		attrs[BlockTypes::LIST_TYPE_ORDERED] = (!ordered.IsNull() && ordered.GetValue<bool>()) ? "true" : "false";

		SetBlockFields(entries, i, BlockTypes::TYPE_LIST, Value(json), Value(1), BlockTypes::ENCODING_JSON,
		               CreateAttributesMap(attrs));
	}
}

void BuilderFunctions::DbHrFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		SetBlockFields(entries, i, BlockTypes::TYPE_HR, Value(""), Value(), BlockTypes::ENCODING_TEXT,
		               CreateAttributesMap({}));
	}
}

void BuilderFunctions::DbMetadataFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		// LEVEL 1, not 0. This emitted 0 -- and `duck_blocks_validate()` rejects it
		// outright: "level 0 is below 1; top level is 1". The repo that owns the spec
		// shipped a public builder whose output fails the format's own validator.
		//
		// It then documented that: the spec's type table gave `metadata` a level of 0,
		// accurately describing this line. So duckdb_markdown, which emits frontmatter
		// at level 0, did not copy a documentation typo -- they conformed to real
		// shipped behaviour, correctly, and were non-conforming as a result.
		SetBlockFields(entries, i, BlockTypes::TYPE_METADATA, content_vec.GetValue(i), Value(1),
		               BlockTypes::ENCODING_YAML, CreateAttributesMap({}));
	}
}

void BuilderFunctions::DbImageFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &src_vec = args.data[0];
	auto &alt_vec = args.data[1];
	auto &title_vec = args.data[2];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto src = src_vec.GetValue(i);
		auto alt = alt_vec.GetValue(i);
		auto title = title_vec.GetValue(i);

		map<string, string> attrs;
		attrs["src"] = src.IsNull() ? "" : src.GetValue<string>();
		if (!alt.IsNull())
			attrs["alt"] = alt.GetValue<string>();
		if (!title.IsNull())
			attrs["title"] = title.GetValue<string>();

		string content = alt.IsNull() ? "" : alt.GetValue<string>();
		SetBlockFields(entries, i, BlockTypes::TYPE_IMAGE, Value(content), Value(), BlockTypes::ENCODING_TEXT,
		               CreateAttributesMap(attrs));
	}
}

void BuilderFunctions::DbRawFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto &format_vec = args.data[1];
	auto count = args.size();
	auto &entries = StructVector::GetEntries(result);

	for (idx_t i = 0; i < count; i++) {
		auto format = format_vec.GetValue(i);
		string format_str = format.IsNull() ? "html" : format.GetValue<string>();

		map<string, string> attrs;
		attrs["format"] = format_str;

		const char *encoding = BlockTypes::ENCODING_HTML;
		if (format_str == "xml")
			encoding = BlockTypes::ENCODING_XML;
		else if (format_str == "latex")
			encoding = BlockTypes::ENCODING_LATEX;

		SetBlockFields(entries, i, BlockTypes::TYPE_RAW, content_vec.GetValue(i), Value(), encoding,
		               CreateAttributesMap(attrs));
	}
}

// ============================================================================
// Flattening builder overloads - return LIST(duck_block) with parent + children
// ============================================================================

void BuilderFunctions::DbParagraphFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &children_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto children_list = children_vec.GetValue(i);

		// Create parent paragraph block with NULL content
		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_PARAGRAPH, BlockTypes::KIND_BLOCK, Value(1),
		                                         BlockTypes::ENCODING_TEXT, {});

		// Flatten with parent at level 1
		auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void BuilderFunctions::DbHeadingFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &level_vec = args.data[0];
	auto &children_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto heading_level = level_vec.GetValue(i);
		auto children_list = children_vec.GetValue(i);

		map<string, string> attrs;
		if (!heading_level.IsNull()) {
			attrs[BlockTypes::ATTR_HEADING_LEVEL] = std::to_string(heading_level.GetValue<int32_t>());
		}

		// Create parent heading block with NULL content
		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_HEADING, BlockTypes::KIND_BLOCK, Value(1),
		                                         BlockTypes::ENCODING_TEXT, attrs);

		// Flatten with parent at level 1
		auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void BuilderFunctions::DbBlockquoteFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &level_vec = args.data[0];
	auto &children_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto bq_level = level_vec.GetValue(i);
		auto children_list = children_vec.GetValue(i);

		int32_t level_val = bq_level.IsNull() ? 1 : bq_level.GetValue<int32_t>();

		// Create parent blockquote with NULL content
		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_BLOCKQUOTE, BlockTypes::KIND_BLOCK, Value(level_val),
		                                         BlockTypes::ENCODING_TEXT, {});

		// Flatten with parent at level 1
		auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

void BuilderFunctions::DbCodeFlattenFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &language_vec = args.data[0];
	auto &children_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto language = language_vec.GetValue(i);
		auto children_list = children_vec.GetValue(i);

		map<string, string> attrs;
		if (!language.IsNull()) {
			attrs["language"] = language.GetValue<string>();
		}

		// Create parent code block with NULL content
		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_CODE, BlockTypes::KIND_BLOCK, Value(1),
		                                         BlockTypes::ENCODING_TEXT, attrs);

		// Flatten with parent at level 1
		auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
	}
}

// ============================================================================
// V2 API: Block builders - all return LIST(duck_block)
// ============================================================================

void BuilderFunctions::DbHeadingV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &level_vec = args.data[0];
	auto &content_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto heading_level = level_vec.GetValue(i);
		auto content = content_vec.GetValue(i);

		map<string, string> attrs;
		if (!heading_level.IsNull()) {
			attrs[BlockTypes::ATTR_HEADING_LEVEL] = std::to_string(heading_level.GetValue<int32_t>());
		}

		// Create parent heading block with NULL content
		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_HEADING, BlockTypes::KIND_BLOCK, Value(1),
		                                         BlockTypes::ENCODING_TEXT, attrs);

		// Build result using content handling utility
		auto result_list = BuildWithContent(parent, content, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

// A block-level text run with no paragraph semantics -- Pandoc's `Plain`, HTML text
// not wrapped in a <p>. Readers emit this type, so builders must be able to produce
// it: a type one side can emit and the other cannot is the asymmetry that gave
// `list` three shapes.
void BuilderFunctions::DbPlainV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);
		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_PLAIN, BlockTypes::KIND_BLOCK, Value(1),
		                                         BlockTypes::ENCODING_TEXT, {});
		auto result_list = BuildWithContent(parent, content, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbParagraphV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		// Create parent paragraph block with NULL content
		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_PARAGRAPH, BlockTypes::KIND_BLOCK, Value(1),
		                                         BlockTypes::ENCODING_TEXT, {});

		// Build result using content handling utility
		auto result_list = BuildWithContent(parent, content, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbCodeV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &language_vec = args.data[0];
	auto &content_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto language = language_vec.GetValue(i);
		auto content = content_vec.GetValue(i);

		map<string, string> attrs;
		if (!language.IsNull()) {
			attrs["language"] = language.GetValue<string>();
		}

		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_CODE, BlockTypes::KIND_BLOCK, Value(1),
		                                         BlockTypes::ENCODING_TEXT, attrs);

		auto result_list = BuildWithContent(parent, content, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbCodeV2NoLangFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_CODE, BlockTypes::KIND_BLOCK, Value(1),
		                                         BlockTypes::ENCODING_TEXT, {});

		auto result_list = BuildWithContent(parent, content, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbBlockquoteV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &level_vec = args.data[0];
	auto &content_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto bq_level = level_vec.GetValue(i);
		auto content = content_vec.GetValue(i);

		int32_t level_val = bq_level.IsNull() ? 1 : bq_level.GetValue<int32_t>();

		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_BLOCKQUOTE, BlockTypes::KIND_BLOCK, Value(level_val),
		                                         BlockTypes::ENCODING_TEXT, {});

		// Use level_val as base_level so blockquote's level reflects quote nesting
		auto result_list = BuildWithContent(parent, content, level_val);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbBlockquoteV2NoLevelFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_BLOCKQUOTE, BlockTypes::KIND_BLOCK, Value(1),
		                                         BlockTypes::ENCODING_TEXT, {});

		auto result_list = BuildWithContent(parent, content, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

// Helper to convert VARCHAR[] items to JSON string
static string ItemsToJson(const Value &items) {
	string json = "[";
	if (!items.IsNull()) {
		auto &items_list = ListValue::GetChildren(items);
		bool first = true;
		for (auto &item : items_list) {
			if (!first)
				json += ",";
			first = false;
			string s = item.IsNull() ? "" : item.GetValue<string>();
			json += "\"";
			for (char c : s) {
				if (c == '"')
					json += "\\\"";
				else if (c == '\\')
					json += "\\\\";
				else if (c == '\n')
					json += "\\n";
				else
					json += c;
			}
			json += "\"";
		}
	}
	json += "]";
	return json;
}

void BuilderFunctions::DbListBlockV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &ordered_vec = args.data[0];
	auto &items_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto ordered = ordered_vec.GetValue(i);
		auto items = items_vec.GetValue(i);

		map<string, string> attrs;
		const bool is_ordered = !ordered.IsNull() && ordered.GetValue<bool>();
		attrs[BlockTypes::LIST_TYPE_ORDERED] = is_ordered ? "true" : "false";
		// Both attribute names. The Pandoc reader writes list_type, these builders wrote
		// only `ordered`, and a consumer reading one saw nothing from the other.
		attrs[BlockTypes::ATTR_LIST_TYPE] = is_ordered ? BlockTypes::LIST_TYPE_ORDERED : BlockTypes::LIST_TYPE_BULLET;

		// STRUCTURAL, not JSON. This was the third shape duck_block_utils emitted for
		// element_type='list' -- the others being duck_block_list_block and the Pandoc
		// reader -- and the one that made a consumer's decoder depend on which builder
		// produced the block. It also exported as an EMPTY BulletList, the same silent
		// total loss the Pandoc list had, because the exporter walks children and this
		// builder emitted none. Spec 2.0: one shape per element_type.
		vector<Value> result_list;
		result_list.push_back(CreateBlockWithNullContent(BlockTypes::TYPE_LIST, BlockTypes::KIND_BLOCK, Value(1),
		                                                 BlockTypes::ENCODING_TEXT, attrs));
		int32_t child_order = 1;
		if (!items.IsNull()) {
			map<string, string> empty_attrs;
			for (auto &item : ListValue::GetChildren(items)) {
				// Each item here is a STRING -- a single text child -- so under spec
				// v1.0's rule its text belongs on the list_item itself, not in a
				// paragraph child. The Pandoc reader's items hold a Plain BLOCK rather
				// than a text, so those correctly keep their paragraph. One rule, two
				// inputs, two representations -- not two shapes for one input.
				auto item_block = CreateBlockWithNullContent(BlockTypes::TYPE_LIST_ITEM, BlockTypes::KIND_BLOCK,
				                                             Value(2), BlockTypes::ENCODING_TEXT, empty_attrs);
				auto item_children = StructValue::GetChildren(item_block);
				item_children[BlockTypes::CONTENT_IDX] = item.IsNull() ? Value("") : item;
				item_children[BlockTypes::LEVEL_IDX] = Value(2);
				item_children[BlockTypes::ELEMENT_ORDER_IDX] = Value(child_order++);
				result_list.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(item_children)));
			}
		}
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbListBlockV2NoOrderFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &items_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto items = items_vec.GetValue(i);

		map<string, string> attrs;
		attrs[BlockTypes::LIST_TYPE_ORDERED] = "false";
		attrs[BlockTypes::ATTR_LIST_TYPE] = BlockTypes::LIST_TYPE_BULLET;

		// STRUCTURAL, not JSON. This was the third shape duck_block_utils emitted for
		// element_type='list' -- the others being duck_block_list_block and the Pandoc
		// reader -- and the one that made a consumer's decoder depend on which builder
		// produced the block. It also exported as an EMPTY BulletList, the same silent
		// total loss the Pandoc list had, because the exporter walks children and this
		// builder emitted none. Spec 2.0: one shape per element_type.
		vector<Value> result_list;
		result_list.push_back(CreateBlockWithNullContent(BlockTypes::TYPE_LIST, BlockTypes::KIND_BLOCK, Value(1),
		                                                 BlockTypes::ENCODING_TEXT, attrs));
		int32_t child_order = 1;
		if (!items.IsNull()) {
			map<string, string> empty_attrs;
			for (auto &item : ListValue::GetChildren(items)) {
				// Each item here is a STRING -- a single text child -- so under spec
				// v1.0's rule its text belongs on the list_item itself, not in a
				// paragraph child. The Pandoc reader's items hold a Plain BLOCK rather
				// than a text, so those correctly keep their paragraph. One rule, two
				// inputs, two representations -- not two shapes for one input.
				auto item_block = CreateBlockWithNullContent(BlockTypes::TYPE_LIST_ITEM, BlockTypes::KIND_BLOCK,
				                                             Value(2), BlockTypes::ENCODING_TEXT, empty_attrs);
				auto item_children = StructValue::GetChildren(item_block);
				item_children[BlockTypes::CONTENT_IDX] = item.IsNull() ? Value("") : item;
				item_children[BlockTypes::LEVEL_IDX] = Value(2);
				item_children[BlockTypes::ELEMENT_ORDER_IDX] = Value(child_order++);
				result_list.push_back(Value::STRUCT(BlockTypes::DuckBlockType(), std::move(item_children)));
			}
		}
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbListItemV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &ordered_vec = args.data[0];
	auto &content_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto ordered = ordered_vec.GetValue(i);
		auto content = content_vec.GetValue(i);

		map<string, string> attrs;
		attrs[BlockTypes::LIST_TYPE_ORDERED] = (!ordered.IsNull() && ordered.GetValue<bool>()) ? "true" : "false";

		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_LIST_ITEM, BlockTypes::KIND_BLOCK, Value(1),
		                                         BlockTypes::ENCODING_TEXT, attrs);

		auto result_list = BuildWithContent(parent, content, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbListItemV2NoOrderFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		map<string, string> attrs;
		attrs[BlockTypes::LIST_TYPE_ORDERED] = "false";

		auto parent = CreateBlockWithNullContent(BlockTypes::TYPE_LIST_ITEM, BlockTypes::KIND_BLOCK, Value(1),
		                                         BlockTypes::ENCODING_TEXT, attrs);

		auto result_list = BuildWithContent(parent, content, 1);
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbHrV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
		struct_values.push_back(make_pair("element_type", Value(BlockTypes::TYPE_HR)));
		struct_values.push_back(make_pair("content", Value("")));
		struct_values.push_back(make_pair("level", Value(1)));
		struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_TEXT)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap({})));
		struct_values.push_back(make_pair("element_order", Value(0)));

		vector<Value> result_list;
		result_list.push_back(Value::STRUCT(std::move(struct_values)));
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbMetadataV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
		struct_values.push_back(make_pair("element_type", Value(BlockTypes::TYPE_METADATA)));
		struct_values.push_back(make_pair("content", content));
		// LEVEL 1, not 0 -- this is the REGISTERED `duck_block_metadata`, and it emitted
		// a block that `duck_blocks_validate()` rejects outright: "level 0 is below 1".
		// The V1 function above had the identical bug; fixing only that one left this
		// one live, which is why the fix was verified by CALLING the function rather
		// than by reading the diff.
		struct_values.push_back(make_pair("level", Value(1)));
		struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_YAML)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap({})));
		struct_values.push_back(make_pair("element_order", Value(0)));

		vector<Value> result_list;
		result_list.push_back(Value::STRUCT(std::move(struct_values)));
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbImageV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &src_vec = args.data[0];
	auto count = args.size();
	bool has_alt = args.ColumnCount() > 1;
	bool has_title = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		auto src = src_vec.GetValue(i);

		map<string, string> attrs;
		attrs["src"] = src.IsNull() ? "" : src.GetValue<string>();

		string content = "";
		if (has_alt) {
			auto alt = args.data[1].GetValue(i);
			if (!alt.IsNull()) {
				attrs["alt"] = alt.GetValue<string>();
				content = alt.GetValue<string>();
			}
		}
		if (has_title) {
			auto title = args.data[2].GetValue(i);
			if (!title.IsNull())
				attrs["title"] = title.GetValue<string>();
		}

		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
		struct_values.push_back(make_pair("element_type", Value(BlockTypes::TYPE_IMAGE)));
		struct_values.push_back(make_pair("content", Value(content)));
		struct_values.push_back(make_pair("level", Value(1)));
		struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_TEXT)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap(attrs)));
		struct_values.push_back(make_pair("element_order", Value(0)));

		vector<Value> result_list;
		result_list.push_back(Value::STRUCT(std::move(struct_values)));
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbRawV2Fun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &format_vec = args.data[0];
	auto &content_vec = args.data[1];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto format = format_vec.GetValue(i);
		auto content = content_vec.GetValue(i);

		string format_str = format.IsNull() ? "html" : format.GetValue<string>();

		map<string, string> attrs;
		attrs["format"] = format_str;

		const char *encoding = BlockTypes::ENCODING_HTML;
		if (format_str == "xml")
			encoding = BlockTypes::ENCODING_XML;
		else if (format_str == "latex")
			encoding = BlockTypes::ENCODING_LATEX;

		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
		struct_values.push_back(make_pair("element_type", Value(BlockTypes::TYPE_RAW)));
		struct_values.push_back(make_pair("content", content));
		struct_values.push_back(make_pair("level", Value(1)));
		struct_values.push_back(make_pair("encoding", Value(encoding)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap(attrs)));
		struct_values.push_back(make_pair("element_order", Value(0)));

		vector<Value> result_list;
		result_list.push_back(Value::STRUCT(std::move(struct_values)));
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::DbRawV2NoFormatFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &content_vec = args.data[0];
	auto count = args.size();

	for (idx_t i = 0; i < count; i++) {
		auto content = content_vec.GetValue(i);

		map<string, string> attrs;
		attrs["format"] = "html";

		child_list_t<Value> struct_values;
		struct_values.push_back(make_pair("kind", Value(BlockTypes::KIND_BLOCK)));
		struct_values.push_back(make_pair("element_type", Value(BlockTypes::TYPE_RAW)));
		struct_values.push_back(make_pair("content", content));
		struct_values.push_back(make_pair("level", Value(1)));
		struct_values.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_HTML)));
		struct_values.push_back(make_pair("attributes", CreateAttributesMap(attrs)));
		struct_values.push_back(make_pair("element_order", Value(0)));

		vector<Value> result_list;
		result_list.push_back(Value::STRUCT(std::move(struct_values)));
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
	}
}

void BuilderFunctions::Register(ExtensionLoader &loader) {
	auto duck_block_type = BlockTypes::DuckBlockType();
	auto duck_block_list_type = BlockTypes::DuckBlockListType();

	// ========================================================================
	// Legacy V1 API - Only register overloads that DON'T conflict with V2
	// (V2 versions with same input signature but different return type win)
	// ========================================================================

	// duck_block_heading(content, level) - LEGACY ONLY (V2 uses level, content order)
	loader.RegisterFunction(ScalarFunction("duck_block_heading", {LogicalType::VARCHAR, LogicalType::INTEGER},
	                                       duck_block_type, DbHeadingFun));

	// duck_block_paragraph(content) - REMOVED: V2 version exists with same signature

	// duck_block_code(content, language) - REMOVED: V2 has same signature (VARCHAR, VARCHAR)
	// duck_block_code(content) - REMOVED: V2 version exists with same signature

	// duck_block_blockquote(content, level) - LEGACY ONLY (V2 uses level, content order)
	loader.RegisterFunction(ScalarFunction("duck_block_blockquote", {LogicalType::VARCHAR, LogicalType::INTEGER},
	                                       duck_block_type, DbBlockquoteFun));
	// duck_block_blockquote(content) - REMOVED: V2 version exists with same signature

	// duck_block_list_block(items, ordered) - LEGACY ONLY (V2 uses ordered, items order)
	loader.RegisterFunction(ScalarFunction("duck_block_list_block",
	                                       {LogicalType::LIST(LogicalType::VARCHAR), LogicalType::BOOLEAN},
	                                       duck_block_type, DbListBlockFun));
	// duck_block_list_block(items) - REMOVED: V2 version exists with same signature

	// duck_block_hr() - REMOVED: V2 version exists with same signature

	// duck_block_metadata(yaml_content) - REMOVED: V2 version exists with same signature

	// duck_block_image - REMOVED: V2 versions exist with same signatures

	// duck_block_raw(content, format) - REMOVED: V2 has same signature (VARCHAR, VARCHAR)
	// duck_block_raw(content) - REMOVED: V2 version exists with same signature

	// ========================================================================
	// Flattening overloads - take children list, return flattened list
	// ========================================================================

	// duck_block_paragraph(children LIST(duck_block)) -> LIST(duck_block)
	loader.RegisterFunction(
	    ScalarFunction("duck_block_paragraph", {duck_block_list_type}, duck_block_list_type, DbParagraphFlattenFun));

	// duck_block_heading(level, children LIST(duck_block)) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("duck_block_heading", {LogicalType::INTEGER, duck_block_list_type},
	                                       duck_block_list_type, DbHeadingFlattenFun));

	// duck_block_blockquote(level, children LIST(duck_block)) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("duck_block_blockquote", {LogicalType::INTEGER, duck_block_list_type},
	                                       duck_block_list_type, DbBlockquoteFlattenFun));

	// duck_block_blockquote(children LIST(duck_block)) -> LIST(duck_block) (level defaults to 1)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_blockquote", {duck_block_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &children_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto children_list = children_vec.GetValue(i);
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(
			        BlockTypes::TYPE_BLOCKQUOTE, BlockTypes::KIND_BLOCK, Value(1), BlockTypes::ENCODING_TEXT, {});
			    auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// duck_block_code(language, children LIST(duck_block)) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("duck_block_code", {LogicalType::VARCHAR, duck_block_list_type},
	                                       duck_block_list_type, DbCodeFlattenFun));

	// duck_block_code(children LIST(duck_block)) -> LIST(duck_block) (no language)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_code", {duck_block_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &children_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto children_list = children_vec.GetValue(i);
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(
			        BlockTypes::TYPE_CODE, BlockTypes::KIND_BLOCK, Value(1), BlockTypes::ENCODING_TEXT, {});
			    auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// ========================================================================
	// Nested list overloads - accept LIST(LIST(duck_block)) and flatten
	// This enables: duck_block_heading(1, [duck_block_text('A'), duck_block_bold('B')])
	// ========================================================================
	auto duck_block_nested_list_type = LogicalType::LIST(duck_block_list_type);

	// duck_block_heading(level, LIST(LIST(duck_block))) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_heading", {LogicalType::INTEGER, duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &level_vec = args.data[0];
		    auto &nested_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto level = level_vec.GetValue(i);
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    int32_t heading_level = level.IsNull() ? 1 : level.GetValue<int32_t>();
			    map<string, string> attrs;
			    attrs[BlockTypes::ATTR_HEADING_LEVEL] = to_string(heading_level);
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(
			        BlockTypes::TYPE_HEADING, BlockTypes::KIND_BLOCK, Value(1), BlockTypes::ENCODING_TEXT, attrs);
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// duck_block_paragraph(LIST(LIST(duck_block))) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_paragraph", {duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &nested_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(
			        BlockTypes::TYPE_PARAGRAPH, BlockTypes::KIND_BLOCK, Value(1), BlockTypes::ENCODING_TEXT, {});
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// duck_block_plain(LIST(LIST(duck_block))) -> LIST(duck_block)
	// Mirrors the paragraph overload above: rich inline content in a block-level run
	// that carries no paragraph semantics.
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_plain", {duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &nested_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(
			        BlockTypes::TYPE_PLAIN, BlockTypes::KIND_BLOCK, Value(1), BlockTypes::ENCODING_TEXT, {});
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// duck_block_paragraph(VARCHAR[]) -> LIST(duck_block)
	// Converts each string to a duck_block_text inline element (Issue #4)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_paragraph", {LogicalType::LIST(LogicalType::VARCHAR)}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &strings_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto strings_list = strings_vec.GetValue(i);

			    // Create paragraph parent with NULL content
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(
			        BlockTypes::TYPE_PARAGRAPH, BlockTypes::KIND_BLOCK, Value(1), BlockTypes::ENCODING_TEXT, {});

			    vector<Value> result_list;
			    result_list.push_back(parent);

			    // Convert each string to a text inline element at level 2
			    if (!strings_list.IsNull()) {
				    auto &string_children = ListValue::GetChildren(strings_list);
				    int32_t child_order = 0;
				    for (auto &str_val : string_children) {
					    if (!str_val.IsNull()) {
						    child_list_t<Value> text_struct;
						    text_struct.push_back(make_pair("kind", Value(BlockTypes::KIND_INLINE)));
						    text_struct.push_back(make_pair("element_type", Value(BlockTypes::INLINE_TEXT)));
						    text_struct.push_back(make_pair("content", str_val));
						    text_struct.push_back(make_pair("level", Value(2)));
						    text_struct.push_back(make_pair("encoding", Value(BlockTypes::ENCODING_TEXT)));
						    text_struct.push_back(make_pair("attributes", CreateAttributesMap({})));
						    text_struct.push_back(make_pair("element_order", Value(child_order++)));
						    result_list.push_back(Value::STRUCT(std::move(text_struct)));
					    }
				    }
			    }

			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
		    }
	    }));

	// duck_block_blockquote(level, LIST(LIST(duck_block))) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_blockquote", {LogicalType::INTEGER, duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &level_vec = args.data[0];
		    auto &nested_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto level = level_vec.GetValue(i);
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    int32_t quote_level = level.IsNull() ? 1 : level.GetValue<int32_t>();
			    auto parent =
			        BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_BLOCKQUOTE, BlockTypes::KIND_BLOCK,
			                                                     Value(quote_level), BlockTypes::ENCODING_TEXT, {});
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, quote_level);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// duck_block_blockquote(LIST(LIST(duck_block))) -> LIST(duck_block) (level defaults to 1)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_blockquote", {duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &nested_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(
			        BlockTypes::TYPE_BLOCKQUOTE, BlockTypes::KIND_BLOCK, Value(1), BlockTypes::ENCODING_TEXT, {});
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// duck_block_code(language, LIST(LIST(duck_block))) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_code", {LogicalType::VARCHAR, duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &lang_vec = args.data[0];
		    auto &nested_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto lang = lang_vec.GetValue(i);
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    map<string, string> attrs;
			    if (!lang.IsNull())
				    attrs["language"] = lang.GetValue<string>();
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(
			        BlockTypes::TYPE_CODE, BlockTypes::KIND_BLOCK, Value(1), BlockTypes::ENCODING_TEXT, attrs);
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// duck_block_code(LIST(LIST(duck_block))) -> LIST(duck_block) (no language)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_code", {duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &nested_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(
			        BlockTypes::TYPE_CODE, BlockTypes::KIND_BLOCK, Value(1), BlockTypes::ENCODING_TEXT, {});
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// ========================================================================
	// V2 API: All builders return LIST(duck_block)
	// Config params first, content last
	// These REPLACE legacy overloads by having different return type
	// ========================================================================

	// duck_block_heading(level INTEGER, content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("duck_block_heading", {LogicalType::INTEGER, LogicalType::VARCHAR},
	                                       duck_block_list_type, DbHeadingV2Fun));

	// duck_block_paragraph(content VARCHAR) -> LIST(duck_block)
	// V2: Returns LIST instead of single duck_block
	loader.RegisterFunction(
	    ScalarFunction("duck_block_paragraph", {LogicalType::VARCHAR}, duck_block_list_type, DbParagraphV2Fun));
	loader.RegisterFunction(
	    ScalarFunction("duck_block_plain", {LogicalType::VARCHAR}, duck_block_list_type, DbPlainV2Fun));
	loader.RegisterFunction(
	    ScalarFunction("duck_block_plain", {duck_block_list_type}, duck_block_list_type, DbPlainV2Fun));

	// duck_block_code(language VARCHAR, content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("duck_block_code", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                       duck_block_list_type, DbCodeV2Fun));

	// duck_block_code(content VARCHAR) -> LIST(duck_block) (no language)
	loader.RegisterFunction(
	    ScalarFunction("duck_block_code", {LogicalType::VARCHAR}, duck_block_list_type, DbCodeV2NoLangFun));

	// duck_block_blockquote(level INTEGER, content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("duck_block_blockquote", {LogicalType::INTEGER, LogicalType::VARCHAR},
	                                       duck_block_list_type, DbBlockquoteV2Fun));

	// duck_block_blockquote(content VARCHAR) -> LIST(duck_block) (level defaults to 1)
	loader.RegisterFunction(ScalarFunction("duck_block_blockquote", {LogicalType::VARCHAR}, duck_block_list_type,
	                                       DbBlockquoteV2NoLevelFun));

	// duck_block_hr() -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("duck_block_hr", {}, duck_block_list_type, DbHrV2Fun));

	// duck_block_metadata(yaml_content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(
	    ScalarFunction("duck_block_metadata", {LogicalType::VARCHAR}, duck_block_list_type, DbMetadataV2Fun));

	// duck_block_image(src VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(
	    ScalarFunction("duck_block_image", {LogicalType::VARCHAR}, duck_block_list_type, DbImageV2Fun));

	// duck_block_image(src VARCHAR, alt VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("duck_block_image", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                       duck_block_list_type, DbImageV2Fun));

	// duck_block_image(src VARCHAR, alt VARCHAR, title VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("duck_block_image",
	                                       {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                       duck_block_list_type, DbImageV2Fun));

	// duck_block_raw(content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(
	    ScalarFunction("duck_block_raw", {LogicalType::VARCHAR}, duck_block_list_type, DbRawV2NoFormatFun));

	// duck_block_raw(format VARCHAR, content VARCHAR) -> LIST(duck_block)
	// Note: V2 has format first

	// duck_block_list_block(ordered BOOLEAN, items VARCHAR[]) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("duck_block_list_block",
	                                       {LogicalType::BOOLEAN, LogicalType::LIST(LogicalType::VARCHAR)},
	                                       duck_block_list_type, DbListBlockV2Fun));

	// duck_block_list_block(items VARCHAR[]) -> LIST(duck_block) (ordered defaults to false)
	loader.RegisterFunction(ScalarFunction("duck_block_list_block", {LogicalType::LIST(LogicalType::VARCHAR)},
	                                       duck_block_list_type, DbListBlockV2NoOrderFun));

	// duck_block_list_block(ordered BOOLEAN, items LIST(LIST(duck_block))) -> LIST(duck_block)
	// Wraps duck_block_list_item results with a list container
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_list_block", {LogicalType::BOOLEAN, duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &ordered_vec = args.data[0];
		    auto &nested_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto ordered = ordered_vec.GetValue(i);
			    auto nested_list = nested_vec.GetValue(i);
			    bool is_ordered = !ordered.IsNull() && ordered.GetValue<bool>();

			    // Create list parent element with NULL content (children follow)
			    map<string, string> attrs;
			    attrs[BlockTypes::LIST_TYPE_ORDERED] = is_ordered ? "true" : "false";
			    auto list_parent = BuilderFunctions::CreateBlockWithNullContent(
			        BlockTypes::TYPE_LIST, BlockTypes::KIND_BLOCK, Value(1), BlockTypes::ENCODING_TEXT, attrs);

			    vector<Value> result_list;
			    result_list.push_back(list_parent);

			    // Flatten nested list and add children at level 2
			    int32_t child_order = 0;
			    if (!nested_list.IsNull()) {
				    auto &outer_children = ListValue::GetChildren(nested_list);
				    for (auto &inner_list : outer_children) {
					    if (!inner_list.IsNull()) {
						    auto &inner_children = ListValue::GetChildren(inner_list);
						    for (auto &item : inner_children) {
							    if (!item.IsNull()) {
								    auto child_fields = StructValue::GetChildren(item);
								    child_fields[BlockTypes::LEVEL_IDX] =
								        Value((child_fields[BlockTypes::LEVEL_IDX].IsNull()
								                   ? 1 // a NULL level means top level, i.e. depth 1
								                   : child_fields[BlockTypes::LEVEL_IDX].GetValue<int32_t>()) +
								              1);
								    child_fields[BlockTypes::ELEMENT_ORDER_IDX] = Value(child_order++);
								    result_list.push_back(
								        Value::STRUCT(BlockTypes::DuckBlockType(), std::move(child_fields)));
							    }
						    }
					    }
				    }
			    }
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
		    }
	    }));

	// duck_block_list_block(items LIST(LIST(duck_block))) -> LIST(duck_block) (unordered by default)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_list_block", {duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &nested_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto nested_list = nested_vec.GetValue(i);

			    // Create list parent element with NULL content (children follow)
			    map<string, string> attrs;
			    attrs[BlockTypes::LIST_TYPE_ORDERED] = "false";
			    auto list_parent = BuilderFunctions::CreateBlockWithNullContent(
			        BlockTypes::TYPE_LIST, BlockTypes::KIND_BLOCK, Value(1), BlockTypes::ENCODING_TEXT, attrs);

			    vector<Value> result_list;
			    result_list.push_back(list_parent);

			    // Flatten nested list and add children at level 2
			    int32_t child_order = 0;
			    if (!nested_list.IsNull()) {
				    auto &outer_children = ListValue::GetChildren(nested_list);
				    for (auto &inner_list : outer_children) {
					    if (!inner_list.IsNull()) {
						    auto &inner_children = ListValue::GetChildren(inner_list);
						    for (auto &item : inner_children) {
							    if (!item.IsNull()) {
								    auto child_fields = StructValue::GetChildren(item);
								    child_fields[BlockTypes::LEVEL_IDX] =
								        Value((child_fields[BlockTypes::LEVEL_IDX].IsNull()
								                   ? 1 // a NULL level means top level, i.e. depth 1
								                   : child_fields[BlockTypes::LEVEL_IDX].GetValue<int32_t>()) +
								              1);
								    child_fields[BlockTypes::ELEMENT_ORDER_IDX] = Value(child_order++);
								    result_list.push_back(
								        Value::STRUCT(BlockTypes::DuckBlockType(), std::move(child_fields)));
							    }
						    }
					    }
				    }
			    }
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
		    }
	    }));

	// duck_block_list - shorter alias for duck_block_list_block
	loader.RegisterFunction(ScalarFunction("duck_block_list",
	                                       {LogicalType::BOOLEAN, LogicalType::LIST(LogicalType::VARCHAR)},
	                                       duck_block_list_type, DbListBlockV2Fun));
	loader.RegisterFunction(ScalarFunction("duck_block_list", {LogicalType::LIST(LogicalType::VARCHAR)},
	                                       duck_block_list_type, DbListBlockV2NoOrderFun));
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_list", {LogicalType::BOOLEAN, duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &ordered_vec = args.data[0];
		    auto &nested_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto ordered = ordered_vec.GetValue(i);
			    auto nested_list = nested_vec.GetValue(i);
			    bool is_ordered = !ordered.IsNull() && ordered.GetValue<bool>();
			    map<string, string> attrs;
			    attrs[BlockTypes::LIST_TYPE_ORDERED] = is_ordered ? "true" : "false";
			    auto list_parent = BuilderFunctions::CreateBlockWithNullContent(
			        BlockTypes::TYPE_LIST, BlockTypes::KIND_BLOCK, Value(1), BlockTypes::ENCODING_TEXT, attrs);
			    vector<Value> result_list;
			    result_list.push_back(list_parent);
			    int32_t child_order = 0;
			    if (!nested_list.IsNull()) {
				    auto &outer_children = ListValue::GetChildren(nested_list);
				    for (auto &inner_list : outer_children) {
					    if (!inner_list.IsNull()) {
						    auto &inner_children = ListValue::GetChildren(inner_list);
						    for (auto &item : inner_children) {
							    if (!item.IsNull()) {
								    auto child_fields = StructValue::GetChildren(item);
								    child_fields[BlockTypes::LEVEL_IDX] =
								        Value((child_fields[BlockTypes::LEVEL_IDX].IsNull()
								                   ? 1 // a NULL level means top level, i.e. depth 1
								                   : child_fields[BlockTypes::LEVEL_IDX].GetValue<int32_t>()) +
								              1);
								    child_fields[BlockTypes::ELEMENT_ORDER_IDX] = Value(child_order++);
								    result_list.push_back(
								        Value::STRUCT(BlockTypes::DuckBlockType(), std::move(child_fields)));
							    }
						    }
					    }
				    }
			    }
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
		    }
	    }));
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_list", {duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &nested_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto nested_list = nested_vec.GetValue(i);
			    map<string, string> attrs;
			    attrs[BlockTypes::LIST_TYPE_ORDERED] = "false";
			    auto list_parent = BuilderFunctions::CreateBlockWithNullContent(
			        BlockTypes::TYPE_LIST, BlockTypes::KIND_BLOCK, Value(1), BlockTypes::ENCODING_TEXT, attrs);
			    vector<Value> result_list;
			    result_list.push_back(list_parent);
			    int32_t child_order = 0;
			    if (!nested_list.IsNull()) {
				    auto &outer_children = ListValue::GetChildren(nested_list);
				    for (auto &inner_list : outer_children) {
					    if (!inner_list.IsNull()) {
						    auto &inner_children = ListValue::GetChildren(inner_list);
						    for (auto &item : inner_children) {
							    if (!item.IsNull()) {
								    auto child_fields = StructValue::GetChildren(item);
								    child_fields[BlockTypes::LEVEL_IDX] =
								        Value((child_fields[BlockTypes::LEVEL_IDX].IsNull()
								                   ? 1 // a NULL level means top level, i.e. depth 1
								                   : child_fields[BlockTypes::LEVEL_IDX].GetValue<int32_t>()) +
								              1);
								    child_fields[BlockTypes::ELEMENT_ORDER_IDX] = Value(child_order++);
								    result_list.push_back(
								        Value::STRUCT(BlockTypes::DuckBlockType(), std::move(child_fields)));
							    }
						    }
					    }
				    }
			    }
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(result_list)));
		    }
	    }));

	// duck_block_list_item(content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(
	    ScalarFunction("duck_block_list_item", {LogicalType::VARCHAR}, duck_block_list_type, DbListItemV2NoOrderFun));

	// duck_block_list_item(ordered BOOLEAN, content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("duck_block_list_item", {LogicalType::BOOLEAN, LogicalType::VARCHAR},
	                                       duck_block_list_type, DbListItemV2Fun));

	// duck_block_list_item(content LIST(duck_block)) -> LIST(duck_block)
	loader.RegisterFunction(
	    ScalarFunction("duck_block_list_item", {duck_block_list_type}, duck_block_list_type, DbListItemV2NoOrderFun));

	// duck_block_list_item(ordered BOOLEAN, content LIST(duck_block)) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("duck_block_list_item", {LogicalType::BOOLEAN, duck_block_list_type},
	                                       duck_block_list_type, DbListItemV2Fun));

	// duck_block_list_item(content LIST(LIST(duck_block))) -> LIST(duck_block) (flatten nested lists)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_list_item", {duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &nested_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    map<string, string> attrs;
			    attrs[BlockTypes::LIST_TYPE_ORDERED] = "false";
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(
			        BlockTypes::TYPE_LIST_ITEM, BlockTypes::KIND_BLOCK, Value(1), BlockTypes::ENCODING_TEXT, attrs);
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// duck_block_list_item(ordered BOOLEAN, content LIST(LIST(duck_block))) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_list_item", {LogicalType::BOOLEAN, duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &ordered_vec = args.data[0];
		    auto &nested_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto ordered = ordered_vec.GetValue(i);
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    bool is_ordered = !ordered.IsNull() && ordered.GetValue<bool>();
			    map<string, string> attrs;
			    attrs[BlockTypes::LIST_TYPE_ORDERED] = is_ordered ? "true" : "false";
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(
			        BlockTypes::TYPE_LIST_ITEM, BlockTypes::KIND_BLOCK, Value(1), BlockTypes::ENCODING_TEXT, attrs);
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// duck_block_raw(format VARCHAR, content VARCHAR) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction("duck_block_raw", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                       duck_block_list_type, DbRawV2Fun));

	// ========================================================================
	// duck_block_div - Generic block container (Issue #6)
	// ========================================================================

	// duck_block_div(children LIST(duck_block)) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_div", {duck_block_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &children_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto children_list = children_vec.GetValue(i);
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_DIV, BlockTypes::KIND_BLOCK,
			                                                               Value(1), BlockTypes::ENCODING_TEXT, {});
			    auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// duck_block_div(id VARCHAR, children LIST(duck_block)) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_div", {LogicalType::VARCHAR, duck_block_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &id_vec = args.data[0];
		    auto &children_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto id = id_vec.GetValue(i);
			    auto children_list = children_vec.GetValue(i);
			    map<string, string> attrs;
			    if (!id.IsNull()) {
				    attrs["id"] = id.GetValue<string>();
			    }
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_DIV, BlockTypes::KIND_BLOCK,
			                                                               Value(1), BlockTypes::ENCODING_TEXT, attrs);
			    auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// duck_block_div(id VARCHAR, class VARCHAR, children LIST(duck_block)) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_div", {LogicalType::VARCHAR, LogicalType::VARCHAR, duck_block_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &id_vec = args.data[0];
		    auto &class_vec = args.data[1];
		    auto &children_vec = args.data[2];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto id = id_vec.GetValue(i);
			    auto class_val = class_vec.GetValue(i);
			    auto children_list = children_vec.GetValue(i);
			    map<string, string> attrs;
			    if (!id.IsNull()) {
				    attrs["id"] = id.GetValue<string>();
			    }
			    if (!class_val.IsNull()) {
				    attrs["class"] = class_val.GetValue<string>();
			    }
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_DIV, BlockTypes::KIND_BLOCK,
			                                                               Value(1), BlockTypes::ENCODING_TEXT, attrs);
			    auto flattened = FlattenBlockWithChildren(parent, children_list, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// duck_block_div(children LIST(LIST(duck_block))) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_div", {duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &nested_vec = args.data[0];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_DIV, BlockTypes::KIND_BLOCK,
			                                                               Value(1), BlockTypes::ENCODING_TEXT, {});
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// duck_block_div(id VARCHAR, children LIST(LIST(duck_block))) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_div", {LogicalType::VARCHAR, duck_block_nested_list_type}, duck_block_list_type,
	    [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &id_vec = args.data[0];
		    auto &nested_vec = args.data[1];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto id = id_vec.GetValue(i);
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    map<string, string> attrs;
			    if (!id.IsNull()) {
				    attrs["id"] = id.GetValue<string>();
			    }
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_DIV, BlockTypes::KIND_BLOCK,
			                                                               Value(1), BlockTypes::ENCODING_TEXT, attrs);
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));

	// duck_block_div(id VARCHAR, class VARCHAR, children LIST(LIST(duck_block))) -> LIST(duck_block)
	loader.RegisterFunction(ScalarFunction(
	    "duck_block_div", {LogicalType::VARCHAR, LogicalType::VARCHAR, duck_block_nested_list_type},
	    duck_block_list_type, [](DataChunk &args, ExpressionState &state, Vector &result) {
		    auto &id_vec = args.data[0];
		    auto &class_vec = args.data[1];
		    auto &nested_vec = args.data[2];
		    auto count = args.size();
		    for (idx_t i = 0; i < count; i++) {
			    auto id = id_vec.GetValue(i);
			    auto class_val = class_vec.GetValue(i);
			    auto nested_list = nested_vec.GetValue(i);
			    auto flat_children = FlattenNestedList(nested_list);
			    map<string, string> attrs;
			    if (!id.IsNull()) {
				    attrs["id"] = id.GetValue<string>();
			    }
			    if (!class_val.IsNull()) {
				    attrs["class"] = class_val.GetValue<string>();
			    }
			    auto parent = BuilderFunctions::CreateBlockWithNullContent(BlockTypes::TYPE_DIV, BlockTypes::KIND_BLOCK,
			                                                               Value(1), BlockTypes::ENCODING_TEXT, attrs);
			    auto flattened = FlattenBlockWithChildren(parent, flat_children, 1);
			    result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(flattened)));
		    }
	    }));
}

} // namespace duckdb
