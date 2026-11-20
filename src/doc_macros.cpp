#include "doc_macros.hpp"
#include "duck_block_compact.hpp"
#include "duckdb_compat.hpp"
#include "register_helper.hpp"
#include "duckdb/catalog/default/default_functions.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"
#include "duckdb/function/pragma_function.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/extension_helper.hpp"

#include <functional>

namespace duckdb {

// C++ Scalar function: duck_block_ensure_extension(VARCHAR) -> BOOLEAN
static void DbEnsureExtensionFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &context = CompatClientContext(state);
	UnaryExecutor::Execute<string_t, bool>(args.data[0], result, args.size(), [&](string_t ext_name) {
		string name = ext_name.GetString();
		return ExtensionHelper::TryAutoLoadExtension(context, name);
	});
}

static string DocMacrosQuery(ClientContext &context, const FunctionParameters &parameters) {
	// Attempt initial companion extension auto-loads in background
	// json only: it is a CORE DuckDB extension, and to_json() backs the `blocks`
	// and `pandoc` output formats. markdown and webbed are deliberately NOT loaded
	// -- nothing here calls their writers, and auto-loading a sibling
	// document-format extension is the dependency this extension does not take.
	ExtensionHelper::TryAutoLoadExtension(context, "json");

	string sql = R"DBSQL(
-- ===========================================================================
-- UNIFIED DOCUMENT & AST QUERY PIPELINE MACROS (duck_block_utils)
-- ===========================================================================

-- Quote a string for embedding in a dynamic query. Retained solely for
-- profile_file(), which builds a SUMMARIZE statement; the dispatch macros that
-- were its main consumer now live in panduck.
CREATE OR REPLACE MACRO duck_block_quote(s) AS ('''' || replace(coalesce(s, ''), '''', '''''') || '''');

-- Document queries over a block list. These take LIST(duck_block), never a path:
-- path -> blocks is panduck's job (read_panduck_doc / panduck_read_blocks), and
-- this extension depends on nothing.
--
-- Named db_* like everything else here. The `doc_*` prefix now belongs to panduck.
-- There is no shared render helper: the output CASE below is four direct calls,
-- and wrapping it in an indirection bought a name without buying anything else.

-- 7. Tabular Dataset Profiling
CREATE OR REPLACE MACRO profile_table(target_query) AS TABLE
    SELECT * FROM query('SUMMARIZE ' || target_query);

CREATE OR REPLACE MACRO profile_file(path) AS TABLE
    SELECT * FROM query('SUMMARIZE SELECT * FROM ' || duck_block_quote(path));

)DBSQL";
	return sql;
}

// ---------------------------------------------------------------------------
// The document-query macros are registered at LOAD, not behind a pragma.
//
// A macro created by a pragma is doubly unreachable from a sibling extension:
// it is invisible to the statement that triggered the load, and a macro cannot
// invoke a pragma to bring it into scope. panduck could build on the C++ scalar
// duck_blocks_toc but not on these, so its path-taking doc_section and
// doc_sections_like could not exist at all.
//
// It also removes a cost every consumer was paying: "LOAD duck_block_utils AND
// run a pragma at every call site" is the same shape as the accepted cost we
// removed from the reader-dispatch plan.
//
// The pragma survives for duck_block_aliases -- opt-in short names are exactly
// what a pragma is for. It is the core query surface that should not need one.
// ---------------------------------------------------------------------------

// DuckDB's DefaultMacro changed SHAPE between the version this extension ships
// against and DuckDB main, and it is a shape change rather than a rename:
//
//   v1.5: { schema; name; parameters[8]; named_parameters[8]; macro; }
//   v2.0: { schema; name; macro_definition; }
//
// On v2.0 the signature moved INSIDE the definition string -- the generator now
// builds "CREATE MACRO __dummy__(a, b, c := 'x') AS (body)" and lets the parser
// work out parameters and defaults, instead of assembling ColumnRefExpressions
// itself. DefaultTableMacro did NOT change, so DOC_TABLE_MACROS below is
// untouched; only the scalar side needs this.
//
// The macros stay in the v1.5 shape and the v2.0 signature is SYNTHESISED from
// it, so the SQL body is written once. Writing two arrays under an #ifdef would
// put the same forty lines of SQL in the source twice, and the copy that the
// local build never compiles is the one that would drift.
struct DocScalarMacro {
	const char *schema;
	const char *name;
	const char *parameters[8];
	DefaultNamedParameter named_parameters[8];
	const char *macro;
};

template <class T, class = void>
struct HasMacroDefinition : std::false_type {};
template <class T>
struct HasMacroDefinition<T, decltype(void(std::declval<T &>().macro_definition))> : std::true_type {};

// Templated on the DefaultMacro type so the member accesses are DEPENDENT: the
// discarded branch of an `if constexpr` is still parsed, and naming a member
// that does not exist on this version is a hard error unless lookup is deferred.
template <class DM = DefaultMacro>
static unique_ptr<CreateMacroInfo> CreateScalarMacroInfo(const DocScalarMacro &m) {
	DM dm {};
	if constexpr (HasMacroDefinition<DM>::value) {
		string signature = "(";
		bool first = true;
		for (idx_t i = 0; i < 8 && m.parameters[i] != nullptr; i++) {
			signature += first ? "" : ", ";
			signature += m.parameters[i];
			first = false;
		}
		for (idx_t i = 0; i < 8 && m.named_parameters[i].name != nullptr; i++) {
			signature += first ? "" : ", ";
			signature += m.named_parameters[i].name;
			signature += " := ";
			signature += m.named_parameters[i].default_value;
			first = false;
		}
		signature += ") AS ";
		signature += m.macro;

		dm.schema = m.schema;
		dm.name = m.name;
		// `signature` outlives the call: CreateInternalMacroInfo parses the text
		// immediately and keeps no pointer into it.
		dm.macro_definition = signature.c_str();
		return DefaultFunctionGenerator::CreateInternalMacroInfo(dm);
	} else {
		dm.schema = m.schema;
		dm.name = m.name;
		for (idx_t i = 0; i < 8; i++) {
			dm.parameters[i] = m.parameters[i];
			dm.named_parameters[i] = m.named_parameters[i];
		}
		dm.macro = m.macro;
		return DefaultFunctionGenerator::CreateInternalMacroInfo(dm);
	}
}

