#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include <type_traits>
#include <utility>

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

// --- StructVector children ------------------------------------------------------
// v1.5: StructVector::GetEntries(vec) -> vector<unique_ptr<Vector>> &
// v2.0: StructVector::GetEntries(vec) -> vector<Vector> &
//
// The child vectors are held BY VALUE now, so `entries[i]->SetValue(...)` stops
// compiling: entries[i] is a Vector, not a pointer to one. The container type is
// spelled via decltype rather than written out, so a helper that takes the entry
// list as a parameter has one signature that is correct on both versions.
using CompatStructEntries =
    typename std::remove_reference<decltype(StructVector::GetEntries(std::declval<Vector &>()))>::type;

// Templated so the element type is DEPENDENT and only the taken branch of the
// `if constexpr` is instantiated -- `*entries[index]` is not valid on v2.0 and
// `entries[index]` is not a Vector on v1.5.
//
// `if constexpr` here and tag dispatch in the three helpers below is a real
// inconsistency, kept deliberately: those three are copied VERBATIM from
// duckdb_markdown's header so the fleet copies stay diffable, and that header
// avoids `if constexpr` because some extensions in the ecosystem build their
// TUs at C++11. This extension does not -- both halves compile at -std=c++17
// (checked in the local build and in the canary's own compile line) -- so the
// shims written for this repo use the clearer form.
template <class ENTRIES = CompatStructEntries>
inline Vector &CompatStructChild(ENTRIES &entries, idx_t index) {
	using Elem = typename std::decay<decltype(entries[index])>::type;
	if constexpr (std::is_same<Elem, Vector>::value) {
		return entries[index];
	} else {
		return *entries[index];
	}
}

// --- bind-signature name type -------------------------------------------------
// Used wherever a bind callback receives or fills a vector of column names, and
// wherever a COPY option key crosses the boundary (option keys are identifiers
// too). String LITERALS need no helper: Identifier(const char *) is implicit by
// design, so `names.push_back("blocks")` compiles unchanged on both versions.
// Only signatures and RUNTIME strings move.
//
// DERIVED from TableFunctionBindInput rather than selected by __has_include,
// because the header and the signature do NOT move together. identifier.hpp was
// backported to the stable branch WITHOUT changing table_function_bind_t:
//
//   v1.5-variegata @ our pin   no identifier.hpp    bind: vector<string>
//   v1.5-variegata @ branch tip   HAS identifier.hpp   bind: vector<string>
//   main (v2.0)                   HAS identifier.hpp   bind: vector<Identifier>
//
// So a __has_include probe is right today only because the pin predates the
// backport, and would flip CompatName to Identifier on the very next submodule
// bump -- against a DuckDB that still wants strings.
//
// Decomposed from table_function_bind_t ITSELF, the typedef that actually
// changed, rather than from a sibling that happens to move with it. Deriving
// from TableFunctionBindInput::input_table_names also works today -- I checked
// both headers and the member and the `names` parameter do move together -- but
// "happens to move together" is an assumption that has to be re-verified on
// every bump, and not assuming is the entire point of this block.
template <class T>
struct CompatBindNamesOf;
template <class R, class A, class B, class C, class D>
struct CompatBindNamesOf<R (*)(A, B, C, D)> {
	// `typename` is REQUIRED here, D being dependent, and must NOT appear on the
	// namespace-scope alias below, where it is C++20-only and breaks MSVC.
	using type = typename std::remove_reference<D>::type::value_type;
};
using CompatName = CompatBindNamesOf<table_function_bind_t>::type;

inline string CompatNameStr(const string &name) {
	return name;
}
#ifdef DUCKDB_HAS_IDENTIFIER
// Only an overload -- __has_include still answers "does this type exist", which
// is the one question it is actually reliable for.
inline string CompatNameStr(const Identifier &id) {
	return id.GetIdentifierName();
}
#endif

inline CompatName CompatMakeName(string name) {
	return CompatName(std::move(name));
}

// Ties the derived TYPE to its OVERLOAD SET, and is the one assert here that can
// actually fail. Deriving CompatName correctly still leaves a second failure
// mode open: CompatName resolves to Identifier on a DuckDB whose identifier.hpp
// this header did NOT find, so the Identifier overload was never declared and
// CompatNameStr has nothing to match -- Identifier's operator const string & is
// explicit, so there is no silent fallback to the string overload either. This
// catches that at the header, next to the explanation, instead of at some
// distant call site. It holds on both lines, so it is safe to ship.
//
// Note what is NOT asserted: `is_same<CompatName, string>` would be true on the
// pin and FALSE on v2.0, so shipping it would hard-fail the very build this
// header exists to make work. And asserting CompatName equals the derivation it
// is defined as cannot fail -- it documents intent without checking anything.
static_assert(std::is_same<decltype(CompatNameStr(std::declval<const CompatName &>())), string>::value,
              "CompatNameStr must accept the derived CompatName on every DuckDB line");

