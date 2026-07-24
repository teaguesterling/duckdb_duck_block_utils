#pragma once

#include "duckdb.hpp"

namespace duckdb {

class RenderMacros {
public:
	static void Register(ExtensionLoader &loader);
};

} // namespace duckdb
