-- 02_duck_block_utils_create_page_structure.sql
-- Build db_blocks from project data
--
-- Run: ./build/release/duckdb /tmp/dashboard.duckdb -f test/dashboard/v2/02_duck_block_utils_create_page_structure.sql

CREATE OR REPLACE TABLE page_blocks AS
WITH
-- Get config value once
owner AS (
    SELECT default_owner FROM config
),

-- Category ordering
categories AS (
    SELECT * FROM (VALUES
        ('DuckDB Extensions',          100),
        ('Shell Helpers',              200),
        ('Result and Data Management', 300),
        ('Watched Projects',           400),
        ('Legacy Projects',            500)
    ) AS t(name, sort_order)
),

-- Enrich projects with computed URLs
projects_enriched AS (
    SELECT
        p.*,
        c.sort_order AS category_order,
        -- Build URL structs for cleaner formatting
        {
            github:        format('https://github.com/{}/{}', (SELECT default_owner FROM owner), p.name),
            github_ci:     format('https://github.com/{}/{}/actions/workflows/ci.yml/badge.svg',
                                  (SELECT default_owner FROM owner), p.name),
            extension:     p.extension_page,
            extension_ci:  format('https://github.com/duckdb/community-extensions/actions/workflows/{}.yml/badge.svg',
                                  coalesce(p.extension_name, p.name)),
            docs:          p.docs,
            docs_badge:    format('https://readthedocs.org/projects/{}/badge/', replace(p.name, '_', '-'))
        } AS urls
    FROM projects p
    JOIN categories c ON p.category = c.name
),

-- Header blocks
header AS (
    SELECT h.sort_order, h.block
    FROM (
        SELECT unnest([
            {
                sort_order: 1,
                block: db_heading('DuckDB Extensions Dashboard', 1)
            },
            {
                sort_order: 2,
                block: db_paragraph(format(
                    'A collection of DuckDB extensions and related projects by {}.',
                    (SELECT default_owner FROM owner)
                ))
            }
        ]) AS h
    )
),

-- Category header blocks
category_headers AS (
    SELECT
        c.sort_order,
        db_heading(c.name, 2) AS block
    FROM categories c
    WHERE c.name IN (SELECT DISTINCT category FROM projects)
),

-- Project content blocks
project_content AS (
    SELECT
        p.category_order + row_number() OVER (PARTITION BY p.category ORDER BY p.name) AS sort_order,
        db_paragraph(array_to_string([
            -- Project heading
            format('### {}', p.name),
            '',
            -- Description
            coalesce(p.description, '_No description_'),
            '',
            -- GitHub link with CI badge
            format('- [GitHub]({}) • ![CI]({})', p.urls.github, p.urls.github_ci),
            -- Extension link with badge (if exists)
            CASE WHEN p.urls.extension IS NOT NULL
                THEN format('- [Extension]({}) • ![Extension CI]({})', p.urls.extension, p.urls.extension_ci)
            END,
            -- Docs link with badge (if exists)
            CASE WHEN p.urls.docs IS NOT NULL
                THEN format('- [Docs]({}) • ![Docs]({})', p.urls.docs, p.urls.docs_badge)
            END
        ], E'\n')) AS block
    FROM projects_enriched p
),

-- Footer
footer AS (
    SELECT
        999 AS sort_order,
        db_paragraph(array_to_string([
            '---',
            '',
            '_Generated with DuckDB extensions: yaml, duck_block_utils, markdown_'
        ], E'\n')) AS block
)

-- Combine all blocks
SELECT sort_order, block FROM header
UNION ALL SELECT sort_order, block FROM category_headers
UNION ALL SELECT sort_order, block FROM project_content
UNION ALL SELECT sort_order, block FROM footer
ORDER BY sort_order;

-- Summary
SELECT count(*) AS total_blocks FROM page_blocks;