// --- LogicalType alias ---------------------------------------------------------
// v1.5: void SetAlias(string)      -- mutates in place
// v2.0: LogicalType WithAlias(string) const -- returns a copy, never mutating a
//       type whose type-info is shared.
//
// Detected by PROBING for the member rather than by the Identifier macro above,
// because these are two independent changes and tying one to the other would
// silently pick the wrong branch if they ever land in different releases.
// The member probe itself (the decltype(void(expr)) partial specialisation) is
// valid C++11; only the dispatch below needs care -- see the note on it.
template <class T, class = void>
struct CompatHasWithAlias : std::false_type {};
template <class T>
struct CompatHasWithAlias<T, decltype(void(std::declval<const T &>().WithAlias(string())))> : std::true_type {};

// Dispatched on a tag rather than with `if constexpr`, so the header also
// compiles at C++11. Several extensions in this ecosystem build their TUs at
// C++11 deliberately (forcing C++17 on the extension but not on libduckdb makes
// static-const members in duckdb's headers acquire implicit inline linkage in
// one and not the other, which produces multiple-definition link errors), and
// `if constexpr` is C++17-only. Tag dispatch has the same property that matters
// here: only the selected overload is instantiated, so the branch referring to
// the absent member is never compiled.
template <class TYPE>
inline LogicalType CompatWithAliasImpl(TYPE type, string alias, std::true_type) {
	return type.WithAlias(std::move(alias));
}
template <class TYPE>
inline LogicalType CompatWithAliasImpl(TYPE type, string alias, std::false_type) {
	type.SetAlias(std::move(alias));
	return type;
}
// The ENTRY POINT is deliberately NOT a template. A `template <class TYPE =
// LogicalType>` form looks equivalent but is not: the default template argument
// is inert because deduction wins, so the very common call
//
//     CompatWithAlias(LogicalType::VARCHAR, "md")
//
// deduces TYPE = LogicalTypeId -- `LogicalType::VARCHAR` is a static constexpr
// LogicalTypeId (types.hpp), not a LogicalType -- and then hard-errors inside
// the shim with "request for member 'SetAlias' in 'type', which is of non-class
// type 'duckdb::LogicalTypeId'". A concrete parameter restores the implicit
// LogicalTypeId -> LogicalType conversion at the call site. Only the Impl
// overloads stay templated, which is all the tag dispatch needs.
inline LogicalType CompatWithAlias(LogicalType type, string alias) {
	return CompatWithAliasImpl(std::move(type), std::move(alias), CompatHasWithAlias<LogicalType>());
}

// --- Vector::ToUnifiedFormat ---------------------------------------------------
// v1.5: ToUnifiedFormat(count, data)  -- the only overload
// v2.0: ToUnifiedFormat(data)         -- plus the count form kept as [[deprecated]]
//
// PROBE FOR THE COUNT-FREE OVERLOAD, not the count-taking one. v2.0 did not
// remove the count form, it deprecated it, so a probe for the count form is
// true on BOTH versions and the shim would always take the deprecated path --
// silently never calling the new API it exists to reach. The count-free form is
// the one that exists only on v2.0, so it is the one that discriminates.
template <class T, class = void>
struct CompatToUnifiedWithoutCount : std::false_type {};
template <class T>
struct CompatToUnifiedWithoutCount<T, decltype(void(std::declval<T &>().ToUnifiedFormat(
                                          std::declval<UnifiedVectorFormat &>())))> : std::true_type {};

template <class VEC>
inline void CompatToUnifiedFormatImpl(VEC &vec, idx_t, UnifiedVectorFormat &data, std::true_type) {
	vec.ToUnifiedFormat(data);
}
template <class VEC>
inline void CompatToUnifiedFormatImpl(VEC &vec, idx_t count, UnifiedVectorFormat &data, std::false_type) {
	vec.ToUnifiedFormat(count, data);
}
template <class VEC = Vector>
inline void CompatToUnifiedFormat(VEC &vec, idx_t count, UnifiedVectorFormat &data) {
	CompatToUnifiedFormatImpl(vec, count, data, CompatToUnifiedWithoutCount<VEC>());
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

template <class VALUE, class FV>
inline VALUE *CompatFlatDataMutableImpl(Vector &vec, std::true_type) {
	return FV::template GetDataMutable<VALUE>(vec);
}
template <class VALUE, class FV>
inline VALUE *CompatFlatDataMutableImpl(Vector &vec, std::false_type) {
	return FV::template GetData<VALUE>(vec);
}
template <class VALUE, class FV = FlatVector>
inline VALUE *CompatFlatDataMutable(Vector &vec) {
	return CompatFlatDataMutableImpl<VALUE, FV>(vec, CompatHasFlatGetDataMutable<FV>());
}

} // namespace duckdb
