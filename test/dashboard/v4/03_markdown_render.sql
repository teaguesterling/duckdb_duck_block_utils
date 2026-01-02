-- 03_markdown_render.sql
-- Render duck_blocks to Markdown
--
-- v4: Renders blocks and inline arrays from step 2 using db_elements_to_md()
--     No duck_block_utils needed - only markdown rendering functions
--
-- Run: duckdb_markdown/build/release/duckdb /tmp/dashboard.duckdb -f 03_markdown_render.sql

------------------------------------------------------------
-- Render all content to markdown
------------------------------------------------------------

CREATE OR REPLACE TABLE rendered_markdown AS
WITH
-- Header: render each block
header_md AS (
    SELECT sort_order, db_element_to_md(block) AS md
    FROM header_blocks
),

-- Categories: render each block
category_md AS (
    SELECT sort_order, db_element_to_md(block) AS md
    FROM category_blocks
),

-- Projects: render heading, description (with inlines), and link list
project_md AS (
    SELECT
        sort_order,
        -- Heading
        db_element_to_md(heading_block)
        -- Description (with inline content if no description text)
        || CASE
            WHEN desc_inlines IS NOT NULL
            THEN db_elements_to_md(desc_inlines) || E'\n\n'
            ELSE db_element_to_md(desc_block)
        END
        -- Links as unordered list items
        || '- ' || db_elements_to_md(github_inlines) || E'\n'
        || CASE WHEN extension_inlines IS NOT NULL
            THEN '- ' || db_elements_to_md(extension_inlines) || E'\n'
            ELSE ''
        END
        || CASE WHEN docs_inlines IS NOT NULL
            THEN '- ' || db_elements_to_md(docs_inlines) || E'\n'
            ELSE ''
        END
        || E'\n'
        AS md
    FROM project_data
),

-- Footer: hr + rendered inlines
footer_md AS (
    SELECT
        998 AS sort_order,
        E'---\n\n' AS md
    UNION ALL
    SELECT
        999 AS sort_order,
        db_elements_to_md(inlines) || E'\n\n' AS md
    FROM footer_inlines
),

-- Combine all markdown
all_md AS (
    SELECT sort_order, md FROM header_md
    UNION ALL SELECT sort_order, md FROM category_md
    UNION ALL SELECT sort_order, md FROM project_md
    UNION ALL SELECT sort_order, md FROM footer_md
)

SELECT string_agg(md, '' ORDER BY sort_order) AS markdown
FROM all_md;

------------------------------------------------------------
-- Output
------------------------------------------------------------

COPY (SELECT markdown FROM rendered_markdown)
TO 'projects_dashboard.md' (FORMAT CSV, HEADER FALSE, QUOTE '');

SELECT
    format('Generated {} characters', length(markdown)) AS status,
    left(markdown, 500) || '...' AS preview
FROM rendered_markdown;
