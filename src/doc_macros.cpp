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
	// Loaded HERE, at registration, not inside the macros. A statement binds fully
	// before it executes, so a macro body that calls duck_blocks_to_md() cannot make
	// markdown appear in time -- db_ensure_extension would run too late and the
	// statement fails to bind, uncatchably. Doing it in this earlier statement is
	// what makes the delegation work at all.
	ExtensionHelper::TryAutoLoadExtension(context, "json");
	ExtensionHelper::TryAutoLoadExtension(context, "markdown");
	ExtensionHelper::TryAutoLoadExtension(context, "webbed");

	string sql = R"DBSQL(
-- ===========================================================================
-- UNIFIED DOCUMENT & AST QUERY PIPELINE MACROS (duck_block_utils)
-- ===========================================================================

-- Quote a string for embedding in a dynamic query. Retained solely for
-- profile_file(), which builds a SUMMARIZE statement; the dispatch macros that
-- were its main consumer now live in panduck.
CREATE OR REPLACE MACRO db_quote(s) AS ('''' || replace(coalesce(s, ''), '''', '''''') || '''');

-- 0. Output rendering. Format-specific writers live in the format extensions:
-- duck_blocks_to_md is duckdb_markdown's, duck_blocks_to_html is webbed's. This
-- extension owns only text, ansi, blocks and pandoc, and never gains a markdown
-- or HTML writer of its own.
CREATE OR REPLACE MACRO doc_render(blocks, output_format := 'text') AS (
    CASE lower(output_format)
        WHEN 'text'   THEN db_blocks_to_text(blocks)
        WHEN 'ansi'   THEN db_blocks_render_ansi(blocks)
        WHEN 'blocks' THEN to_json(blocks)::VARCHAR
        WHEN 'pandoc' THEN to_json(duck_blocks_to_pandoc_ast(blocks))::VARCHAR
        WHEN 'md'     THEN duck_blocks_to_md(blocks)
        WHEN 'html'   THEN duck_blocks_to_html(blocks)
        ELSE error('doc_render: unsupported output_format ' || coalesce(output_format, 'NULL') ||
                   '; expected one of text, ansi, blocks, pandoc, md, html')
    END
);

-- 3. Table of contents over a block list.
CREATE OR REPLACE MACRO doc_toc(blocks) AS TABLE
    SELECT (toc).level AS level,
           (toc).title AS title,
           (toc).id AS id,
           (toc).indent AS indent,
           (toc).element_order AS element_order
    FROM (SELECT unnest(db_blocks_toc(blocks)) AS toc);

-- 4. Slice a section and its child subsections to the next heading boundary.
-- The `top` CTE selects innermost non-overlapping matches; getting it wrong
-- returns the wrong slice silently rather than failing, so it is copied verbatim
-- from the path-taking version -- only the block source and the output changed.
CREATE OR REPLACE MACRO doc_section(blocks, section_pattern, output_format := 'text') AS (
    [
        (
            SELECT doc_render(sliced_blocks, output_format)
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
                SELECT db_blocks_reorder(flatten(list(db_blocks_slice(doc.b, top.s, top.e) ORDER BY top.s))) AS sliced_blocks
                FROM doc, top
            )
        )
    ][1]
);

-- 5. Search sections for matching text. The preamble span in the UNION ALL makes
-- content before the first heading searchable and is load-bearing.
CREATE OR REPLACE MACRO doc_search(blocks, query_term, output_format := 'text') AS TABLE
    WITH doc AS (SELECT blocks AS b),
         h AS (SELECT e.element_order AS ord, e.title AS title
               FROM (SELECT unnest(db_blocks_headings(b)) AS e FROM doc)),
         span AS (SELECT ord AS s, coalesce(lead(ord) OVER (ORDER BY ord) - 1, 2147483647) AS e, title FROM h
                  UNION ALL
                  SELECT 0 AS s, coalesce((SELECT min(ord) - 1 FROM h), 2147483647)::INT AS e, '(preamble)' AS title
                  WHERE coalesce((SELECT min(ord) - 1 FROM h), 2147483647)::INT >= 0),
         hit AS (SELECT span.s, span.e, span.title, db_blocks_slice(doc.b, span.s, span.e) AS sec_blocks
                 FROM doc, span
                 WHERE db_blocks_to_text(db_blocks_slice(doc.b, span.s, span.e)) ILIKE '%' || query_term || '%')
    SELECT hit.title AS section,
           hit.s AS start_order,
           doc_render(hit.sec_blocks, output_format) AS content
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
