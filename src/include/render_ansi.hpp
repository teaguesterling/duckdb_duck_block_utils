#pragma once

#include "duckdb.hpp"

namespace duckdb {

class RenderAnsiFunctions {
public:
	static void Register(ExtensionLoader &loader);
};

} // namespace duckdb
