#include "render_macros.hpp"
#include "duckdb/function/pragma_function.hpp"
#include "duckdb/main/client_context.hpp"

namespace duckdb {

// Returns SQL to create the ANSI terminal rendering macros.
// Rendering targets plain UTF-8 text with ANSI SGR escape sequences — the same
// output format glow/glamour produce. Requires the json extension (autoloaded).
static string DuckBlockRenderQuery(ClientContext &context, const FunctionParameters &parameters) {
	string sql = R"DBSQL(
-- SGR helper: wrap text in an ANSI escape, resetting afterwards
CREATE OR REPLACE MACRO duck_block_ansi(code, s) AS
    chr(27) || '[' || code || 'm' || s || chr(27) || '[0m';

-- Pad to display width (naive length; escape-code and wide-char aware padding
-- needs the future C++ renderer)
CREATE OR REPLACE MACRO duck_block_ansi_pad(s, w) AS
    coalesce(s, '') || repeat(' ', greatest(w - length(coalesce(s, '')), 0));

-- content is rendered literally. Formatting comes from structured inline
-- elements (bold/italic/code/link), not from markdown syntax in content --
-- this extension is format-agnostic and has no markdown awareness. The C++
-- renderer (duck_blocks_render_ansi / duck_blocks_render) styles structured inlines;
-- the pure-SQL helpers below operate per-block and so render content verbatim.
CREATE OR REPLACE MACRO duck_block_ansi_inline(s) AS coalesce(s, '');

-- Code block with a dim gutter and language tag
CREATE OR REPLACE MACRO duck_block_render_code(content, lang) AS
    duck_block_ansi('2', '┃ ') || duck_block_ansi('38;5;222', coalesce(lang, ''))
    || chr(10)
    || array_to_string(
         list_transform(string_split(rtrim(coalesce(content, ''), chr(10)), chr(10)),
                        lambda l: duck_block_ansi('2', '┃ ') || duck_block_ansi('38;5;252', l)),
         chr(10));

-- List from JSON array content: '["item", ...]'
CREATE OR REPLACE MACRO duck_block_render_list_json(j, ordered) AS (
    [ array_to_string(
        list_transform(range(1, len(items) + 1),
          lambda i: duck_block_ansi('38;5;212',
                      CASE WHEN ordered = 'true' THEN lpad(i::VARCHAR, 2, ' ') || '. '
                           ELSE '  • ' END)
               || duck_block_ansi_inline(items[i]))
        , chr(10))
      for items in [from_json(j, '["VARCHAR"]')]
    ][1]
);

-- Table from JSON content: '{"headers": [...], "rows": [[...]]}'
CREATE OR REPLACE MACRO duck_block_render_table_json(j) AS (
    [
      [
        array_to_string(
          list_transform(range(1, len(t.headers) + 1),
                         lambda i: duck_block_ansi('1', duck_block_ansi_pad(t.headers[i], ws[i])))
          , duck_block_ansi('2', ' │ '))
        || chr(10)
        || duck_block_ansi('2', array_to_string(
             list_transform(range(1, len(t.headers) + 1), lambda i: repeat('─', ws[i])), '─┼─'))
        || chr(10)
        || array_to_string(
             list_transform(t.rows, lambda r:
               array_to_string(
                 list_transform(range(1, len(t.headers) + 1),
                                lambda i: duck_block_ansi_pad(duck_block_ansi_inline(r[i]),
                                                      ws[i] + length(duck_block_ansi_inline(r[i])) - length(r[i]))),
                 duck_block_ansi('2', ' │ '))),
             chr(10))
        for ws in [
          list_transform(range(1, len(t.headers) + 1),
            lambda i: greatest(length(t.headers[i]),
                          coalesce(list_aggregate(list_transform(t.rows, lambda r: length(r[i])), 'max'), 0)))
        ]
      ][1]
      for t in [from_json(j, '{"headers": ["VARCHAR"], "rows": [["VARCHAR"]]}')]
    ][1]
);

-- Render a single block element to ANSI text
CREATE OR REPLACE MACRO duck_block_render_one(element_type, content, attributes) AS
    CASE element_type
      WHEN 'heading' THEN
        chr(10) || duck_block_ansi(
          CASE coalesce(attributes['heading_level'], '2')
            WHEN '1' THEN '1;38;5;219'
            WHEN '2' THEN '1;38;5;141'
            ELSE '1;38;5;75' END,
          '▍ ' || duck_block_ansi_inline(content))
      WHEN 'paragraph'  THEN duck_block_ansi_inline(content)
      WHEN 'code'       THEN duck_block_render_code(content, attributes['language'])
      WHEN 'list'       THEN duck_block_render_list_json(content, coalesce(attributes['ordered'], 'false'))
      WHEN 'table'      THEN duck_block_render_table_json(content)
      WHEN 'hr'         THEN duck_block_ansi('2', repeat('─', 64))
      WHEN 'blockquote' THEN duck_block_ansi('3;38;5;115',
                              '▌ ' || replace(coalesce(content, ''), chr(10), chr(10) || '▌ '))
      WHEN 'metadata'   THEN ''
      ELSE duck_block_ansi_inline(content)
    END;

-- Render LIST(duck_block) to a full ANSI document (inline elements skipped).
-- Delegates to the C++ renderer, which adds width-aware word wrapping and theme support;
-- pass an explicit width and/or theme: duck_blocks_render(blocks, 80, 'light')
CREATE OR REPLACE MACRO duck_blocks_render(blocks, width := 0, theme := 'auto') AS
    duck_blocks_render_ansi(blocks, width, theme);

-- JSON array of objects -> table duck_block (headers from first object's keys)
CREATE OR REPLACE MACRO duck_block_json_to_table(j) AS (
    [
      {
        kind: 'block',
        element_type: 'table',
        content: to_json({
          headers: hs,
          rows: list_transform(range(0, json_array_length(j)::INT),
                  lambda i: list_transform(hs,
                    lambda k: coalesce(json_extract_string(j, '$[' || i::VARCHAR || ']."' || k || '"'), '')))
        })::VARCHAR,
        level: 1,
        encoding: 'json',
        attributes: MAP {}::MAP(VARCHAR, VARCHAR),
        element_order: 1
      }::duck_block
      for hs in [json_keys(j, '$[0]')]
    ][1]
);

-- JSON array of objects -> table block as a one-element LIST(duck_block), so it
-- composes with the list-returning builders (duck_block_heading, duck_block_paragraph, ...) and
-- drops straight into duck_blocks_assemble([...]).
CREATE OR REPLACE MACRO duck_blocks_table(j) AS ([ duck_block_json_to_table(j) ]);

-- Pretty-render an arbitrary SQL query as an ANSI table
CREATE OR REPLACE MACRO duck_blocks_render_query(q) AS TABLE
    SELECT duck_blocks_render([duck_block_json_to_table(to_json(list(r))::VARCHAR)]) AS rendered
    FROM query(q) r;

-- A SQL query's results as an embeddable table block LIST(duck_block).
-- The query runs inside a scalar subquery so duck_block_json_to_table sees a plain
-- column (a subquery passed as its argument would land inside a lambda, which
-- DuckDB rejects). Wrapped in a list so it drops into duck_blocks_assemble/duck_blocks_page.
CREATE OR REPLACE MACRO duck_blocks_query_table(q) AS (
    [ (SELECT duck_block_json_to_table(to_json(list(r))::VARCHAR) FROM query(q) r) ]
);

-- Compose a page: an h1 title followed by a list of block-lists (the same shape
-- duck_blocks_assemble takes), assembled with sequential element_order. Embed query
-- results with duck_blocks_query_table:
--   duck_blocks_page('Report', [duck_block_paragraph('rows:'), duck_blocks_query_table('SELECT ...')])
CREATE OR REPLACE MACRO duck_blocks_page(title, blocks) AS (
    duck_blocks_assemble(list_prepend(duck_block_heading(1, title), blocks))
);
)DBSQL";
	return sql;
}

void RenderMacros::Register(ExtensionLoader &loader) {
	auto pragma = PragmaFunction::PragmaStatement("duck_block_render", DuckBlockRenderQuery);
	loader.RegisterFunction(pragma);
}

} // namespace duckdb
