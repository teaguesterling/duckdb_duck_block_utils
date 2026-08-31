#include "doc_macros.hpp"
#include "duckdb/function/pragma_function.hpp"
#include "duckdb/main/client_context.hpp"

namespace duckdb {

static string DocMacrosQuery(ClientContext &context, const FunctionParameters &parameters) {
	string sql = R"DBSQL(
-- ===========================================================================
-- UNIFIED DOCUMENT & AST QUERY PIPELINE MACROS (duck_block_utils)
-- ===========================================================================

-- Helper to safely quote strings in dynamic queries
CREATE OR REPLACE MACRO db_quote(s) AS ('''' || replace(coalesce(s, ''), '''', '''''') || '''');

-- 1. Low-level dispatcher: convert any document source path into LIST(duck_block)
CREATE OR REPLACE MACRO doc_to_blocks(src, format := 'auto', pages := '') AS (
    [
        (
            SELECT list(b::duck_block)
            FROM query(
                CASE
                    WHEN format = 'html' OR format = 'htm' OR (format = 'auto' AND (lower(src) LIKE '%.html' OR lower(src) LIKE '%.htm'))
                        THEN 'SELECT unnest(html_to_duck_blocks(html)) AS b FROM read_html_objects(' || db_quote(src) || ')'
                    WHEN format = 'pdf' OR (format = 'auto' AND lower(src) LIKE '%.pdf')
                        THEN 'SELECT unnest(parse_markdown_to_duck_blocks(pdf_to_markdown(' || db_quote(src) || '))) AS b'
                    WHEN format = 'json' OR (format = 'auto' AND lower(src) LIKE '%.json')
                        THEN 'SELECT unnest(read_pandoc_ast(' || db_quote(src) || ')) AS b'
                    WHEN format = 'ast' OR format = 'code' OR (format = 'auto' AND (
                         lower(src) LIKE '%.py' OR lower(src) LIKE '%.rs' OR lower(src) LIKE '%.go' OR
                         lower(src) LIKE '%.c' OR lower(src) LIKE '%.cpp' OR lower(src) LIKE '%.cc' OR
                         lower(src) LIKE '%.js' OR lower(src) LIKE '%.ts' OR lower(src) LIKE '%.jsx' OR
                         lower(src) LIKE '%.tsx' OR lower(src) LIKE '%.java' OR lower(src) LIKE '%.kt' OR
                         lower(src) LIKE '%.cs' OR lower(src) LIKE '%.swift' OR lower(src) LIKE '%.rb' OR
                         lower(src) LIKE '%.php' OR lower(src) LIKE '%.lua' OR lower(src) LIKE '%.sh' OR
                         lower(src) LIKE '%.zig' OR lower(src) LIKE '%.dart' OR lower(src) LIKE '%.sql' OR
                         lower(src) LIKE '%.gql' OR lower(src) LIKE '%.css'))
                        THEN 'SELECT unnest(blocks) AS b FROM ast_to_blocks_list(' || db_quote(src) || ')'
                    ELSE 'SELECT b FROM read_markdown_blocks(' || db_quote(src) || ') b'
                END
            ) r(b)
        )
    ][1]
);

-- 2. Universal document reader: converts any document into text, markdown, html, or ansi
CREATE OR REPLACE MACRO doc_read(src, format := 'auto', pages := '', output_format := 'text') AS (
    CASE lower(output_format)
        WHEN 'ansi'   THEN db_blocks_render_ansi(doc_to_blocks(src, format, pages))
        WHEN 'text'   THEN db_blocks_to_text(doc_to_blocks(src, format, pages))
        WHEN 'blocks' THEN to_json(doc_to_blocks(src, format, pages))::VARCHAR
        WHEN 'md'     THEN to_json(duck_blocks_to_pandoc_ast(doc_to_blocks(src, format, pages)))::VARCHAR
        WHEN 'pandoc' THEN to_json(duck_blocks_to_pandoc_ast(doc_to_blocks(src, format, pages)))::VARCHAR
        ELSE db_blocks_to_text(doc_to_blocks(src, format, pages))
    END
);

-- 3. Document Table of Contents / Outline Table Function
CREATE OR REPLACE MACRO doc_toc(src, format := 'auto') AS TABLE
    SELECT (toc).level AS level,
           (toc).title AS title,
           (toc).id AS id,
           (toc).indent AS indent,
           (toc).element_order AS element_order
    FROM query(
        'WITH b AS (SELECT doc_to_blocks(' || db_quote(src) || ', ' || db_quote(format) || ') AS blocks) ' ||
        'SELECT unnest(db_blocks_toc(blocks)) AS toc FROM b'
    );

-- 4. Document Section Slicing: slices a section & child subsections to next heading boundary
CREATE OR REPLACE MACRO doc_section(src, section_pattern, format := 'auto', output_format := 'text') AS (
    [
        (
            SELECT
                CASE lower(output_format)
                    WHEN 'ansi'   THEN db_blocks_render_ansi(sliced_blocks)
                    WHEN 'text'   THEN db_blocks_to_text(sliced_blocks)
                    WHEN 'blocks' THEN to_json(sliced_blocks)::VARCHAR
                    WHEN 'md'     THEN to_json(duck_blocks_to_pandoc_ast(sliced_blocks))::VARCHAR
                    ELSE db_blocks_to_text(sliced_blocks)
                END
            FROM (
                WITH doc AS (SELECT doc_to_blocks(src, format) AS blocks),
                     h AS (SELECT e.level AS lvl, e.title AS title, e.id AS id, e.element_order AS ord
                           FROM (SELECT unnest(db_blocks_headings(blocks)) AS e FROM doc)),
                     sec AS (SELECT h1.ord AS s, coalesce(min(h2.ord) - 1, 2147483647) AS e
                             FROM h h1 LEFT JOIN h h2 ON h2.ord > h1.ord AND h2.lvl <= h1.lvl
                             WHERE h1.title ILIKE '%' || section_pattern || '%' OR h1.id = section_pattern
                             GROUP BY h1.ord),
                     top AS (SELECT s, e FROM sec a
                             WHERE NOT EXISTS (SELECT 1 FROM sec b
                                               WHERE b.s <= a.s AND b.e >= a.e AND (b.s, b.e) <> (a.s, a.e)))
                SELECT db_blocks_reorder(flatten(list(db_blocks_slice(doc.blocks, top.s, top.e) ORDER BY top.s))) AS sliced_blocks
                FROM doc, top
            )
        )
    ][1]
);

-- 5. Document Section Search: searches for innermost sections matching query text
CREATE OR REPLACE MACRO doc_search(src, query_term, format := 'auto', output_format := 'text') AS TABLE
    SELECT * FROM query(
        'WITH doc AS (SELECT doc_to_blocks(' || db_quote(src) || ', ' || db_quote(format) || ') AS blocks), ' ||
        '     h AS (SELECT e.element_order AS ord, e.title AS title ' ||
        '           FROM (SELECT unnest(db_blocks_headings(blocks)) AS e FROM doc)), ' ||
        '     span AS (SELECT ord AS s, coalesce(lead(ord) OVER (ORDER BY ord) - 1, 2147483647) AS e, title FROM h ' ||
        '              UNION ALL ' ||
        '              SELECT 0 AS s, coalesce((SELECT min(ord) - 1 FROM h), 2147483647)::INT AS e, ''(preamble)'' AS title ' ||
        '              WHERE coalesce((SELECT min(ord) - 1 FROM h), 2147483647)::INT >= 0), ' ||
        '     hit AS (SELECT span.s, span.e, span.title, db_blocks_slice(doc.blocks, span.s, span.e) AS sec_blocks ' ||
        '             FROM doc, span ' ||
        '             WHERE db_blocks_to_text(db_blocks_slice(doc.blocks, span.s, span.e)) ILIKE ''%'' || ' || db_quote(query_term) || ' || ''%'') ' ||
        'SELECT hit.title AS section, ' ||
        '       hit.s AS start_order, ' ||
        '       CASE ' || db_quote(lower(output_format)) ||
        '           WHEN ''ansi'' THEN db_blocks_render_ansi(hit.sec_blocks) ' ||
        '           WHEN ''text'' THEN db_blocks_to_text(hit.sec_blocks) ' ||
        '           ELSE db_blocks_to_text(hit.sec_blocks) ' ||
        '       END AS content ' ||
        'FROM hit ' ||
        'ORDER BY hit.s'
    );

-- 6. AST CSS Selector Query over source code (or HTML/blocks)
CREATE OR REPLACE MACRO doc_select_blocks(src, css_selector) AS (
    [
        (
            SELECT list(b::duck_block)
            FROM query(
                'WITH ast_table AS (SELECT * FROM read_ast(' || db_quote(src) || ', peek := ''full'')), ' ||
                '     sel_table AS (SELECT * FROM ast_select_from(''ast_table'', ' || db_quote(css_selector) || ')) ' ||
                'SELECT unnest(blocks) AS b FROM (SELECT list(block) AS blocks FROM ast_to_blocks_from(''sel_table''))'
            ) r(b)
        )
    ][1]
);

CREATE OR REPLACE MACRO doc_select(src, css_selector, output_format := 'text') AS (
    CASE lower(output_format)
        WHEN 'ansi'   THEN db_blocks_render_ansi(doc_select_blocks(src, css_selector))
        WHEN 'text'   THEN db_blocks_to_text(doc_select_blocks(src, css_selector))
        WHEN 'md'     THEN to_json(duck_blocks_to_pandoc_ast(doc_select_blocks(src, css_selector)))::VARCHAR
        WHEN 'blocks' THEN to_json(doc_select_blocks(src, css_selector))::VARCHAR
        ELSE db_blocks_to_text(doc_select_blocks(src, css_selector))
    END
);

-- 7. Tabular Dataset Profiling
CREATE OR REPLACE MACRO profile_table(target_query) AS TABLE
    SELECT * FROM query('SUMMARIZE ' || target_query);

CREATE OR REPLACE MACRO profile_file(path) AS TABLE
    SELECT * FROM query('SUMMARIZE SELECT * FROM ' || db_quote(path));
)DBSQL";
	return sql;
}

void DocMacros::Register(ExtensionLoader &loader) {
	// Register pragma duck_block_doc_macros
	auto pragma = PragmaFunction::PragmaStatement("duck_block_doc_macros", DocMacrosQuery);
	loader.RegisterFunction(pragma);
}

} // namespace duckdb
