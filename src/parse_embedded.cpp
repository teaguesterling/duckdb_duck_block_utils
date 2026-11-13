#include "parse_embedded.hpp"
#include "block_json.hpp"
#include "block_types.hpp"
#include "register_helper.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"

#include <algorithm>
#include <cstdio>
#include <unordered_map>

namespace duckdb {

using block_json::JsonParser;
using block_json::JVal;

namespace {

const vector<string> &DefaultFormats() {
	static const vector<string> formats {"csv", "tsv", "json", "jsonl"};
	return formats;
}

// --- output -----------------------------------------------------------------

void AppendJsonString(string &out, const string &s) {
	out += '"';
	for (unsigned char ch : s) {
		switch (ch) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		case '\b':
			out += "\\b";
			break;
		case '\f':
			out += "\\f";
			break;
		default:
			if (ch < 0x20) {
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", ch);
				out += buf;
			} else {
				out += (char)ch;
			}
		}
	}
	out += '"';
}

string TableContent(const vector<string> &headers, const vector<vector<string>> &rows) {
	string out = "{\"headers\":[";
	for (idx_t i = 0; i < headers.size(); i++) {
		if (i) {
			out += ',';
		}
		AppendJsonString(out, headers[i]);
	}
	out += "],\"rows\":[";
	for (idx_t r = 0; r < rows.size(); r++) {
		if (r) {
			out += ',';
		}
		out += '[';
		for (idx_t c = 0; c < rows[r].size(); c++) {
			if (c) {
				out += ',';
			}
			AppendJsonString(out, rows[r][c]);
		}
		out += ']';
	}
	out += "]}";
	return out;
}

// A nested value is kept as compact JSON text, so a cell never loses data.
void Serialize(const JVal &v, string &out) {
	switch (v.type) {
	case JVal::Type::STRING:
		AppendJsonString(out, v.str);
		break;
	case JVal::Type::OTHER:
		out += v.str;
		break;
	case JVal::Type::ARRAY:
		out += '[';
		for (idx_t i = 0; i < v.arr.size(); i++) {
			if (i) {
				out += ',';
			}
			Serialize(v.arr[i], out);
		}
		out += ']';
		break;
	case JVal::Type::OBJECT:
		out += '{';
		for (idx_t i = 0; i < v.obj.size(); i++) {
			if (i) {
				out += ',';
			}
			AppendJsonString(out, v.obj[i].first);
			out += ':';
			Serialize(v.obj[i].second, out);
		}
		out += '}';
		break;
	}
}

// --- csv / tsv ----------------------------------------------------------------

// RFC 4180: a field that starts with a quote runs to the next lone quote, and a
// doubled quote inside it is one quote. A delimiter or a line end inside quotes
// is text. CRLF and LF both end a record. A line that is empty or only
// whitespace, outside quotes, is not a record.
bool ParseDelimited(const string &s, char delim, const char *fmt, vector<string> &headers, vector<vector<string>> &rows,
                    string &error) {
	vector<vector<string>> records;
	vector<string> record;
	string field;
	bool in_quotes = false;
	bool quoted = false;
	idx_t record_no = 1;

	auto end_field = [&]() {
		record.push_back(field);
		field.clear();
		quoted = false;
	};
	auto end_record = [&]() {
		string trimmed = field;
		StringUtil::Trim(trimmed);
		bool blank = record.empty() && !quoted && trimmed.empty();
		if (!blank) {
			end_field();
			records.push_back(std::move(record));
		}
		record.clear();
		field.clear();
		quoted = false;
		record_no++;
	};

	for (idx_t i = 0; i < s.size(); i++) {
		char c = s[i];
		if (in_quotes) {
			if (c == '"') {
				if (i + 1 < s.size() && s[i + 1] == '"') {
					field += '"';
					i++;
				} else {
					in_quotes = false;
				}
			} else {
				field += c;
			}
		} else if (c == '"' && field.empty() && !quoted) {
			in_quotes = true;
			quoted = true;
		} else if (c == delim) {
			end_field();
		} else if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') {
			continue; // the \n ends the record
		} else if (c == '\n' || c == '\r') {
			end_record();
		} else {
			field += c;
		}
	}
	if (in_quotes) {
		error = string(fmt) + ": unterminated quote in record " + std::to_string(record_no);
		return false;
	}
	end_record();

