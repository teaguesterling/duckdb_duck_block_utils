-- duckglow: glow-style markdown rendering in the terminal, entirely in DuckDB.
--
-- Uses the ANSI rendering macros shipped with duck_block_utils
-- (PRAGMA duck_block_render) plus the markdown extension for parsing.
--
-- Usage:
--   duckdb -noheader -list \
--     -c ".read examples/duckglow.sql" \
--     -c "SELECT doc FROM glow('README.md');" | less -R
--
--   -- or pretty-print any query as an ANSI table:
--   duckdb -noheader -list \
--     -c ".read examples/duckglow.sql" \
--     -c "SELECT rendered FROM db_render_query('SELECT * FROM duckdb_settings() LIMIT 10');"

LOAD markdown;
LOAD duck_block_utils;

PRAGMA duck_block_render;

-- Render a whole markdown file to an ANSI document
CREATE OR REPLACE MACRO glow(path) AS TABLE
    SELECT db_render_blocks(list(
               {kind: kind, element_type: element_type, content: content,
                level: level, encoding: encoding, attributes: attributes,
                element_order: element_order}::duck_block
               ORDER BY element_order)) AS doc
    FROM read_markdown_blocks(path)
    WHERE kind = 'block';
