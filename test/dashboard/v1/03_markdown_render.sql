-- 03_markdown_render.sql
-- Render db_blocks to Markdown
-- Run with: ../duckdb_markdown/build/release/duckdb /tmp/dashboard.duckdb -f 03_markdown_render.sql

-- Collect blocks in order and render to markdown
CREATE OR REPLACE TABLE rendered_markdown AS
SELECT duck_blocks_to_md(
    list(block ORDER BY sort_order)
) as markdown
FROM page_blocks;

-- Export to file
COPY (SELECT markdown FROM rendered_markdown) TO 'projects_dashboard.md' (FORMAT CSV, HEADER FALSE, QUOTE '');

-- Show preview (first 2000 chars)
SELECT left(markdown, 2000) || '...' as preview FROM rendered_markdown;
