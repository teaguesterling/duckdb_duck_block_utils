#pragma once

#include "duckdb.hpp"
#include "duckdb/common/types.hpp"

// The vocabulary itself is a separate, link-free header so other extensions can
// consume it via submodule without pulling in anything that needs linking.
#include "duck_block_vocabulary.hpp"

namespace duckdb {

// Inherits every vocabulary constant, so BlockTypes::TYPE_HEADING and friends
// keep resolving unchanged throughout this extension.
class BlockTypes : public DuckBlockVocabulary {
public:
	// ========================================================================
	// Unified duck_block type
	// ========================================================================

	// Create the duck_block type
	static LogicalType DuckBlockType();

	// Create a LIST(duck_block) type
	static LogicalType DuckBlockListType();

	// Create the extended duck_block type with provenance fields
	static LogicalType DuckBlockExtType();

	// Register types with the extension loader
	static void Register(ExtensionLoader &loader);
};

} // namespace duckdb
