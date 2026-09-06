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

	// The one accepted widened shape: duck_block plus a trailing `filename VARCHAR`.
	// Accepted on input everywhere via a registered implicit cast; never returned.
	static LogicalType DuckBlockWithFilenameType();

	// DEPRECATED since 6.4, removed in 6.5. Never produced by any function.
	static LogicalType DuckBlockExtType();

	// Register types with the extension loader
	static void Register(ExtensionLoader &loader);

	// Every declared element_type, in one place. See the definition for why it is
	// not duplicated into the linter.
	static const vector<string> &AllTypeNames();

	// Every declared encoding, in one place -- see the definition for why.
	static const vector<string> &AllEncodingNames();
};

} // namespace duckdb
