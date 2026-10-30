#pragma once

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

namespace duckdb {

// ============================================================================
// Safe integer parsing, shared by assembly, extraction and validation.
//
// This header held the Pandoc-AST converters' shared utilities (the depth cap and
// the deprecation notice) until the converter moved to panduck. Only the parser
// below had other callers, so only it stays.
// ============================================================================

// Parse a base-10 integer, returning `def` on empty, malformed, or
// out-of-range input. std::stoi throws std::invalid_argument /
// std::out_of_range on such input, which escapes as an unclean error when the
// value comes from user data (e.g. a heading_level attribute).
inline int32_t ParseInt32OrDefault(const std::string &s, int32_t def) {
	if (s.empty()) {
		return def;
	}
	errno = 0;
	char *end = nullptr;
	const long value = std::strtol(s.c_str(), &end, 10);
	if (end == s.c_str() || errno == ERANGE || value > long(std::numeric_limits<int32_t>::max()) ||
	    value < long(std::numeric_limits<int32_t>::min())) {
		return def;
	}
	return int32_t(value);
}

} // namespace duckdb
