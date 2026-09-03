#pragma once

#include <set>
#include <string>
#include "duckdb/common/printer.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

namespace duckdb {

// ============================================================================
// Shared utilities for the Pandoc-AST converters
// (pandoc_block_convert.cpp / pandoc_inline_convert.cpp)
// ============================================================================

// Maximum nesting depth for Pandoc-AST conversion, applied both when parsing
// Pandoc JSON into duck_blocks and when emitting Pandoc JSON from
// duck_blocks. Every recursive converter path must call CheckPandocDepth at
// entry so a deeply nested document produces a clean DuckDB error instead of
// exhausting the call stack.
static constexpr int32_t PANDOC_MAX_NESTING_DEPTH = 128;

inline void CheckPandocDepth(idx_t depth) {
	if (depth > idx_t(PANDOC_MAX_NESTING_DEPTH)) {
		throw InvalidInputException("Pandoc AST conversion exceeded the maximum nesting depth of %d",
		                            PANDOC_MAX_NESTING_DEPTH);
	}
}

// Parse a base-10 integer, returning `def` on empty, malformed, or
// out-of-range input. std::stoi throws std::invalid_argument /
// std::out_of_range on such input, which escapes as an unclean error when the
// value comes from user data (e.g. a heading_level attribute).
// DEPRECATION NOTICE, once per session per function.
//
// The Pandoc converter is moving to the panduck extension, which owns format knowledge;
// this library is utilities over the duck_block vocabulary and its spec. panduck already
// carries the converter and this repo's regression net for it, and registers its readers
// under its own names (read_pandoc_blocks, read_pandoc_blocks_string) so both can be
// loaded at once without colliding -- measured, not assumed.
//
// Warned rather than removed for one release. Deleting it here before a released panduck
// can be INSTALLED would leave a window where a consumer resolves neither copy, and
// duckeye routes thirteen formats through this converter.
//
// stderr, not the result: a warning that changes a value is a data change, and a caller
// diffing output would see the deprecation as a defect.
inline void WarnPandocDeprecated(const char *fn) {
	static std::set<std::string> warned;
	if (warned.insert(fn).second) {
		Printer::PrintF(OutputStream::STREAM_STDERR,
		                "duck_block_utils: %s is DEPRECATED and moves to the panduck extension. "
		                "It still works in this release. See "
		                "https://github.com/teaguesterling/duckdb_panduck\n",
		                fn);
	}
}

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
