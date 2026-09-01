#include "doc_macros.hpp"
#include "duckdb/function/pragma_function.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/extension_helper.hpp"

namespace duckdb {

// C++ Scalar function: db_ensure_extension(VARCHAR) -> BOOLEAN
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
CREATE OR REPLACE MACRO db_quote(s) AS ('''' || replace(coalesce(s, ''), '''', '''''') || '''');

-- Document queries over a block list. These take LIST(duck_block), never a path:
-- path -> blocks is panduck's job (read_panduck_doc / panduck_read_blocks), and
-- this extension depends on nothing.
--
-- Named db_* like everything else here. The `doc_*` prefix now belongs to panduck.
-- There is no shared render helper: the output CASE below is four direct calls,
-- and wrapping it in an indirection bought a name without buying anything else.

-- Table of contents, splatted into columns. The scalar db_blocks_toc() returns
-- the same data as a LIST(STRUCT) when a list is what you want.
CREATE OR REPLACE MACRO db_toc(blocks) AS TABLE
    SELECT (toc).level AS level,
           (toc).title AS title,
           (toc).id AS id,
           (toc).indent AS indent,
           (toc).element_order AS element_order
    FROM (SELECT unnest(db_blocks_toc(blocks)) AS toc);

-- Slice a section and its child subsections to the next heading boundary.
-- The `top` CTE selects innermost non-overlapping matches; getting it wrong
-- returns the wrong slice SILENTLY rather than failing, so treat it as fixed.
CREATE OR REPLACE MACRO db_section(blocks, section_pattern, output_format := 'text') AS (
    [
        (
            SELECT CASE lower(output_format)
                       WHEN 'text'   THEN db_blocks_to_text(sliced)
                       WHEN 'ansi'   THEN db_blocks_render_ansi(sliced)
                       WHEN 'blocks' THEN to_json(sliced)::VARCHAR
                       WHEN 'pandoc' THEN to_json(duck_blocks_to_pandoc_ast(sliced))::VARCHAR
                       ELSE error('db_section: unsupported output_format ' ||
                                  coalesce(output_format, 'NULL') ||
                                  '; expected text, ansi, blocks or pandoc')
                   END
            FROM (
                WITH doc AS (SELECT blocks AS b),
                     h AS (SELECT e.level AS lvl, e.title AS title, e.id AS id, e.element_order AS ord
                           FROM (SELECT unnest(db_blocks_headings(b)) AS e FROM doc)),
                     sec AS (SELECT h1.ord AS s, coalesce(min(h2.ord) - 1, 2147483647) AS e
                             FROM h h1 LEFT JOIN h h2 ON h2.ord > h1.ord AND h2.lvl <= h1.lvl
                             WHERE h1.title ILIKE '%' || section_pattern || '%' OR h1.id = section_pattern
                             GROUP BY h1.ord),
                     top AS (SELECT s, e FROM sec a
                             WHERE NOT EXISTS (SELECT 1 FROM sec b2
                                               WHERE b2.s <= a.s AND b2.e >= a.e AND (b2.s, b2.e) <> (a.s, a.e)))
                SELECT db_blocks_reorder(flatten(list(db_blocks_slice(doc.b, top.s, top.e) ORDER BY top.s))) AS sliced
                FROM doc, top
            )
        )
    ][1]
);

-- Sections whose text matches a pattern. The preamble span in the UNION ALL makes
-- content before the first heading searchable and is load-bearing.
CREATE OR REPLACE MACRO db_sections_like(blocks, query_term, output_format := 'text') AS TABLE
    WITH doc AS (SELECT blocks AS b),
         h AS (SELECT e.element_order AS ord, e.title AS title
               FROM (SELECT unnest(db_blocks_headings(b)) AS e FROM doc)),
         span AS (SELECT ord AS s, coalesce(lead(ord) OVER (ORDER BY ord) - 1, 2147483647) AS e, title FROM h
                  UNION ALL
                  SELECT 0 AS s, coalesce((SELECT min(ord) - 1 FROM h), 2147483647)::INT AS e, '(preamble)' AS title
                  WHERE coalesce((SELECT min(ord) - 1 FROM h), 2147483647)::INT >= 0),
         hit AS (SELECT span.s, span.e, span.title, db_blocks_slice(doc.b, span.s, span.e) AS sec
                 FROM doc, span
                 WHERE db_blocks_to_text(db_blocks_slice(doc.b, span.s, span.e)) ILIKE '%' || query_term || '%')
    SELECT hit.title AS section,
           hit.s AS start_order,
           CASE lower(output_format)
               WHEN 'text'   THEN db_blocks_to_text(hit.sec)
               WHEN 'ansi'   THEN db_blocks_render_ansi(hit.sec)
               WHEN 'blocks' THEN to_json(hit.sec)::VARCHAR
               WHEN 'pandoc' THEN to_json(duck_blocks_to_pandoc_ast(hit.sec))::VARCHAR
               ELSE error('db_sections_like: unsupported output_format ' ||
                          coalesce(output_format, 'NULL') ||
                          '; expected text, ansi, blocks or pandoc')
           END AS content
    FROM hit
    ORDER BY hit.s;

-- 7. Tabular Dataset Profiling
CREATE OR REPLACE MACRO profile_table(target_query) AS TABLE
    SELECT * FROM query('SUMMARIZE ' || target_query);

CREATE OR REPLACE MACRO profile_file(path) AS TABLE
    SELECT * FROM query('SUMMARIZE SELECT * FROM ' || db_quote(path));

)DBSQL";
	return sql;
}

void DocMacros::Register(ExtensionLoader &loader) {
	// 1. Register C++ scalar function db_ensure_extension
	auto ensure_ext_func =
	    ScalarFunction("db_ensure_extension", {LogicalType::VARCHAR}, LogicalType::BOOLEAN, DbEnsureExtensionFun);
	loader.RegisterFunction(ensure_ext_func);

	// 2. Register pragma duck_block_doc_macros
	auto pragma = PragmaFunction::PragmaStatement("duck_block_doc_macros", DocMacrosQuery);
	loader.RegisterFunction(pragma);
}

} // namespace duckdb
