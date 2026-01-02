-- 02_duck_block_utils_create_page_structure.sql
-- Build duck_blocks from project data using duck_block_utils builders
--
-- v4: Uses duck_block_builders completely - no markdown syntax in templates
--     Creates blocks + stores inline element arrays for step 3 to render
--
-- Run: ./build/release/duckdb /tmp/dashboard.duckdb -f test/dashboard/v4/02_duck_block_utils_create_page_structure.sql

LOAD duck_block_utils;

------------------------------------------------------------
-- URL Patterns
------------------------------------------------------------

SET VARIABLE url_github TO 'https://github.com/{0}/{1}';
SET VARIABLE url_github_ci TO 'https://github.com/{0}/{1}/actions/workflows/ci.yml/badge.svg';
SET VARIABLE url_extension_ci TO 'https://github.com/duckdb/community-extensions/actions/workflows/{0}.yml/badge.svg';
SET VARIABLE url_rtd_badge TO 'https://readthedocs.org/projects/{0}/badge/';

------------------------------------------------------------
-- Header and footer blocks (simple, no inlines needed)
------------------------------------------------------------

CREATE OR REPLACE TABLE header_blocks AS
WITH owner AS (SELECT default_owner FROM config)
SELECT 1 AS sort_order, db_heading('DuckDB Extensions Dashboard', 1) AS block
UNION ALL
SELECT 2, db_paragraph('A collection of DuckDB extensions and related projects by ' || (SELECT default_owner FROM owner) || '.');

CREATE OR REPLACE TABLE footer_inlines AS
SELECT [db_italic('Generated with DuckDB extensions: yaml, duck_block_utils, markdown')] AS inlines;

------------------------------------------------------------
-- Category headers
------------------------------------------------------------

CREATE OR REPLACE TABLE category_blocks AS
WITH categories AS (
    SELECT * FROM (VALUES
        ('DuckDB Extensions',          100),
        ('Shell Helpers',              200),
        ('Result and Data Management', 300),
        ('Watched Projects',           400),
        ('Legacy Projects',            500)
    ) AS t(name, sort_order)
)
SELECT
    c.sort_order,
    c.name AS category,
    db_heading(c.name, 2) AS block
FROM categories c
WHERE c.name IN (SELECT DISTINCT category FROM projects);

------------------------------------------------------------
-- Project data with inline element arrays
------------------------------------------------------------

CREATE OR REPLACE TABLE project_data AS
WITH
owner AS (
    SELECT default_owner FROM config
),

categories AS (
    SELECT * FROM (VALUES
        ('DuckDB Extensions',          100),
        ('Shell Helpers',              200),
        ('Result and Data Management', 300),
        ('Watched Projects',           400),
        ('Legacy Projects',            500)
    ) AS t(name, sort_order)
),

projects_with_urls AS (
    SELECT
        p.*,
        c.sort_order AS cat_order,
        coalesce(p.owner, o.default_owner) AS effective_owner,
        format(getvariable('url_github'), coalesce(p.owner, o.default_owner), p.name) AS github_url,
        format(getvariable('url_github_ci'), coalesce(p.owner, o.default_owner), p.name) AS github_ci_url,
        format(getvariable('url_extension_ci'), coalesce(p.extension_name, p.name)) AS ext_ci_url,
        format(getvariable('url_rtd_badge'), replace(p.name, '_', '-')) AS docs_badge_url
    FROM projects p
    JOIN categories c ON p.category = c.name
    CROSS JOIN owner o
)

SELECT
    cat_order + row_number() OVER (PARTITION BY category ORDER BY name) AS sort_order,
    category,
    name,
    description,
    -- Heading block for project name
    db_heading(name, 3) AS heading_block,
    -- Description block (empty if no description - inlines will fill it)
    db_paragraph(coalesce(description, '')) AS desc_block,
    -- Inline elements for "No description" italic
    CASE WHEN description IS NULL
        THEN [db_italic('No description')]
    END AS desc_inlines,
    -- Inline elements for each link type
    [db_link('GitHub', github_url), db_text(' '), db_inline_image(github_ci_url, 'CI')] AS github_inlines,
    CASE WHEN extension_page IS NOT NULL
        THEN [db_link('Extension', extension_page), db_text(' '), db_inline_image(ext_ci_url, 'Extension CI')]
    END AS extension_inlines,
    CASE WHEN docs IS NOT NULL
        THEN [db_link('Docs', docs), db_text(' '), db_inline_image(docs_badge_url, 'Docs')]
    END AS docs_inlines
FROM projects_with_urls;

------------------------------------------------------------
-- Summary
------------------------------------------------------------

SELECT 'header_blocks' AS table_name, count(*) AS rows FROM header_blocks
UNION ALL SELECT 'category_blocks', count(*) FROM category_blocks
UNION ALL SELECT 'project_data', count(*) FROM project_data
UNION ALL SELECT 'footer_inlines', count(*) FROM footer_inlines;