	if (records.empty()) {
		error = string(fmt) + ": no header row";
		return false;
	}
	headers = std::move(records[0]);
	for (idx_t r = 1; r < records.size(); r++) {
		if (records[r].size() != headers.size()) {
			error = string(fmt) + ": data row " + std::to_string(r) + " has " + std::to_string(records[r].size()) +
			        " fields, the header has " + std::to_string(headers.size());
			return false;
		}
		rows.push_back(std::move(records[r]));
	}
	return true;
}

// --- json / jsonl -------------------------------------------------------------

// The shared parser is lenient by design (it renders what it can). Data
// conversion must not guess, so the brackets are checked first: balanced,
// correctly nested, with every string closed.
bool BracketsBalanced(const string &s) {
	string stack;
	bool in_str = false;
	for (idx_t i = 0; i < s.size(); i++) {
		char c = s[i];
		if (in_str) {
			if (c == '\\') {
				i++;
			} else if (c == '"') {
				in_str = false;
			}
			continue;
		}
		if (c == '"') {
			in_str = true;
		} else if (c == '[' || c == '{') {
			stack += c;
		} else if (c == ']' || c == '}') {
			if (stack.empty() || stack.back() != (c == ']' ? '[' : '{')) {
				return false;
			}
			stack.pop_back();
		}
	}
	return !in_str && stack.empty();
}

// Parse one complete JSON value: the parser must consume all of `s`.
bool ParseWhole(const string &s, JVal &out) {
	if (!BracketsBalanced(s)) {
		return false;
	}
	JsonParser parser(s);
	out = parser.Parse();
	parser.SkipWs();
	return parser.pos == s.size() && (out.type == JVal::Type::ARRAY || out.type == JVal::Type::OBJECT);
}

// Objects to a table: headers are every key in first-seen order. A missing key
// or a JSON null is an empty cell. A scalar keeps its literal text.
// The most empty cells a key union may add, unless the objects already hold
// more real cells than that. Disjoint keys otherwise make N x N cells from N
// small objects (measured: 47 KB of JSON became 48 MB of content).
constexpr idx_t MAX_PADDED_CELLS = 1000000;

bool ObjectsToTable(const vector<const JVal *> &objects, const char *fmt, vector<string> &headers,
                    vector<vector<string>> &rows, string &error) {
	// JSON keys are case-sensitive, so the index is too.
	std::unordered_map<string, idx_t> index;
	idx_t present = 0;
	for (auto *obj : objects) {
		present += obj->obj.size();
		for (auto &kv : obj->obj) {
			if (index.emplace(kv.first, headers.size()).second) {
				headers.push_back(kv.first);
			}
		}
	}
	idx_t total = objects.size() * headers.size();
	idx_t padded = total > present ? total - present : 0;
	if (padded > MaxValue<idx_t>(MAX_PADDED_CELLS, present)) {
		error = string(fmt) + ": " + std::to_string(objects.size()) + " objects x " + std::to_string(headers.size()) +
		        " keys would need " + std::to_string(padded) + " padding cells; the limit is max(" +
		        std::to_string(MAX_PADDED_CELLS) + ", present cells)";
		return false;
	}
	for (auto *obj : objects) {
		vector<string> row(headers.size());
		for (auto &kv : obj->obj) {
			auto &cell = row[index[kv.first]];
			cell.clear();
			if (kv.second.type == JVal::Type::STRING) {
				cell = kv.second.str;
			} else if (!(kv.second.type == JVal::Type::OTHER && kv.second.str == "null")) {
				Serialize(kv.second, cell);
			}
		}
		rows.push_back(std::move(row));
	}
	return true;
}

bool ParseJson(const string &s, vector<string> &headers, vector<vector<string>> &rows, string &error) {
	JVal root;
	if (!ParseWhole(s, root)) {
		error = "json: not a complete JSON value";
		return false;
	}
	if (root.type != JVal::Type::ARRAY) {
		error = "json: expected an array of objects";
		return false;
	}
	vector<const JVal *> objects;
	for (idx_t i = 0; i < root.arr.size(); i++) {
		if (root.arr[i].type != JVal::Type::OBJECT) {
			error = "json: element " + std::to_string(i + 1) + " is not an object";
			return false;
		}
		objects.push_back(&root.arr[i]);
	}
	if (objects.empty()) {
		error = "json: the array is empty";
		return false;
	}
	return ObjectsToTable(objects, "json", headers, rows, error);
}

