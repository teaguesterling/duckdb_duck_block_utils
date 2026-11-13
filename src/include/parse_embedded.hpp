#pragma once

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

// Embedded data (#79): a code block whose language is csv, tsv, json or jsonl
// becomes a native table block ({"headers": [...], "rows": [[...]]}).
//
//   duck_block_embedded_to_table(text, format) -> STRUCT(content, error)
//   duck_blocks_parse_embedded(blocks[, formats]) -> blocks
//
// The parsers are our own, not DuckDB's readers. read_csv and read_json bind
// their input as a constant, and a block's text is a per-row value, so they
// cannot run here. Types are not decided at this step: every cell is text, and
// duck_block_table_read (#78) sniffs the types when the table is read back.
class ParseEmbeddedFunctions {
public:
	static void Register(ExtensionLoader &loader);
};

} // namespace duckdb
