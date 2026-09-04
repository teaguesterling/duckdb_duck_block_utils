#pragma once

#include "duckdb.hpp"
#include <type_traits>

// duckdb_compat.hpp — fleet-standard cross-version shim for DuckDB extensions.
//
// This extension SHIPS against the pinned stable DuckDB (v1.5.x). The shims here
// exist so the same source also COMPILES against DuckDB main (the v2.0 line),
// which duckdb/community-extensions builds every release PR against via
// build_next.yml — a build with no per-extension opt-out.
//
// FEATURE DETECTION, NOT VERSION NUMBERS. A version macro says *when* a thing
// changed; a probe says whether it changed *here*. The probe keeps working when
// a change is backported, reverted, or lands on a different branch than
// expected.
//
// Pattern established by @bendrucker in teaguesterling/duckdb_webbed#76 (May 2026):
// detect the new API via __has_include of headers that moved in the same DuckDB
// refactor ([duckdb/duckdb#22377](https://github.com/duckdb/duckdb/pull/22377) —
// "mandatory per-vector size tracking" landed alongside the vector-buffer header
// reshuffle), then dispatch via a single #ifdef block.
//
// Cross-version coverage:
//   - duckdb v1.4.x / v1.5.x: old API everywhere
//   - duckdb main / v2.0.x:   new API everywhere
//
// Each change is probed SEPARATELY. Tying several to one macro silently picks
// the wrong branch if they ever land in different releases.
//
// See teaguesterling/duckdb_markdown's docs/duckdb_v2_migration.md for the
// long-form rationale + upgrade checklist for other extensions.

#if __has_include("duckdb/common/vector/list_vector.hpp")
#define DUCKDB_HAS_NEW_VECTOR_HEADERS 1
// The per-vector-accessor classes moved out of duckdb/common/types/vector.hpp
// into one header each. duckdb.hpp no longer pulls them in transitively, so a
// translation unit that says StructVector::GetEntries now gets
// "'StructVector' has not been declared" -- which reads like a missing symbol
// rather than a moved header. flat_vector.hpp is needed by THIS header too:
// CompatFlatDataMutable below names FlatVector at namespace scope.
#include "duckdb/common/vector/flat_vector.hpp"
#include "duckdb/common/vector/list_vector.hpp"
#include "duckdb/common/vector/struct_vector.hpp"
#endif

// count_t / capacity_t are strong types introduced by the same per-vector size
// tracking work. Probed separately from the vector headers above: they arrived
// together, but "arrived together once" is not a reason to make one imply the
// other. count_t's constructor from idx_t is EXPLICIT, so a call site cannot
// simply pass an idx_t through -- and the type does not exist at all on v1.5,
// so the dispatch has to be a preprocessor #ifdef rather than `if constexpr`:
// the discarded branch of an `if constexpr` is still parsed, and a non-dependent
// name that does not exist is a hard error even in the branch never taken.
#if __has_include("duckdb/common/types/size.hpp")
#define DUCKDB_HAS_COUNT_T 1
#include "duckdb/common/types/size.hpp"
#endif

// duckdb::Identifier replaced std::string as the name type in table-function and
// COPY bind signatures. Identifier compares case-insensitively, and construction
// from a RUNTIME string is explicit by design -- promoting a string to an
// identifier is meant to be a deliberate act at the call site -- so a boundary
// helper is needed rather than an implicit conversion.
#if __has_include("duckdb/common/identifier.hpp")
#define DUCKDB_HAS_IDENTIFIER 1
#include "duckdb/common/identifier.hpp"
#endif

