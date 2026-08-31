#pragma once

#include "duckdb.hpp"

namespace duckdb {

class DocMacros {
public:
	static void Register(ExtensionLoader &loader);
};

} // namespace duckdb
