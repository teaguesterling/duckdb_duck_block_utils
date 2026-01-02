-- 03_markdown_render.sql
-- Render db_blocks to Markdown
--
-- Run: duckdb_markdown/build/release/duckdb /tmp/dashboard.duckdb -f 03_markdown_render.sql

-- Render blocks to markdown
CREATE OR REPLACE TABLE rendered_markdown AS
SELECT duck_blocks_to_md(
    list(block ORDER BY sort_order)
) AS markdown
FROM page_blocks;

-- Export to file
COPY (
    SELECT markdown FROM rendered_markdown
)
TO 'projects_dashboard.md' (FORMAT CSV, HEADER FALSE, QUOTE '');

-- Preview
SELECT
    format('Generated {} characters', length(markdown)) AS status,
    left(markdown, 500) || '...' AS preview
FROM rendered_markdown;
