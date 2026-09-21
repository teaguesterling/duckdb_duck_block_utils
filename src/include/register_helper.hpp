#pragma once

#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include <string>
#include <vector>

namespace duckdb {

inline void RegisterScalarWithDesc(ExtensionLoader &loader, ScalarFunction fn, vector<string> params, string desc_str,
                                   vector<string> examples) {
	CreateScalarFunctionInfo info(std::move(fn));
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	FunctionDescription desc;
	desc.parameter_names = std::move(params);
	desc.description = std::move(desc_str);
	desc.examples = std::move(examples);
	desc.categories = {"duck_block_utils"};
	info.descriptions.push_back(std::move(desc));
	loader.RegisterFunction(std::move(info));
}

inline void RegisterScalarSetWithDesc(ExtensionLoader &loader, ScalarFunctionSet fn_set, vector<string> params,
                                      string desc_str, vector<string> examples) {
	CreateScalarFunctionInfo info(std::move(fn_set));
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	FunctionDescription desc;
	desc.parameter_names = std::move(params);
	desc.description = std::move(desc_str);
	desc.examples = std::move(examples);
	desc.categories = {"duck_block_utils"};
	info.descriptions.push_back(std::move(desc));
	loader.RegisterFunction(std::move(info));
}

inline void RegisterTableWithDesc(ExtensionLoader &loader, TableFunction fn, vector<string> params, string desc_str,
                                  vector<string> examples) {
	CreateTableFunctionInfo info(std::move(fn));
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	FunctionDescription desc;
	desc.parameter_names = std::move(params);
	desc.description = std::move(desc_str);
	desc.examples = std::move(examples);
	desc.categories = {"duck_block_utils"};
	info.descriptions.push_back(std::move(desc));
	loader.RegisterFunction(std::move(info));
}

inline void RegisterTableSetWithDesc(ExtensionLoader &loader, TableFunctionSet fn_set, vector<string> params,
                                     string desc_str, vector<string> examples) {
	CreateTableFunctionInfo info(std::move(fn_set));
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	FunctionDescription desc;
	desc.parameter_names = std::move(params);
	desc.description = std::move(desc_str);
	desc.examples = std::move(examples);
	desc.categories = {"duck_block_utils"};
	info.descriptions.push_back(std::move(desc));
	loader.RegisterFunction(std::move(info));
}

} // namespace duckdb
