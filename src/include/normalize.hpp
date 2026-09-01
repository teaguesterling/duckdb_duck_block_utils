#pragma once

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "block_types.hpp"

namespace duckdb {

// Vocabulary-level normalisation: shape fixes every PRODUCER needs, in one place,
// so four extensions do not each write their own and diverge.
//
// This deliberately does NOT live in pandoc_block_convert.cpp. That file is
// format-specific and is being handed to duckdb_panduck; these rules are the
// vocabulary's, and belong to whoever owns the spec.
class NormalizeFunctions {
public:
	static void Register(ExtensionLoader &loader);

	// SPEC 6.0's content rule, as a pass over a finished depth-first vector.
	//
	// Exposed because the rule is SIBLING-DEPENDENT: whether a text run becomes its
	// container's `content` or stays a `plain` depends on what follows it, which a
	// STREAMING reader does not know when it reaches the run. Flagged by the panduck
	// session, whose EPUB and LaTeX readers both emit as they walk -- and it is true
	// of any streaming reader of any format, so the answer should not be "each reader
	// grows its own lookahead".
	//
	// A producer can therefore emit the naive shape (always a `plain` child) and run
	// this afterwards, which is exactly what this repo's own Pandoc reader does. One
	// implementation of the rule instead of N, which is the same argument that put
	// tight-vs-loose in the vocabulary rather than in each reader.
	static void CollapseLonePlainIntoParent(vector<Value> &blocks);
};

} // namespace duckdb
