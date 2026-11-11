#pragma once

#include "duckdb.hpp"
#include "utf8proc_wrapper.hpp"

namespace duckdb {

// Two translation units include this, so it cannot be an anonymous namespace,
// and the names have external linkage. They are generic: a second duckdb::JVal
// in the same link would be a silent ODR clash. The named namespace prevents it.
namespace block_json {

// ---------------------------------------------------------------------------
// Minimal JSON parser for machine-generated block content
// (lists: ["a", ...]; tables: {"headers": [...], "rows": [[...]]}).
// Decodes string escapes, \uXXXX and surrogate pairs included. Shared by the
// ANSI renderer and text extraction.
// ---------------------------------------------------------------------------
struct JVal {
	enum class Type { STRING, ARRAY, OBJECT, OTHER };
	Type type = Type::OTHER;
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

struct JsonParser {
	const string &s;
	size_t pos = 0;
	// Parse recurses once per [ or {. Block content is machine-generated and
	// shallow; past this depth the rest is dropped rather than overflow the stack.
	static constexpr idx_t MAX_DEPTH = 256;
	idx_t depth = 0;

	explicit JsonParser(const string &input) : s(input) {
	}

	void SkipWs() {
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) {
			pos++;
		}
	}

	static void AppendCodepoint(string &out, uint32_t cp) {
		char buf[4];
		int sz = 0;
		if (Utf8Proc::CodepointToUtf8((int)cp, sz, buf)) {
			out.append(buf, sz);
		}
	}

	// Four hex digits at s[at..at+3] into cp; false (cp untouched) otherwise.
	bool Hex4(size_t at, uint32_t &cp) const {
		if (at + 4 > s.size()) {
			return false;
		}
		uint32_t v = 0;
		for (size_t k = at; k < at + 4; k++) {
			char h = s[k];
			v <<= 4;
			if (h >= '0' && h <= '9') {
				v |= (uint32_t)(h - '0');
			} else if (h >= 'a' && h <= 'f') {
				v |= (uint32_t)(h - 'a' + 10);
			} else if (h >= 'A' && h <= 'F') {
				v |= (uint32_t)(h - 'A' + 10);
			} else {
				return false;
			}
		}
		cp = v;
		return true;
	}

	string ParseString() {
		string out;
		D_ASSERT(s[pos] == '"');
		pos++;
		while (pos < s.size() && s[pos] != '"') {
			if (s[pos] == '\\' && pos + 1 < s.size()) {
				pos++;
				switch (s[pos]) {
				case 'n':
					out += '\n';
					break;
				case 't':
					out += '\t';
					break;
				case 'r':
					out += '\r';
					break;
				case 'b':
					out += '\b';
					break;
				case 'f':
					out += '\f';
					break;
				case 'u': {
					// Exactly four hex digits, or it is not a \u escape. strtoul took
					// fewer (and signs), so "\u12" swallowed the closing quote.
					uint32_t cp;
					if (!Hex4(pos + 1, cp)) {
						out += 'u';
						break;
					}
					pos += 4;
					// surrogate pair
					uint32_t lo;
					if (cp >= 0xD800 && cp <= 0xDBFF && pos + 2 < s.size() && s[pos + 1] == '\\' && s[pos + 2] == 'u' &&
					    Hex4(pos + 3, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						pos += 6;
					}
					AppendCodepoint(out, cp);
					break;
				}
				default:
					out += s[pos];
					break;
				}
				pos++;
			} else {
				out += s[pos++];
			}
		}
		if (pos < s.size()) {
			pos++; // closing quote
		}
		return out;
	}

	JVal Parse() {
		JVal v;
		SkipWs();
		if (pos >= s.size() || depth >= MAX_DEPTH) {
			return v;
		}
		char c = s[pos];
		if (c == '"') {
			v.type = JVal::Type::STRING;
			v.str = ParseString();
		} else if (c == '[') {
			v.type = JVal::Type::ARRAY;
			pos++;
			SkipWs();
			if (pos < s.size() && s[pos] == ']') {
				pos++;
				return v;
			}
			while (pos < s.size()) {
				depth++;
				v.arr.push_back(Parse());
				depth--;
				SkipWs();
				if (pos < s.size() && s[pos] == ',') {
					pos++;
					continue;
				}
				break;
			}
			if (pos < s.size() && s[pos] == ']') {
				pos++;
			}
		} else if (c == '{') {
			v.type = JVal::Type::OBJECT;
			pos++;
			SkipWs();
			if (pos < s.size() && s[pos] == '}') {
				pos++;
				return v;
			}
			while (pos < s.size()) {
				SkipWs();
				if (pos >= s.size() || s[pos] != '"') {
					break;
				}
				string key = ParseString();
				SkipWs();
				if (pos < s.size() && s[pos] == ':') {
					pos++;
				}
				depth++;
				v.obj.emplace_back(key, Parse());
				depth--;
				SkipWs();
				if (pos < s.size() && s[pos] == ',') {
					pos++;
					continue;
				}
				break;
			}
			if (pos < s.size() && s[pos] == '}') {
				pos++;
			}
		} else {
			// number / true / false / null — capture raw token as string
			v.type = JVal::Type::OTHER;
			size_t start = pos;
			while (pos < s.size() && s[pos] != ',' && s[pos] != ']' && s[pos] != '}' && s[pos] != ' ' &&
			       s[pos] != '\n' && s[pos] != '\t' && s[pos] != '\r') {
				pos++;
			}
			v.str = s.substr(start, pos - start);
		}
		return v;
	}
};

} // namespace block_json

} // namespace duckdb
