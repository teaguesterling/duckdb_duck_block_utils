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
CREATE OR REPLACE MACRO db_ansi(code, s) AS
    chr(27) || '[' || code || 'm' || s || chr(27) || '[0m';

-- Pad to display width (naive length; escape-code and wide-char aware padding
-- needs the future C++ renderer)
CREATE OR REPLACE MACRO db_ansi_pad(s, w) AS
    coalesce(s, '') || repeat(' ', greatest(w - length(coalesce(s, '')), 0));

-- Inline markdown -> ANSI: **bold**, *italic*, `code`, [text](url)
CREATE OR REPLACE MACRO db_ansi_inline(s) AS
    regexp_replace(
      regexp_replace(
        regexp_replace(
          regexp_replace(coalesce(s, ''),
            '\*\*([^*]+)\*\*', chr(27) || '[1m\1' || chr(27) || '[22m', 'g'),
          '(?:^|\s)\*([^*]+)\*', ' ' || chr(27) || '[3m\1' || chr(27) || '[23m', 'g'),
        '`([^`]+)`', chr(27) || '[38;5;203m\1' || chr(27) || '[39m', 'g'),
      '\[([^\]]+)\]\(([^)]+)\)',
        chr(27) || '[4;38;5;75m\1' || chr(27) || '[0m' || chr(27) || '[2m (\2)' || chr(27) || '[0m', 'g');

-- Code block with a dim gutter and language tag
CREATE OR REPLACE MACRO db_render_code(content, lang) AS
    db_ansi('2', '┃ ') || db_ansi('38;5;222', coalesce(lang, ''))
    || chr(10)
    || array_to_string(
         list_transform(string_split(rtrim(coalesce(content, ''), chr(10)), chr(10)),
                        lambda l: db_ansi('2', '┃ ') || db_ansi('38;5;252', l)),
         chr(10));

-- List from JSON array content: '["item", ...]'
CREATE OR REPLACE MACRO db_render_list_json(j, ordered) AS (
    [ array_to_string(
        list_transform(range(1, len(items) + 1),
          lambda i: db_ansi('38;5;212',
                      CASE WHEN ordered = 'true' THEN lpad(i::VARCHAR, 2, ' ') || '. '
                           ELSE '  • ' END)
               || db_ansi_inline(items[i]))
        , chr(10))
      for items in [from_json(j, '["VARCHAR"]')]
    ][1]
);

-- Table from JSON content: '{"headers": [...], "rows": [[...]]}'
CREATE OR REPLACE MACRO db_render_table_json(j) AS (
    [
      [
        array_to_string(
          list_transform(range(1, len(t.headers) + 1),
                         lambda i: db_ansi('1', db_ansi_pad(t.headers[i], ws[i])))
          , db_ansi('2', ' │ '))
        || chr(10)
        || db_ansi('2', array_to_string(
             list_transform(range(1, len(t.headers) + 1), lambda i: repeat('─', ws[i])), '─┼─'))
        || chr(10)
        || array_to_string(
             list_transform(t.rows, lambda r:
               array_to_string(
                 list_transform(range(1, len(t.headers) + 1),
                                lambda i: db_ansi_pad(db_ansi_inline(r[i]),
                                                      ws[i] + length(db_ansi_inline(r[i])) - length(r[i]))),
                 db_ansi('2', ' │ '))),
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
CREATE OR REPLACE MACRO db_render_block(element_type, content, attributes) AS
    CASE element_type
      WHEN 'heading' THEN
        chr(10) || db_ansi(
          CASE coalesce(attributes['heading_level'], '2')
            WHEN '1' THEN '1;38;5;219'
            WHEN '2' THEN '1;38;5;141'
            ELSE '1;38;5;75' END,
          '▍ ' || db_ansi_inline(content))
      WHEN 'paragraph'  THEN db_ansi_inline(content)
      WHEN 'code'       THEN db_render_code(content, attributes['language'])
      WHEN 'list'       THEN db_render_list_json(content, coalesce(attributes['ordered'], 'false'))
      WHEN 'table'      THEN db_render_table_json(content)
      WHEN 'hr'         THEN db_ansi('2', repeat('─', 64))
      WHEN 'blockquote' THEN db_ansi('3;38;5;115',
                              '▌ ' || replace(coalesce(content, ''), chr(10), chr(10) || '▌ '))
      WHEN 'metadata'   THEN ''
      ELSE db_ansi_inline(content)
    END;

-- Render LIST(duck_block) to a full ANSI document (inline elements skipped)
CREATE OR REPLACE MACRO db_render_blocks(blocks) AS
    array_to_string(
      list_filter(
        list_transform(
          list_filter(blocks, lambda b: b.kind = 'block'),
          lambda b: db_render_block(b.element_type, b.content, b.attributes)),
        lambda s: s <> ''),
      chr(10) || chr(10));

-- JSON array of objects -> table duck_block (headers from first object's keys)
CREATE OR REPLACE MACRO db_json_to_table_block(j) AS (
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

-- Pretty-render an arbitrary SQL query as an ANSI table
CREATE OR REPLACE MACRO db_render_query(q) AS TABLE
    SELECT db_render_blocks([db_json_to_table_block(to_json(list(r))::VARCHAR)]) AS rendered
    FROM query(q) r;
)DBSQL";
	return sql;
}

void RenderMacros::Register(ExtensionLoader &loader) {
	auto pragma = PragmaFunction::PragmaStatement("duck_block_render", DuckBlockRenderQuery);
	loader.RegisterFunction(pragma);
}

} // namespace duckdb
