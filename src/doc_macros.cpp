#include "doc_macros.hpp"
#include "duckdb/catalog/default/default_functions.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"
#include "duckdb/function/pragma_function.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/extension_helper.hpp"

namespace duckdb {

// C++ Scalar function: duck_block_ensure_extension(VARCHAR) -> BOOLEAN
static void DbEnsureExtensionFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &context = state.GetContext();
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

static const DefaultMacro DOC_SCALAR_MACROS[] = {
    {DEFAULT_SCHEMA,
     "duck_blocks_get_section",
     {"blocks", "section_pattern", nullptr},
     {{"output_format", "'text'"}, {nullptr, nullptr}},
     "(\n"
     "    [\n"
     "        (\n"
     "            SELECT CASE lower(output_format)\n"
     "                       WHEN 'text'   THEN duck_blocks_to_text(sliced)\n"
     "                       WHEN 'ansi'   THEN duck_blocks_render_ansi(sliced)\n"
     "                       WHEN 'blocks' THEN to_json(sliced)::VARCHAR\n"
     "                       WHEN 'pandoc' THEN to_json(duck_blocks_to_pandoc_ast(sliced))::VARCHAR\n"
     "                       ELSE error('duck_blocks_get_section: unsupported output_format ' ||\n"
     "                                  coalesce(output_format, 'NULL') ||\n"
     "                                  '; expected text, ansi, blocks or pandoc')\n"
     "                   END\n"
     "            FROM (\n"
     "                WITH doc AS (SELECT blocks AS b),\n"
     "                     h AS (SELECT e.level AS lvl, e.title AS title, e.id AS id, e.element_order AS ord\n"
     "                           FROM (SELECT unnest(duck_blocks_headings(b)) AS e FROM doc)),\n"
     "                     sec AS (SELECT h1.ord AS s, coalesce(min(h2.ord) - 1, 2147483647) AS e\n"
     "                             FROM h h1 LEFT JOIN h h2 ON h2.ord > h1.ord AND h2.lvl <= h1.lvl\n"
     "                             WHERE h1.title ILIKE '%' || section_pattern || '%' OR h1.id = section_pattern\n"
     "                             GROUP BY h1.ord),\n"
     "                     top AS (SELECT s, e FROM sec a\n"
     "                             WHERE NOT EXISTS (SELECT 1 FROM sec b2\n"
     "                                               WHERE b2.s <= a.s AND b2.e >= a.e AND (b2.s, b2.e) <> (a.s, "
     "a.e)))\n"
     "                SELECT duck_blocks_reorder(flatten(list(duck_blocks_slice(doc.b, top.s, top.e) ORDER BY top.s))) "
     "AS sliced\n"
     "                FROM doc, top\n"
     "            )\n"
     "        )\n"
     "    ][1]\n"
     ")"},
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
     "    FROM (SELECT unnest(duck_blocks_toc(blocks)) AS toc)"},
    {DEFAULT_SCHEMA,
     "duck_blocks_sections_like",
     {"blocks", "query_term", nullptr},
     {{"output_format", "'text'"}, {nullptr, nullptr}},
     "WITH doc AS (SELECT blocks AS b),\n"
     "         h AS (SELECT e.element_order AS ord, e.title AS title\n"
     "               FROM (SELECT unnest(duck_blocks_headings(b)) AS e FROM doc)),\n"
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
     "                 WHERE duck_blocks_to_text(duck_blocks_slice(doc.b, span.s, span.e), ' ') ILIKE '%' || "
     "query_term || '%')\n"
     "    SELECT hit.title AS section,\n"
     "           hit.s AS start_order,\n"
     "           CASE lower(output_format)\n"
     "               WHEN 'text'   THEN duck_blocks_to_text(hit.sec)\n"
     "               WHEN 'ansi'   THEN duck_blocks_render_ansi(hit.sec)\n"
     "               WHEN 'blocks' THEN to_json(hit.sec)::VARCHAR\n"
     "               WHEN 'pandoc' THEN to_json(duck_blocks_to_pandoc_ast(hit.sec))::VARCHAR\n"
     "               ELSE error('duck_blocks_sections_like: unsupported output_format ' ||\n"
     "                          coalesce(output_format, 'NULL') ||\n"
     "                          '; expected text, ansi, blocks or pandoc')\n"
     "           END AS content\n"
     "    FROM hit\n"
     "    ORDER BY hit.s"},
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

void DocMacros::Register(ExtensionLoader &loader) {
	// 1. Register C++ scalar function duck_block_ensure_extension
	// VOLATILE: it LOADs an extension if present, so its result depends on installed
	// state and it has a side effect. Constant-folding either would be wrong.
	auto ensure_ext_func = ScalarFunction("duck_block_ensure_extension", {LogicalType::VARCHAR}, LogicalType::BOOLEAN,
	                                      DbEnsureExtensionFun, nullptr, nullptr, nullptr, nullptr,
	                                      LogicalType(LogicalTypeId::INVALID), FunctionStability::VOLATILE);
	loader.RegisterFunction(ensure_ext_func);

	// 2. The document-query macros, at LOAD (see above)
	for (idx_t i = 0; DOC_SCALAR_MACROS[i].name != nullptr; i++) {
		auto info = DefaultFunctionGenerator::CreateInternalMacroInfo(DOC_SCALAR_MACROS[i]);
		loader.RegisterFunction(*info);
	}
	for (idx_t i = 0; DOC_TABLE_MACROS[i].name != nullptr; i++) {
		auto info = DefaultTableFunctionGenerator::CreateTableMacroInfo(DOC_TABLE_MACROS[i]);
		loader.RegisterFunction(*info);
	}

	// 3. Register pragma duck_block_doc_macros
	auto pragma = PragmaFunction::PragmaStatement("duck_block_doc_macros", DocMacrosQuery);
	loader.RegisterFunction(pragma);
}

} // namespace duckdb