// THE COMPACT LINE FORMAT'S SQL. Each piece below is used by more than one macro, or by
// another piece, so it is written ONCE and joined by C string-literal concatenation.
// A piece written "{{NAME}}" comes from the code tables in vendor/duck_block_compact.hpp:
// CompactSql() fills it in at registration, so these macros and anything that vendors
// that header use one set of codes, and the header's TABLE_VERSION is the "@" they write.
#define DBU_COMPACT_VERSION "{{VERSION}}"

#define DBU_COMPACT_TYPE_ENCODE "{{TYPE_ENCODE}}"

#define DBU_COMPACT_ATTR_KEYS "{{ATTR_KEYS}}"

#define DBU_COMPACT_ATTR_ENCODE "{{ATTR_ENCODE}}"

#define DBU_COMPACT_LINE                                                                                               \
	"'{' || concat_ws(',',\n"                                                                                          \
	"            '\"t\":' || to_json(coalesce(" DBU_COMPACT_TYPE_ENCODE                                                \
	"[x.b.kind || ':' || x.b.element_type], x.b.kind || ':' || x.b.element_type)),\n"                                  \
	"            CASE WHEN x.b.content IS NOT NULL THEN '\"c\":' || to_json(x.b.content) END,\n"                       \
	"            '\"l\":' || CASE WHEN x.b.level IS NULL\n"                                                            \
	"                           THEN error('duck_blocks_to_compact: NULL level at element_order ' || "                 \
	"x.b.element_order::VARCHAR)\n"                                                                                    \
	"                           ELSE x.b.level::VARCHAR END,\n"                                                        \
	"            CASE WHEN x.b.encoding IS NULL\n"                                                                     \
	"                     THEN error('duck_blocks_to_compact: NULL encoding at element_order ' || "                    \
	"x.b.element_order::VARCHAR)\n"                                                                                    \
	"                 WHEN x.b.encoding = 'text' THEN NULL\n"                                                          \
	"                 WHEN x.b.encoding = 'json' THEN '\"e\":\"j\"'\n"                                                 \
	"                 WHEN x.b.encoding IN ('t', 'j')\n"                                                               \
	"                     THEN error('duck_blocks_to_compact: encoding ''' || x.b.encoding || ''' is a reserved "      \
	"code')\n"                                                                                                         \
	"                 ELSE '\"e\":' || to_json(x.b.encoding) END,\n"                                                   \
	"            CASE WHEN x.b.attributes IS NULL\n"                                                                   \
	"                     THEN error('duck_blocks_to_compact: NULL attributes at element_order ' || "                  \
	"x.b.element_order::VARCHAR)\n"                                                                                    \
	"                 WHEN list_bool_or(list_transform(map_entries(x.b.attributes), lambda e: e.value IS NULL))\n"     \
	"                     THEN error('duck_blocks_to_compact: NULL attribute value at element_order ' || "             \
	"x.b.element_order::VARCHAR)\n"                                                                                    \
	"                 END,\n"                                                                                          \
	"            nullif('\"a\":{' || array_to_string(list_transform(\n"                                                \
	"                list_filter(map_entries(x.b.attributes), lambda e: e.key IN (" DBU_COMPACT_ATTR_KEYS ")),\n"      \
	"                lambda e: to_json(" DBU_COMPACT_ATTR_ENCODE                                                       \
	"[e.key]) || ':' || to_json(e.value)), ',') || '}', '\"a\":{}'),\n"                                                \
	"            nullif('\"m\":{' || array_to_string(list_transform(\n"                                                \
	"                list_filter(map_entries(x.b.attributes), lambda e: e.key NOT IN (" DBU_COMPACT_ATTR_KEYS ")),\n"  \
	"                lambda e: to_json(e.key) || ':' || to_json(e.value)), ',') || '}', '\"m\":{}')\n"                 \
	"        ) || '}' || chr(10)"

#define DBU_COMPACT_LINE_STRUCT                                                                                        \
	"{\"@\": \"VARCHAR\", \"t\": \"VARCHAR\", \"c\": \"VARCHAR\", \"l\": \"INTEGER\", \"e\": \"VARCHAR\", \"a\": "     \
	"\"JSON\", \"m\": \"MAP(VARCHAR, VARCHAR)\"}"

#define DBU_COMPACT_IS_HEADER "x.j.t IS NULL AND x.j.\"@\" IS NOT NULL"

#define DBU_COMPACT_TYPE_DECODE "{{TYPE_DECODE}}"

#define DBU_COMPACT_KT                                                                                                 \
	"coalesce(" DBU_COMPACT_TYPE_DECODE "[x.j.t],\n"                                                                   \
	"                 CASE WHEN contains(x.j.t, ':') THEN x.j.t\n"                                                     \
	"                      ELSE error('duck_blocks_from_compact: unknown type code ' || coalesce(to_json(x.j.t), "     \
	"'NULL')\n"                                                                                                        \
	"                                 || ' at element_order ' || x.ord::VARCHAR) END)"

