#pragma once

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

// duck_block_table_read(content VARCHAR): the inverse of duck_block_json_to_table.
// It reads a table block's native content ({"headers": [...], "rows": [[...]]})
// back into a relation with typed columns. The bind step sniffs each column,
// so the argument must be a constant: a literal, getvariable(), or a prepared
// parameter. A table function cannot take a subquery.
class TableReadFunctions {
public:
	static void Register(ExtensionLoader &loader);
};

} // namespace duckdb