bool ParseJsonl(const string &s, vector<string> &headers, vector<vector<string>> &rows, string &error) {
	vector<JVal> values;
	auto lines = StringUtil::Split(s, '\n');
	idx_t line_no = 0;
	for (auto &line : lines) {
		line_no++;
		string trimmed = line;
		StringUtil::Trim(trimmed);
		if (trimmed.empty()) {
			continue;
		}
		JVal v;
		if (!ParseWhole(trimmed, v) || v.type != JVal::Type::OBJECT) {
			error = "jsonl: line " + std::to_string(line_no) + " is not a JSON object";
			return false;
		}
		values.push_back(std::move(v));
	}
	if (values.empty()) {
		error = "jsonl: no objects";
		return false;
	}
	vector<const JVal *> objects;
	for (auto &v : values) {
		objects.push_back(&v);
	}
	return ObjectsToTable(objects, "jsonl", headers, rows, error);
}

// The one conversion. Returns true and sets `content`, or false and `error`.
bool Convert(const string &text, const string &format_in, string &content, string &error) {
	auto format = StringUtil::Lower(format_in);
	vector<string> headers;
	vector<vector<string>> rows;
	bool ok;
	if (format == "csv") {
		ok = ParseDelimited(text, ',', "csv", headers, rows, error);
	} else if (format == "tsv") {
		ok = ParseDelimited(text, '\t', "tsv", headers, rows, error);
	} else if (format == "json") {
		ok = ParseJson(text, headers, rows, error);
	} else if (format == "jsonl") {
		ok = ParseJsonl(text, headers, rows, error);
	} else {
		error = "unsupported format: " + format + " (supported: csv, tsv, json, jsonl)";
		return false;
	}
	if (ok) {
		content = TableContent(headers, rows);
	}
	return ok;
}

LogicalType ResultType() {
	child_list_t<LogicalType> children;
	children.push_back(make_pair("content", LogicalType::VARCHAR));
	children.push_back(make_pair("error", LogicalType::VARCHAR));
	return LogicalType::STRUCT(std::move(children));
}

void EmbeddedToTableFun(DataChunk &args, ExpressionState &, Vector &result) {
	auto count = args.size();
	for (idx_t i = 0; i < count; i++) {
		auto text = args.data[0].GetValue(i);
		auto format = args.data[1].GetValue(i);
		if (text.IsNull() || format.IsNull()) {
			result.SetValue(i, Value(ResultType()));
			continue;
		}
		string content, error;
		bool ok = Convert(text.GetValue<string>(), format.GetValue<string>(), content, error);
		child_list_t<Value> out;
		out.push_back(make_pair("content", ok ? Value(content) : Value(LogicalType::VARCHAR)));
		out.push_back(make_pair("error", ok ? Value(LogicalType::VARCHAR) : Value(error)));
		result.SetValue(i, Value::STRUCT(std::move(out)));
	}
	if (args.AllConstant()) {
		result.SetVectorType(VectorType::CONSTANT_VECTOR);
	}
}

// --- the block-list transform -------------------------------------------------

// Attributes with `key` set to `value` (replacing an existing entry), and every
// key in `drop` removed. Order of the other entries is kept.
Value WithAttrs(const Value &attrs, const vector<std::pair<string, string>> &set, const vector<string> &drop) {
	vector<Value> keys, vals;
	if (!attrs.IsNull()) {
		for (auto &entry : MapValue::GetChildren(attrs)) {
			auto &kv = StructValue::GetChildren(entry);
			auto key = kv[0].GetValue<string>();
			bool skip = std::find(drop.begin(), drop.end(), key) != drop.end();
			for (auto &s : set) {
				skip = skip || s.first == key;
			}
			if (!skip) {
				keys.push_back(kv[0]);
				vals.push_back(kv[1]);
			}
		}
	}
	for (auto &s : set) {
		keys.push_back(Value(s.first));
		vals.push_back(Value(s.second));
	}
	return Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, std::move(keys), std::move(vals));
}

string Attr(const Value &attrs, const string &key) {
	if (attrs.IsNull()) {
		return string();
	}
	for (auto &entry : MapValue::GetChildren(attrs)) {
		auto &kv = StructValue::GetChildren(entry);
		if (!kv[0].IsNull() && kv[0].GetValue<string>() == key && !kv[1].IsNull()) {
			return kv[1].GetValue<string>();
		}
	}
	return string();
}

Value Rebuild(const Value &block, const Value &type, const Value &content, const Value &encoding, const Value &attrs) {
	auto &c = StructValue::GetChildren(block);
	child_list_t<Value> out;
	out.push_back(make_pair("kind", c[BlockTypes::KIND_IDX]));
	out.push_back(make_pair("element_type", type));
	out.push_back(make_pair("content", content));
	out.push_back(make_pair("level", c[BlockTypes::LEVEL_IDX]));
	out.push_back(make_pair("encoding", encoding));
	out.push_back(make_pair("attributes", attrs));
	out.push_back(make_pair("element_order", c[BlockTypes::ELEMENT_ORDER_IDX]));
	return Value::STRUCT(std::move(out));
}