namespace duckdb {

// --- Output chunk finalization -------------------------------------------------

#ifdef DUCKDB_HAS_NEW_VECTOR_HEADERS

// DuckDB main mandates per-vector Size() tracking; DataChunk::SetCardinality only
// updates chunk.count. SetChildCardinality additionally calls FlatVector::SetSize
// on every column so query operators reading vec.Size() see the right value.
// Without this, VariadicExecutor (and similar) reports:
//   "Mismatch in input vector sizes ... expected 0 rows but got N"
inline void CompatSetOutputCardinality(DataChunk &chunk, idx_t count) {
	chunk.SetChildCardinality(count);
}

#else // Old API (v1.4.x / v1.5.x)

inline void CompatSetOutputCardinality(DataChunk &chunk, idx_t count) {
	chunk.SetCardinality(count);
}

#endif

// --- Vector::Reference(Value) --------------------------------------------------
// v1.5: void Reference(const Value &value)                  -- count implicit
// v2.0: void Reference(const Value &value, count_t count)   -- count mandatory
//
// The 1-argument form is GONE on v2.0, not deprecated; a vector now has to know
// how many rows it spans. Both forms build a constant vector referencing the one
// value, so the count to pass is the number of rows the result covers -- in a
// scalar function, args.size().
#ifdef DUCKDB_HAS_COUNT_T
inline void CompatReferenceValue(Vector &vec, const Value &value, idx_t count) {
	vec.Reference(value, count_t(count));
}
#else
inline void CompatReferenceValue(Vector &vec, const Value &value, idx_t count) {
	(void)count;
	vec.Reference(value);
}
#endif

// --- bind-signature name type -------------------------------------------------
// Used wherever a bind callback receives or fills a vector of column names, and
// wherever a COPY option key crosses the boundary (option keys are identifiers
// too). String LITERALS need no helper: Identifier(const char *) is implicit by
// design, so `names.push_back("blocks")` compiles unchanged on both versions.
// Only signatures and RUNTIME strings move.
#ifdef DUCKDB_HAS_IDENTIFIER
using CompatName = Identifier;
inline string CompatNameStr(const Identifier &id) {
	return id.GetIdentifierName();
}
inline Identifier CompatMakeName(string name) {
	return Identifier(std::move(name));
}
#else
using CompatName = string;
inline string CompatNameStr(const string &name) {
	return name;
}
inline string CompatMakeName(string name) {
	return name;
}
#endif

// --- LogicalType alias ---------------------------------------------------------
// v1.5: void SetAlias(string)      -- mutates in place
// v2.0: LogicalType WithAlias(string) const -- returns a copy, never mutating a
//       type whose type-info is shared. SetAlias is REMOVED, not deprecated.
//
// Detected by PROBING for the member rather than by the Identifier macro above,
// because these are two independent changes and tying one to the other would
// silently pick the wrong branch if they ever land in different releases.
// `if constexpr` discards the untaken branch only inside a template, hence the
// template parameter.
template <class T, class = void>
struct CompatHasWithAlias : std::false_type {};
template <class T>
struct CompatHasWithAlias<T, decltype(void(std::declval<const T &>().WithAlias(string())))> : std::true_type {};

template <class TYPE = LogicalType>
inline LogicalType CompatWithAlias(TYPE type, string alias) {
	if constexpr (CompatHasWithAlias<TYPE>::value) {
		return type.WithAlias(std::move(alias));
	} else {
		type.SetAlias(std::move(alias));
		return type;
	}
}

// --- Vector::ToUnifiedFormat ---------------------------------------------------
// v2.0 dropped the count parameter. Probed the same way.
template <class T, class = void>
struct CompatToUnifiedTakesCount : std::false_type {};
template <class T>
struct CompatToUnifiedTakesCount<T, decltype(void(std::declval<T &>().ToUnifiedFormat(
                                        idx_t(0), std::declval<UnifiedVectorFormat &>())))> : std::true_type {};

template <class VEC = Vector>
inline void CompatToUnifiedFormat(VEC &vec, idx_t count, UnifiedVectorFormat &data) {
	if constexpr (CompatToUnifiedTakesCount<VEC>::value) {
		vec.ToUnifiedFormat(count, data);
	} else {
		vec.ToUnifiedFormat(data);
	}
}

// --- FlatVector mutable data ---------------------------------------------------
// v1.5: FlatVector::GetData<T>(vec)         returns T*
// v2.0: FlatVector::GetData<T>(vec)         returns const T*
//       FlatVector::GetDataMutable<T>(vec)  returns T*
// Writing through the v2.0 GetData is a compile error, which is the point of the
// split -- so the WRITE path must ask for mutability explicitly.
template <class T, class = void>
struct CompatHasFlatGetDataMutable : std::false_type {};
template <class T>
struct CompatHasFlatGetDataMutable<T, decltype(void(T::template GetDataMutable<bool>(std::declval<Vector &>())))>
    : std::true_type {};

template <class VALUE, class FV = FlatVector>
inline VALUE *CompatFlatDataMutable(Vector &vec) {
	if constexpr (CompatHasFlatGetDataMutable<FV>::value) {
		return FV::template GetDataMutable<VALUE>(vec);
	} else {
		return FV::template GetData<VALUE>(vec);
	}
}

} // namespace duckdb
