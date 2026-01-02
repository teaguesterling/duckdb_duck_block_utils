-- 02_duck_block_utils_create_page_structure.sql
-- Build db_blocks from project data
-- Run with: ./build/release/duckdb /tmp/dashboard.duckdb -f test/dashboard/02_duck_block_utils_create_page_structure.sql

-- Build document blocks for the dashboard
CREATE OR REPLACE TABLE page_blocks AS
WITH
-- Title and intro
header_blocks AS (
    SELECT 1 as sort_order, db_heading('DuckDB Extensions Dashboard', 1) as block
    UNION ALL
    SELECT 2, db_paragraph('A collection of DuckDB extensions and related projects by ' ||
        (SELECT default_owner FROM config) || '.')
),

-- Category sections
category_data AS (
    SELECT DISTINCT category,
        CASE category
            WHEN 'DuckDB Extensions' THEN 100
            WHEN 'Shell Helpers' THEN 200
            WHEN 'Result and Data Management' THEN 300
            WHEN 'Watched Projects' THEN 400
            WHEN 'Legacy Projects' THEN 500
        END as cat_order
    FROM projects
),

category_headers AS (
    SELECT cat_order as sort_order, db_heading(category, 2) as block
    FROM category_data
),

-- Project entries with bulleted links and badges
project_blocks AS (
    SELECT
        cd.cat_order + row_number() OVER (PARTITION BY p.category ORDER BY p.name) as sort_order,
        db_paragraph(
            '### ' || p.name || chr(10) || chr(10) ||
            coalesce(p.description, '_No description_') || chr(10) || chr(10) ||
            -- Bulleted list of links with inline badges
            '- [GitHub](https://github.com/' ||
                (SELECT default_owner FROM config) || '/' || p.name || ')' ||
                ' • ![CI](https://github.com/' || (SELECT default_owner FROM config) || '/' || p.name || '/actions/workflows/ci.yml/badge.svg)' ||
            CASE WHEN p.extension_page IS NOT NULL THEN
                chr(10) || '- [Extension](' || p.extension_page || ')' ||
                ' • ![Extension CI](https://github.com/duckdb/community-extensions/actions/workflows/' ||
                    coalesce(p.extension_name, p.name) || '.yml/badge.svg)'
            ELSE '' END ||
            CASE WHEN p.docs IS NOT NULL THEN
                chr(10) || '- [Docs](' || p.docs || ')' ||
                ' • ![Docs](https://readthedocs.org/projects/' || replace(p.name, '_', '-') || '/badge/)'
            ELSE '' END
        ) as block
    FROM projects p
    JOIN category_data cd ON p.category = cd.category
),

-- Footer
footer AS (
    SELECT 999 as sort_order,
        db_paragraph('---' || chr(10) || chr(10) ||
            '_Generated with DuckDB extensions: yaml, duck_block_utils, markdown_') as block
)

-- Combine all
SELECT sort_order, block FROM header_blocks
UNION ALL SELECT sort_order, block FROM category_headers
UNION ALL SELECT sort_order, block FROM project_blocks
UNION ALL SELECT sort_order, block FROM footer
ORDER BY sort_order;

-- Show block count
SELECT count(*) as total_blocks FROM page_blocks;