#define DBU_COMPACT_ATTR_CODES "{{ATTR_CODES}}"

#define DBU_COMPACT_A_STRUCT "{{A_STRUCT}}"

#define DBU_COMPACT_A                                                                                                  \
	"CASE WHEN len(list_filter(coalesce(json_keys(x.j.a), []), lambda k: k NOT IN (" DBU_COMPACT_ATTR_CODES            \
	"))) > 0\n"                                                                                                        \
	"             THEN error('duck_blocks_from_compact: unknown attribute code at element_order ' || x.ord::VARCHAR\n" \
	"                        || ': ' || to_json(json_keys(x.j.a)))\n"                                                  \
	"             ELSE from_json(x.j.a, '" DBU_COMPACT_A_STRUCT "') END"

// Every refusal, as one expression that is `true` for a line the format allows. It is used
// as a ROW FILTER (WHERE, list_filter), not only inside the decode: a filter runs whatever
// a query projects, a decode does not. On DuckDB v2.1-dev a count(*) read no decoded
// column, so the error inside DBU_COMPACT_KT never ran and a bad file counted as 1 row.
#define DBU_COMPACT_CHECK                                                                                              \
	"CASE WHEN " DBU_COMPACT_TYPE_DECODE "[x.j.t] IS NULL AND NOT contains(coalesce(x.j.t, ''), ':')\n"                \
	"         THEN error('duck_blocks_from_compact: unknown type code ' || coalesce(to_json(x.j.t), 'NULL')\n"         \
	"                    || ' at element_order ' || x.ord::VARCHAR)\n"                                                 \
	"     WHEN len(list_filter(coalesce(json_keys(x.j.a), []), lambda k: k NOT IN (" DBU_COMPACT_ATTR_CODES            \
	"))) > 0\n"                                                                                                        \
	"         THEN error('duck_blocks_from_compact: unknown attribute code at element_order ' || x.ord::VARCHAR\n"     \
	"                    || ': ' || to_json(json_keys(x.j.a)))\n"                                                      \
	"     WHEN x.j.l IS NULL\n"                                                                                        \
	"         THEN error('duck_blocks_from_compact: missing level at element_order ' || x.ord::VARCHAR)\n"             \
	"     ELSE true END"

#define DBU_COMPACT_A_ENTRIES "{{A_ENTRIES}}"

#define DBU_COMPACT_ELEMENT                                                                                            \
	"struct_pack(\n"                                                                                                   \
	"            kind := r.kt[:position(':' IN r.kt) - 1],\n"                                                          \
	"            element_type := r.kt[position(':' IN r.kt) + 1:],\n"                                                  \
	"            content := r.j.c,\n"                                                                                  \
	"            level := CASE WHEN r.j.l IS NULL\n"                                                                   \
	"                          THEN error('duck_blocks_from_compact: missing level at element_order ' || "             \
	"r.ord::VARCHAR)\n"                                                                                                \
	"                          ELSE r.j.l END,\n"                                                                      \
	"            encoding := CASE coalesce(r.j.e, 't') WHEN 't' THEN 'text' WHEN 'j' THEN 'json' ELSE r.j.e END,\n"    \
	"            attributes := map_from_entries(list_sort(map_entries(map_concat(\n"                                   \
	"                coalesce(r.j.m, MAP {}::MAP(VARCHAR, VARCHAR)),\n"                                                \
	"                map_from_entries(list_filter(" DBU_COMPACT_A_ENTRIES ", lambda e: e.v IS NOT NULL)))))),\n"       \
	"            element_order := r.ord)::duck_block"

//! Fill the {{NAME}} pieces of a compact macro's text from the tables in
//! vendor/duck_block_compact.hpp. Text without "{{" is returned unchanged.
static string CompactSql(const char *text) {
	string sql(text);
	if (sql.find("{{") == string::npos) {
		return sql;
	}
	namespace cmp = duck_block::compact;
	auto join = [](idx_t n, const std::function<string(idx_t)> &item) {
		string out;
		for (idx_t i = 0; i < n; i++) {
			out += (i ? ", " : "") + item(i);
		}
		return out;
	};
	auto kt = [&](idx_t i) {
		return string(cmp::TYPE_CODES[i].kind) + ":" + cmp::TYPE_CODES[i].element_type;
	};
	const vector<std::pair<string, string>> pieces = {
	    {"{{VERSION}}", string("'") + cmp::TABLE_VERSION + "'"},
	    {"{{TYPE_ENCODE}}",
	     "MAP {" +
	         join(cmp::TYPE_CODE_COUNT, [&](idx_t i) { return "'" + kt(i) + "': '" + cmp::TYPE_CODES[i].code + "'"; }) +
	         "}"},
	    {"{{TYPE_DECODE}}",
	     "MAP {" +
	         join(cmp::TYPE_CODE_COUNT,
	              [&](idx_t i) { return string("'") + cmp::TYPE_CODES[i].code + "': '" + kt(i) + "'"; }) +
	         "}"},
	    {"{{ATTR_KEYS}}",
	     join(cmp::ATTR_CODE_COUNT, [&](idx_t i) { return string("'") + cmp::ATTR_CODES[i].key + "'"; })},
	    {"{{ATTR_ENCODE}}", "MAP {" +
	                            join(cmp::ATTR_CODE_COUNT,
	                                 [&](idx_t i) {
		                                 return string("'") + cmp::ATTR_CODES[i].key + "': '" +
		                                        cmp::ATTR_CODES[i].code + "'";
	                                 }) +
	                            "}"},
	    {"{{ATTR_CODES}}",
	     join(cmp::ATTR_CODE_COUNT, [&](idx_t i) { return string("'") + cmp::ATTR_CODES[i].code + "'"; })},
	    {"{{A_STRUCT}}", "{" +
	                         join(cmp::ATTR_CODE_COUNT,
	                              [&](idx_t i) { return string("\"") + cmp::ATTR_CODES[i].code + "\": \"VARCHAR\""; }) +
	                         "}"},
	    {"{{A_ENTRIES}}", "[" +
	                          join(cmp::ATTR_CODE_COUNT,
	                               [&](idx_t i) {
		                               return string("{k: '") + cmp::ATTR_CODES[i].key + "', v: r.a." +
		                                      cmp::ATTR_CODES[i].code + "}";
	                               }) +
	                          "]"},
	};
	for (auto &piece : pieces) {
		for (auto at = sql.find(piece.first); at != string::npos;
		     at = sql.find(piece.first, at + piece.second.size())) {
			sql.replace(at, piece.first.size(), piece.second);
		}
	}
	if (sql.find("{{") != string::npos) {
		throw InternalException("compact macro text has an unfilled piece: %s", sql.substr(sql.find("{{"), 40));
	}
	return sql;
}