Value ParseBlock(const Value &block, const vector<string> &formats) {
	auto &c = StructValue::GetChildren(block);
	auto &type = c[BlockTypes::ELEMENT_TYPE_IDX];
	auto &kind = c[BlockTypes::KIND_IDX];
	if (type.IsNull() || type.GetValue<string>() != BlockTypes::TYPE_CODE || kind.IsNull() ||
	    kind.GetValue<string>() != BlockTypes::KIND_BLOCK) {
		return block;
	}
	auto &attrs = c[BlockTypes::ATTRIBUTES_IDX];
	auto language = StringUtil::Lower(Attr(attrs, "language"));
	if (language.empty() || std::find(formats.begin(), formats.end(), language) == formats.end()) {
		return block;
	}
	auto text = c[BlockTypes::CONTENT_IDX].IsNull() ? string() : c[BlockTypes::CONTENT_IDX].GetValue<string>();
	string content, error;
	if (Convert(text, language, content, error)) {
		return Rebuild(block, Value(BlockTypes::TYPE_TABLE), Value(content), Value(BlockTypes::ENCODING_JSON),
		               WithAttrs(attrs, {{"source_text", text}}, {"parse_error"}));
	}
	return Rebuild(block, c[BlockTypes::ELEMENT_TYPE_IDX], c[BlockTypes::CONTENT_IDX], c[BlockTypes::ENCODING_IDX],
	               WithAttrs(attrs, {{"parse_error", error}}, {}));
}

void ParseEmbeddedImpl(DataChunk &args, Vector &result, bool has_formats) {
	auto count = args.size();
	for (idx_t i = 0; i < count; i++) {
		auto blocks = args.data[0].GetValue(i);
		if (blocks.IsNull()) {
			result.SetValue(i, blocks);
			continue;
		}
		vector<string> formats = DefaultFormats();
		if (has_formats) {
			auto f = args.data[1].GetValue(i);
			if (!f.IsNull()) {
				formats.clear();
				for (auto &v : ListValue::GetChildren(f)) {
					if (!v.IsNull()) {
						formats.push_back(StringUtil::Lower(v.GetValue<string>()));
					}
				}
			}
		}
		vector<Value> out;
		for (auto &b : ListValue::GetChildren(blocks)) {
			out.push_back(b.IsNull() ? b : ParseBlock(b, formats));
		}
		result.SetValue(i, Value::LIST(BlockTypes::DuckBlockType(), std::move(out)));
	}
	if (args.AllConstant()) {
		result.SetVectorType(VectorType::CONSTANT_VECTOR);
	}
}

void ParseEmbeddedFun(DataChunk &args, ExpressionState &, Vector &result) {
	ParseEmbeddedImpl(args, result, false);
}

void ParseEmbeddedFormatsFun(DataChunk &args, ExpressionState &, Vector &result) {
	ParseEmbeddedImpl(args, result, true);
}

} // namespace

void ParseEmbeddedFunctions::Register(ExtensionLoader &loader) {
	RegisterScalarWithDesc(loader,
	                       ScalarFunction("duck_block_embedded_to_table", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                      ResultType(), EmbeddedToTableFun),
	                       {"text", "format"},
	                       "Parse csv, tsv, json (array of objects) or jsonl text into table block content. Returns "
	                       "STRUCT(content, error): the content, or the reason it failed.",
	                       {"duck_block_embedded_to_table('a,b\\n1,2', 'csv').content"});

	auto list_type = BlockTypes::DuckBlockListType();
	ScalarFunctionSet set("duck_blocks_parse_embedded");
	set.AddFunction(ScalarFunction({list_type}, list_type, ParseEmbeddedFun));
	set.AddFunction(
	    ScalarFunction({list_type, LogicalType::LIST(LogicalType::VARCHAR)}, list_type, ParseEmbeddedFormatsFun));
	RegisterScalarSetWithDesc(
	    loader, set, {"blocks", "formats"},
	    "Turn each code block whose language is in formats (default csv, tsv, json, jsonl) into a table block. "
	    "The source text is kept in attributes['source_text']. A block that fails to parse stays a code block, "
	    "with the reason in attributes['parse_error'].",
	    {"duck_blocks_parse_embedded(blocks)", "duck_blocks_parse_embedded(blocks, ['csv'])"});
}

} // namespace duckdb
