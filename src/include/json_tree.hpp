#pragma once

#include "duckdb.hpp"
#include "yyjson.hpp"

namespace duckdb {

// Two translation units include this, so the names have external linkage. They
// are generic: a second duckdb::JVal in the same link would be a silent ODR
// clash. The named namespace prevents it.
namespace json_tree {

// ---------------------------------------------------------------------------
// A list's or table's JSON content as a small tree
// (lists: ["a", ...]; tables: {"headers": [...], "rows": [[...]]}).
//
// The parsing is DuckDB's: the yyjson library that DuckDB vendors for its json
// extension. This extension keeps no JSON parser of its own. Shared by the
// ANSI renderer and text extraction.
// ---------------------------------------------------------------------------
struct JVal {
	enum class Type { STRING, ARRAY, OBJECT, OTHER };
	Type type = Type::OTHER;
	// STRING: the decoded text. OTHER: the literal as written (a number,
	// true, false or null).
	string str;
	vector<JVal> arr;
	vector<std::pair<string, JVal>> obj;

	const JVal *Find(const string &key) const {
		for (auto &kv : obj) {
			if (kv.first == key) {
				return &kv.second;
			}
		}
		return nullptr;
	}
};

// The tree is built by recursion, once per [ or {. Block content is
// machine-generated and shallow; past this depth a value is left empty rather
// than overflow the stack.
static constexpr idx_t MAX_DEPTH = 256;

inline JVal FromYyjson(duckdb_yyjson::yyjson_val *val, idx_t depth) {
	using namespace duckdb_yyjson; // NOLINT -- the spelling the json extension uses
	JVal v;
	if (!val || depth >= MAX_DEPTH) {
		return v;
	}
	if (yyjson_is_str(val)) {
		v.type = JVal::Type::STRING;
		v.str = string(yyjson_get_str(val), yyjson_get_len(val));
	} else if (yyjson_is_arr(val)) {
		v.type = JVal::Type::ARRAY;
		size_t idx, max;
		yyjson_val *item;
		yyjson_arr_foreach(val, idx, max, item) {
			v.arr.push_back(FromYyjson(item, depth + 1));
		}
	} else if (yyjson_is_obj(val)) {
		v.type = JVal::Type::OBJECT;
		size_t idx, max;
		yyjson_val *key, *item;
		yyjson_obj_foreach(val, idx, max, key, item) {
			v.obj.emplace_back(string(yyjson_get_str(key), yyjson_get_len(key)), FromYyjson(item, depth + 1));
		}
	} else if (yyjson_is_raw(val)) {
		// Numbers are read as raw text, so a cell keeps "2.50", not 2.5.
		v.str = string(yyjson_get_raw(val), yyjson_get_len(val));
	} else if (yyjson_is_true(val)) {
		v.str = "true";
	} else if (yyjson_is_false(val)) {
		v.str = "false";
	} else if (yyjson_is_null(val)) {
		v.str = "null";
	}
	return v;
}

// The parsed tree, or false when the input is not valid JSON.
inline bool Parse(const string &input, JVal &out) {
	using namespace duckdb_yyjson; // NOLINT
	auto *doc = yyjson_read(input.data(), input.size(), YYJSON_READ_NUMBER_AS_RAW);
	if (!doc) {
		return false;
	}
	out = FromYyjson(yyjson_doc_get_root(doc), 0);
	yyjson_doc_free(doc);
	return true;
}

// The parsed tree; an empty OTHER value when the input is not valid JSON.
inline JVal Parse(const string &input) {
	JVal out;
	Parse(input, out);
	return out;
}

} // namespace json_tree

} // namespace duckdb
