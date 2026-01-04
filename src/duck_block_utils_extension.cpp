#define DUCKDB_EXTENSION_MAIN

#include "duck_block_utils_extension.hpp"
#include "block_types.hpp"
#include "type_functions.hpp"
#include "manipulation.hpp"
#include "builders.hpp"
#include "inline_builders.hpp"
#include "pandoc_inline_convert.hpp"
#include "assembly.hpp"
#include "extraction.hpp"
#include "validation.hpp"
#include "pandoc_block_convert.hpp"
#include "pragma_aliases.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
	// Phase 1: Foundation
	BlockTypes::Register(loader);
	TypeFunctions::Register(loader);
	ManipulationFunctions::Register(loader);

	// Phase 2: Builder Functions
	BuilderFunctions::Register(loader);

	// Phase 2b: Inline Builder Functions
	InlineBuilderFunctions::Register(loader);

	// Phase 2c: Pandoc Inline Conversion Functions
	PandocInlineConvert::Register(loader);

	// Phase 3: Assembly Functions
	AssemblyFunctions::Register(loader);

	// Phase 4: Extraction Functions
	ExtractionFunctions::Register(loader);

	// Phase 5: Validation Functions
	ValidationFunctions::Register(loader);

	// Phase 6: Pandoc AST Block Functions
	PandocBlockConvert::Register(loader);

	// Phase 7: Pragma for short aliases
	PragmaAliases::Register(loader);
}

void DuckBlockUtilsExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string DuckBlockUtilsExtension::Name() {
	return "duck_block_utils";
}

std::string DuckBlockUtilsExtension::Version() const {
#ifdef EXT_VERSION_DUCK_BLOCK_UTILS
	return EXT_VERSION_DUCK_BLOCK_UTILS;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(duck_block_utils, loader) {
	duckdb::LoadInternal(loader);
}
}
