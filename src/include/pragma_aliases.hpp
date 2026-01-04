#pragma once

#include "duckdb.hpp"

namespace duckdb {

class PragmaAliases {
public:
	static void Register(ExtensionLoader &loader);
};

} // namespace duckdb