static const DocScalarMacro DOC_SCALAR_MACROS[] = {
    {DEFAULT_SCHEMA,
     "duck_blocks_get_section",
     {"blocks", "section_pattern", nullptr},
     {{nullptr, nullptr}},
     "(\n"
     "    [\n"
     "        (\n"
     "            SELECT sliced\n"
     "            FROM (\n"
     "                WITH doc AS (SELECT blocks AS b),\n"
     "                     h AS (SELECT e.level AS lvl, e.title AS title, e.id AS id, e.element_order AS ord\n"
     "                           FROM (SELECT unnest(duck_blocks_headings_structs(b)) AS e FROM doc)),\n"
     "                     sec AS (SELECT h1.ord AS s, coalesce(min(h2.ord) - 1, 2147483647) AS e\n"
     "                             FROM h h1 LEFT JOIN h h2 ON h2.ord > h1.ord AND h2.lvl <= h1.lvl\n"
     "                             WHERE h1.title ILIKE '%' || section_pattern || '%' OR h1.id = section_pattern\n"
     "                             GROUP BY h1.ord),\n"
     // OUTERMOST matching sections only: a section contained in another match is
     // dropped. This was a NOT EXISTS anti-join over `sec`, which DuckDB could not
     // decorrelate when `blocks` was a bare TABLE COLUMN -- "INTERNAL Error: Failed to
     // bind column reference: inequal types (INTEGER != BIGINT)", on the published
     // release too; a value or scalar-subquery argument never hit it. Ordered by start
     // (ties: longest first), every earlier row starts at or before this one, so "an
     // earlier row ends at or after me" IS containment, and a window says it without a
     // second reference to the CTE. Agrees with the anti-join on every value input.
     "                     top AS (SELECT s, e FROM (\n"
     "                               SELECT s, e, max(e) OVER (ORDER BY s, e DESC\n"
     "                                      ROWS BETWEEN UNBOUNDED PRECEDING AND 1 PRECEDING) AS prev_e\n"
     "                               FROM sec)\n"
     "                             WHERE prev_e IS NULL OR prev_e < e)\n"
     // element_order is PRESERVED, not renumbered (recovery contract): the outermost
     // sections are disjoint and listed by start, so the concatenation is already in
     // document order, and a caller can join the result back to the document -- or to
     // page_rows -- by element_order. get_pages and sections_like never renumbered;
     // this one did, through a duck_blocks_reorder that ordering never needed.
     "                SELECT flatten(list(duck_blocks_slice(doc.b, top.s, top.e) ORDER BY top.s)) AS sliced\n"
     "                FROM doc, top\n"
     "            )\n"
     "        )\n"
     "    ][1]\n"
     ")"},
    {DEFAULT_SCHEMA,
     "duck_blocks_get_pages",
     {"blocks", "first_page", "last_page", nullptr},
     {{nullptr, nullptr}},
     "(\n"
     "    [\n"
     "        (\n"
     "            SELECT sliced\n"
     "            FROM (\n"
     "                WITH doc AS (SELECT blocks AS b),\n"
     "         brk AS (SELECT e.element_order AS ord,\n"
     "                        try_cast(e.attributes['page_number'] AS INTEGER) AS num\n"
     "                 FROM (SELECT unnest(b) AS e FROM doc)\n"
     "                 WHERE e.kind = 'block' AND e.element_type = 'page_break'),\n"
     "         numbered AS (SELECT ord, coalesce(num, (row_number() OVER (ORDER BY ord))::INTEGER)\n"
     "                             AS page_number FROM brk),\n"
     "         span AS (SELECT page_number, ord AS s,\n"
     "                         coalesce(lead(ord) OVER (ORDER BY ord) - 1, 2147483647) AS e\n"
     "                  FROM numbered\n"
     "                  UNION ALL\n"
     "                  SELECT NULL::INTEGER, 0, ((SELECT min(ord) FROM brk) - 1)::INTEGER\n"
     "                  WHERE (SELECT min(ord) FROM brk) > 0)\n"
     "                SELECT duck_blocks_slice(doc.b, sel.s, sel.e) AS sliced\n"
     "                FROM doc, (SELECT min(s) AS s, max(e) AS e FROM span\n"
     "                           WHERE page_number BETWEEN first_page AND last_page) AS sel\n"
     "                WHERE sel.s IS NOT NULL\n"
     "            )\n"
     "        )\n"
     "    ][1]\n"
     ")"},
    // The two to_text jobs have two NAMES. Rendering joins blocks with a blank line;
    // MATCHING flattens them with a space so a phrase that spans a block boundary is
    // found. Both are right for their job, and `to_text(x, ' ')` looks exactly as
    // deliberate at a call site as `to_text(x)`, so a wrong choice read as a right
    // one -- the sections_like predicate below was the one bare-literal call site and
    // it is what generated the trap. A search on the render form reads wrong on sight
    // now, and so does a render on the match form. (Proposed by Tiiny, ruled by Teague.)
    {DEFAULT_SCHEMA,
     "duck_blocks_to_match_text",
     {"blocks", nullptr},
     {{nullptr, nullptr}},
     "duck_blocks_to_text(blocks, ' ')"},
    // 6.5: the ORIGINAL text behaviour, by construction. The pre-reshape default was
    // literally duck_blocks_to_text(sliced); this is the same call over the same value,
    // moved outside the subquery, which is exact because to_text(NULL) IS NULL (the
    // no-match case). ONE-arg to_text on purpose: the two-arg form a few lines up in
    // sections_like is a SEARCH predicate and flattens with ' ', and a port that used it
    // would keep every row and silently change the separator in the text (Tiiny).
    {DEFAULT_SCHEMA,
     "duck_blocks_get_section_text",
     {"blocks", "section_pattern", nullptr},
     {{nullptr, nullptr}},
     "duck_blocks_to_text(duck_blocks_get_section(blocks, section_pattern))"},
    {DEFAULT_SCHEMA,
     "duck_blocks_get_pages_text",
     {"blocks", "first_page", "last_page", nullptr},
     {{nullptr, nullptr}},
     "duck_blocks_to_text(duck_blocks_get_pages(blocks, first_page, last_page))"},
    // THE COMPACT LINE FORMAT. One JSON object per line, one line per element, in
    // element_order order, after a header line: `t` type code, `c` content, `l` level,
    // `e` encoding, `a` known attribute keys under one-letter codes, `m` every other key
    // unchanged. NULL content and default fields are left out; an element's line number
    // is its order. Design and measurements: duckdb_panduck docs/superpowers/specs/
    // 2026-11-19-compact-blocks-line-format-design.md. The DBU_COMPACT_* pieces above are
    // the source of the code tables; docs/api.md restates them.
    //
    // TYPE CODES: a block is UPPERCASE, an inline is lowercase, a value is a symbol, and
    // a type that is both a block and an inline has one letter in both cases.
    //
    // THE HEADER, {"@":"<this extension's version>"}, names the code table. Codes are
    // only ever ADDED, and a reader refuses a type or attribute code it does not know,
    // so a file from a newer table fails loudly instead of losing a field.
    //
    // NO SUBQUERIES. The first version's bodies were subqueries; given a COLUMN, DuckDB
    // ran them as correlated subqueries, and the reader's cost grew with the square of
    // the document (3,888 elements 2.2 s; 15,552 out of memory at 16 GB). These are list
    // functions over the one value -- linear (62,208 elements: writer 0.65 s, reader
    // 1.5 s). The only subquery left reads this extension's version, and it is
    // uncorrelated.
    //
    // A NULL level, encoding, attributes map or attribute value is REFUSED with an
    // error, not written as a default: the format cannot carry it, and a default would
    // read back as a value the element never had.
    {DEFAULT_SCHEMA,
     "duck_blocks_to_compact",
     {"blocks", nullptr},
     {{nullptr, nullptr}},
     "'{\"@\":' || to_json(" DBU_COMPACT_VERSION ") || '}' || chr(10)\n"
     "    || coalesce(array_to_string(list_transform(\n"
     "        list_sort(list_transform(blocks, lambda b, i: {o: b.element_order, i: i, b: b})),\n"
     "        lambda x: " DBU_COMPACT_LINE "), ''), '')"},
    // Empty lines and header lines (an "@" and no "t") are skipped, so files
    // concatenate; element_order is the element's position among the remaining lines.
    {DEFAULT_SCHEMA,
     "duck_blocks_from_compact",
     {"compact", nullptr},
     {{nullptr, nullptr}},
     "coalesce(list_transform(list_transform(\n"
     "        list_filter(list_transform(\n"
     "            list_filter(\n"
     "                list_transform(list_filter(string_split(compact, chr(10)), lambda line: line <> ''),\n"
     "                               lambda line: {j: from_json(line, '" DBU_COMPACT_LINE_STRUCT "'), ord: 0}),\n"
     "                lambda x: NOT (" DBU_COMPACT_IS_HEADER ")),\n"
     "            lambda x, i: {j: x.j, ord: (i - 1)::INTEGER}),\n"
     "            lambda x: " DBU_COMPACT_CHECK "),\n"
     "        lambda x: {j: x.j, ord: x.ord, kt: " DBU_COMPACT_KT ", a: " DBU_COMPACT_A "}),\n"
     "        lambda r: " DBU_COMPACT_ELEMENT "), []::duck_block[])"},
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

static const DefaultTableMacro DOC_TABLE_MACROS[] = {
    {DEFAULT_SCHEMA,
     "duck_blocks_toc_rows",
     {"blocks", nullptr},
     {{nullptr, nullptr}},
     "SELECT (toc).level AS level,\n"
     "           (toc).title AS title,\n"
     "           (toc).id AS id,\n"
     "           (toc).indent AS indent,\n"
     "           (toc).element_order AS element_order\n"
     "    FROM (SELECT unnest(duck_blocks_toc_structs(blocks)) AS toc)"},
    {DEFAULT_SCHEMA,
     "duck_blocks_diff",
     {"before", "after", nullptr},
     {{nullptr, nullptr}},
     // IDENTITY IS (element_type, content). Not content alone -- a heading and a
     // paragraph reading the same words are different elements, and matching on text
     // would report a promotion as no change. Not (type, content, level) either: a
     // section indented under a new parent would then be a DELETE plus an INSERT,
     // which is the answer least useful to someone asking what changed. Level
     // differences are reported as MOVED instead, so a restructure reads as a
     // restructure.
     //
     // MULTIPLICITY IS COUNTED, so a paragraph appearing twice before and once after
     // is one REMOVED rather than nothing. Comparing sets would hide it -- the same
     // mistake as comparing advisory findings with SELECT DISTINCT.
     //
     // Containers carry no content, so many share the key ('div',''). They aggregate
     // into one row with a count, which is honest: this function cannot tell two
     // empty divs apart, and neither can the vocabulary.
     "WITH a AS (SELECT (u).element_type AS et, coalesce((u).\"content\",'') AS c,\n"
     "                  min((u).\"level\") AS lvl, count(*) AS n\n"
     "           FROM (SELECT unnest(before) AS u) GROUP BY 1,2),\n"
     "     b AS (SELECT (u).element_type AS et, coalesce((u).\"content\",'') AS c,\n"
     "                  min((u).\"level\") AS lvl, count(*) AS n\n"
     "           FROM (SELECT unnest(after) AS u) GROUP BY 1,2),\n"
     "     j AS (SELECT coalesce(a.et,b.et) AS et, coalesce(a.c,b.c) AS c,\n"
     "                  coalesce(a.n,0) AS an, coalesce(b.n,0) AS bn, a.lvl AS al, b.lvl AS bl\n"
     "           FROM a FULL OUTER JOIN b ON a.et=b.et AND a.c=b.c)\n"
     "    SELECT 'ADDED' AS change, et AS element_type, c AS \"content\", bn-an AS n,\n"
     "           al AS before_level, bl AS after_level FROM j WHERE bn > an\n"
     "    UNION ALL\n"
     "    SELECT 'REMOVED', et, c, an-bn, al, bl FROM j WHERE an > bn\n"
     "    UNION ALL\n"
     "    SELECT 'MOVED', et, c, an, al, bl FROM j WHERE an = bn AND an > 0 AND al IS DISTINCT FROM bl\n"
     "    ORDER BY 1, 4 DESC, 2, 3"},
    {DEFAULT_SCHEMA,
     "duck_blocks_quality",
     {"blocks", nullptr},
     {{nullptr, nullptr}},
     // DOCUMENT QUALITY, WHICH IS NOT CONFORMANCE, and the separation is the point.
     // duck_blocks_lint and duck_blocks_validate answer "does this obey the spec".
     // Everything here is about a document that is perfectly conformant and still
     // wrong for a reader: an outline that lies about its hierarchy, a heading with
     // nothing under it, two headings a fragment link cannot tell apart.
     //
     // Kept as a SEPARATE function rather than new lint rules, because a conforming
     // document must be able to run the conformance check and get silence. Folding
     // these in would make every real document warn, and a warning everyone ignores
     // guards nothing.
     "WITH e AS (SELECT (u).element_type AS et, coalesce((u).\"content\",'') AS c,\n"
     "                  (u).\"level\" AS lvl, (u).kind AS k, (u).attributes AS atts,\n"
     "                  (u).element_order AS ord\n"
     "           FROM (SELECT unnest(blocks) AS u)),\n"
     "     h AS (SELECT ord, c, try_cast(atts['heading_level'] AS INTEGER) AS hl,\n"
     "                  lag(try_cast(atts['heading_level'] AS INTEGER)) OVER (ORDER BY ord) AS prev_hl,\n"
     // A heading followed by a DEEPER heading is ordinary structure -- a title above its
     // first subsection -- not an empty section. The first version of this rule fired on
     // that pattern and reported 84 findings across the repo's own 20 documents, nearly
     // all of them correct documents. A rule that fires on the normal case is noise, and
     // noise is how a linter stops being read.
     "                  (SELECT x.et FROM e x WHERE x.ord > e.ord ORDER BY x.ord LIMIT 1) AS next_et,\n"
     "                  (SELECT try_cast(x.atts['heading_level'] AS INTEGER) FROM e x\n"
     "                   WHERE x.ord > e.ord ORDER BY x.ord LIMIT 1) AS next_hl\n"
     "           FROM e WHERE et = 'heading')\n"
     // NO heading_level_skip RULE HERE. duck_blocks_lint has carried one since before
     // this function existed -- "Heading level skipped from hN to hM", validation.cpp
     // line 365 -- and I wrote a second one before checking. A duplicated rule is worse
     // than a missing one: two findings for one defect, and a fix in one place that
     // looks like it did not work.
     "    SELECT ord AS element_order, 'empty_section' AS rule,\n"
     "           'section has no body and no subsections: the next heading is h' || next_hl ||\n"
     "           ', at or above this one' AS message\n"
     "    FROM h WHERE next_et = 'heading' AND next_hl <= hl\n"
     "    UNION ALL\n"
     "    SELECT min(ord), 'duplicate_heading',\n"
     "           'heading text appears ' || count(*) || ' times, so a fragment link cannot address one'\n"
     "    FROM h WHERE c <> '' GROUP BY c, hl HAVING count(*) > 1\n"
     "    UNION ALL\n"
     "    SELECT ord, 'link_without_text',\n"
     "           'link carries no text of its own, so it reads as its URL or as nothing'\n"
     "    FROM e WHERE et = 'link' AND c = ''\n"
     "      AND NOT EXISTS (SELECT 1 FROM e c2 WHERE c2.ord > e.ord AND c2.lvl > e.lvl\n"
     "                      AND c2.ord < coalesce((SELECT min(x.ord) FROM e x\n"
     "                                             WHERE x.ord > e.ord AND x.lvl <= e.lvl), 2147483647))\n"
     "    ORDER BY 1"},
    {DEFAULT_SCHEMA,
     "duck_blocks_page_rows",
     {"blocks", nullptr},
     {{nullptr, nullptr}},
     "WITH doc AS (SELECT blocks AS b),\n"
     "         brk AS (SELECT e.element_order AS ord,\n"
     "                        try_cast(e.attributes['page_number'] AS INTEGER) AS num\n"
     "                 FROM (SELECT unnest(b) AS e FROM doc)\n"
     "                 WHERE e.kind = 'block' AND e.element_type = 'page_break'),\n"
     "         numbered AS (SELECT ord, coalesce(num, (row_number() OVER (ORDER BY ord))::INTEGER)\n"
     "                             AS page_number FROM brk),\n"
     "         span AS (SELECT page_number, ord AS s,\n"
     "                         coalesce(lead(ord) OVER (ORDER BY ord) - 1, 2147483647) AS e\n"
     "                  FROM numbered\n"
     "                  UNION ALL\n"
     "                  SELECT NULL::INTEGER, 0, ((SELECT min(ord) FROM brk) - 1)::INTEGER\n"
     "                  WHERE (SELECT min(ord) FROM brk) > 0),\n"
     // page_rows is an EXTRACTOR, so it hands back the blocks, not just a count of
     // them. The slice is computed once in `sliced` and shared: block_count is len()
     // of the very list returned, so the two can never disagree. Computing the slice
     // twice would let a future edit change one and not the other.
     "         sliced AS (SELECT span.page_number AS page_number, span.s AS s, span.e AS e,\n"
     "                           duck_blocks_slice(doc.b, span.s, span.e) AS sec\n"
     "                    FROM doc, span)\n"
     "    SELECT page_number,\n"
     "           s AS start_order,\n"
     "           e AS end_order,\n"
     "           len(sec) AS block_count,\n"
     "           sec AS blocks\n"
     "    FROM sliced\n"
     "    ORDER BY s"},
    // read_markdown_sections' columns for any reader's blocks (#82). content is the
    // section's own text; `blocks` carries the same slice, so a caller with the
    // markdown extension loaded can render Markdown with duck_blocks_to_md(blocks).
    // The slice starts after the heading's inline run, so the title is not content.
    {DEFAULT_SCHEMA,
     "duck_blocks_sections",
     {"blocks", nullptr},
     {{nullptr, nullptr}},
     "WITH doc AS (SELECT blocks AS b),\n"
     "         sec AS (SELECT unnest(duck_blocks_sections_structs(doc.b)) AS s, doc.b AS b FROM doc),\n"
     "         sliced AS (SELECT s, duck_blocks_slice(b, (s).content_start_order, (s).end_order) AS own FROM sec)\n"
     "    SELECT (s).section_id AS section_id,\n"
     "           (s).section_path AS section_path,\n"
     "           (s).level AS level,\n"
     "           (s).title AS title,\n"
     "           duck_blocks_to_text(own) AS content,\n"
     "           (s).parent_id AS parent_id,\n"
     "           (s).start_order AS start_order,\n"
     "           (s).end_order AS end_order,\n"
     "           own AS blocks\n"
     "    FROM sliced\n"
     "    ORDER BY (s).start_order"},
    {DEFAULT_SCHEMA,
     "duck_blocks_sections_like",
     {"blocks", "query_term", nullptr},
     {{nullptr, nullptr}},
     "WITH doc AS (SELECT blocks AS b),\n"
     "         h AS (SELECT e.element_order AS ord, e.title AS title\n"
     "               FROM (SELECT unnest(duck_blocks_headings_structs(b)) AS e FROM doc)),\n"
     "         span AS (SELECT ord AS s, coalesce(lead(ord) OVER (ORDER BY ord) - 1, 2147483647) AS e, title FROM h\n"
     "                  UNION ALL\n"
     "                  SELECT 0 AS s, coalesce((SELECT min(ord) - 1 FROM h), 2147483647)::INT AS e, '(preamble)' AS "
     "title\n"
     "                  WHERE coalesce((SELECT min(ord) - 1 FROM h), 2147483647)::INT >= 0),\n"
     "         hit AS (SELECT span.s, span.e, span.title, duck_blocks_slice(doc.b, span.s, span.e) AS sec\n"
     "                 FROM doc, span\n"
     // SEARCH JOINS WITH A SPACE, not the default '\n\n'. A search predicate wants a
     // flat text stream; a RENDERING wants paragraph separation, and they are not the
     // same job. With the default, a phrase spanning a block boundary does not match:
     //
     //     to_text(b, ' ') ILIKE '%here second%'  ->  true
     //     to_text(b)      ILIKE '%here second%'  ->  false
     //
     // Found by the duckeye session, who are DELETING their own section/search SQL to
     // call these macros -- their search passed ' ' and this did not, so a naive swap
     // would have silently lost every cross-block hit. Wrong answers, no error, which
     // is the failure this cluster has been chasing all day.
     //
     // The output branches below keep the default separator: what you SEE should read
     // as a document. Only the predicate is flattened.
     "                 WHERE duck_blocks_to_match_text(duck_blocks_slice(doc.b, span.s, span.e)) ILIKE '%' || "
     "query_term || '%')\n"
     "    SELECT hit.title AS section,\n"
     "           hit.s AS start_order,\n"
     "           hit.sec AS blocks\n"
     "    FROM hit\n"
     "    ORDER BY hit.s"},
    // 6.5: original sections_like shape (section, start_order, content VARCHAR), rows
    // unchanged. The parameter is `doc`, not `blocks`, because the inner macro's third
    // COLUMN is named blocks and a parameter of that name would be substituted into it.
    {DEFAULT_SCHEMA,
     "duck_blocks_sections_like_text",
     {"doc", "query_term", nullptr},
     {{nullptr, nullptr}},
     "SELECT section, start_order, duck_blocks_to_text(blocks) AS content\n"
     "    FROM duck_blocks_sections_like(doc, query_term)"},
    // read_compact_blocks(path): one row per element, the seven canonical fields then
    // `filename` (spec 6.4). It STREAMS: read_json scans the lines, in parallel, and a
    // line's position comes from row_number() OVER () across the whole scan minus its
    // file's first number. That is file order only while preserve_insertion_order is on
    // (the default), so with it off the reader REFUSES rather than misnumber. Measured:
    // 486,000 lines in four files, 0 misnumbered, at 1, 8 and 32 threads (1.5 s).
    // Numbering with OVER (PARTITION BY filename) instead misnumbered 70-80% of rows.
    {DEFAULT_SCHEMA,
     "read_compact_blocks",
     {"path", nullptr},
     {{nullptr, nullptr}},
     "SELECT b.kind, b.element_type, b.content, b.level, b.encoding, b.attributes, b.element_order, filename\n"
     "FROM (\n"
     "    SELECT filename, " DBU_COMPACT_ELEMENT " AS b\n"
     "    FROM (\n"
     "        SELECT filename, {j: x.j, ord: x.ord, kt: " DBU_COMPACT_KT ", a: " DBU_COMPACT_A "} AS r\n"
     "        FROM (\n"
     "            SELECT filename, {j: j, ord: (g - min(g) OVER (PARTITION BY filename))::INTEGER} AS x\n"
     "            FROM (\n"
     "                SELECT filename, j, row_number() OVER () AS g\n"
     "                FROM (\n"
     "                    SELECT filename, {\"@\": \"@\", t: t, c: c, l: l, e: e, a: a, m: m} AS j\n"
     "                    FROM read_json(path, format := 'newline_delimited', filename := true,\n"
     "                                   columns := {\"@\": 'VARCHAR', t: 'VARCHAR', c: 'VARCHAR', l: 'INTEGER',\n"
     "                                               e: 'VARCHAR', a: 'JSON', m: 'MAP(VARCHAR, VARCHAR)'})\n"
     "                    WHERE CASE WHEN current_setting('preserve_insertion_order')::BOOLEAN THEN true\n"
     "                               ELSE error('read_compact_blocks: preserve_insertion_order is off, so the scan "
     "order '\n"
     "                                          || 'is not the file order and element_order would be wrong') END\n"
     "                )\n"
     "                WHERE NOT (j.t IS NULL AND j.\"@\" IS NOT NULL)\n"
     "            )\n"
     "        )\n"
     "        WHERE " DBU_COMPACT_CHECK "\n"
     "    )\n"
     ")\n"
     "ORDER BY filename, b.element_order"},
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

void DocMacros::Register(ExtensionLoader &loader) {
	// 1. Register C++ scalar function duck_block_ensure_extension
	// VOLATILE: it LOADs an extension if present, so its result depends on installed
	// state and it has a side effect. Constant-folding either would be wrong.
	// Stability set with the SETTER rather than threaded through the constructor's
	// positional tail. DuckDB v2.0 REMOVED the bind_scalar_function_extended_t
	// parameter, so the run of nullptrs that reached `stability` on v1.5 lands one
	// slot earlier there and a nullptr arrives where a LogicalType varargs is
	// expected. The short constructor plus a setter means the same two lines
	// compile on both.
	auto ensure_ext_func = ScalarFunction("duck_block_ensure_extension", {LogicalType::VARCHAR}, LogicalType::BOOLEAN,
	                                      DbEnsureExtensionFun);
	ensure_ext_func.SetStability(FunctionStability::VOLATILE);
	RegisterScalarWithDesc(loader, ensure_ext_func, {"extension_name"}, "Ensure specified extension is loaded.",
	                       {"duck_block_ensure_extension('json')"});

	// 2. The document-query macros, at LOAD (see above)
	for (idx_t i = 0; DOC_SCALAR_MACROS[i].name != nullptr; i++) {
		// `text` outlives the call: the macro is parsed immediately, nothing keeps a pointer.
		const string text = CompactSql(DOC_SCALAR_MACROS[i].macro);
		DocScalarMacro m = DOC_SCALAR_MACROS[i];
		m.macro = text.c_str();
		auto info = CreateScalarMacroInfo(m);
		FunctionDescription desc;
		desc.categories = {"duck_block_utils"};
		info->descriptions.push_back(desc);
		info->on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		loader.RegisterFunction(*info);
	}
	for (idx_t i = 0; DOC_TABLE_MACROS[i].name != nullptr; i++) {
		const string text = CompactSql(DOC_TABLE_MACROS[i].macro);
		DefaultTableMacro m = DOC_TABLE_MACROS[i];
		m.macro = text.c_str();
		auto info = DefaultTableFunctionGenerator::CreateTableMacroInfo(m);
		FunctionDescription desc;
		desc.categories = {"duck_block_utils"};
		info->descriptions.push_back(desc);
		info->on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		loader.RegisterFunction(*info);
	}

	// 3. Register pragma duck_block_doc_macros
	auto pragma = PragmaFunction::PragmaStatement("duck_block_doc_macros", DocMacrosQuery);
	loader.RegisterFunction(pragma);
}

} // namespace duckdb
