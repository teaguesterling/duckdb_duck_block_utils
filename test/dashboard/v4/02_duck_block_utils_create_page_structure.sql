-- 02_duck_block_utils_create_page_structure.sql
-- Build doc_blocks from project data using duck_block_utils builders
--
-- v4: Uses duck_block_builders completely - no markdown syntax in templates
--
-- Run: ./build/release/duckdb /tmp/dashboard.duckdb -f test/dashboard/v4/02_duck_block_utils_create_page_structure.sql

------------------------------------------------------------
-- URL Patterns
------------------------------------------------------------

SET VARIABLE url_github TO 'https://github.com/{0}/{1}';
SET VARIABLE url_github_ci TO 'https://github.com/{0}/{1}/actions/workflows/ci.yml/badge.svg';
SET VARIABLE url_extension_ci TO 'https://github.com/duckdb/community-extensions/actions/workflows/{0}.yml/badge.svg';
SET VARIABLE url_rtd_badge TO 'https://readthedocs.org/projects/{0}/badge/';

------------------------------------------------------------
-- Helper: Render inline elements to markdown text
------------------------------------------------------------

-- Helper macro to render a link with optional badge to markdown
CREATE OR REPLACE MACRO render_link_with_badge(label, url, badge_url, badge_alt) AS
    pandoc_inlines_to_text(
        doc_inlines_to_pandoc([
            doc_link(label, url),
            doc_text(' '),
            doc_inline_image(badge_url, badge_alt)
        ]),
        'markdown'
    );

------------------------------------------------------------
-- Build page
------------------------------------------------------------

CREATE OR REPLACE TABLE page_blocks AS
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

-- Build URLs for each project
projects_with_urls AS (
    SELECT
        p.*,
        c.sort_order AS cat_order,
        coalesce(p.owner, o.default_owner) AS effective_owner,
        -- GitHub URLs
        format(getvariable('url_github'), coalesce(p.owner, o.default_owner), p.name) AS github_url,
        format(getvariable('url_github_ci'), coalesce(p.owner, o.default_owner), p.name) AS github_ci_url,
        -- Extension URLs
        format(getvariable('url_extension_ci'), coalesce(p.extension_name, p.name)) AS ext_ci_url,
        -- Docs URLs
        format(getvariable('url_rtd_badge'), replace(p.name, '_', '-')) AS docs_badge_url
    FROM projects p
    JOIN categories c ON p.category = c.name
    CROSS JOIN owner o
),

-- Build link list items using inline builders
projects_with_links AS (
    SELECT
        *,
        -- GitHub link with CI badge - always present
        render_link_with_badge('GitHub', github_url, github_ci_url, 'CI') AS github_link,
        -- Extension link with badge - optional
        CASE WHEN extension_page IS NOT NULL
            THEN render_link_with_badge('Extension', extension_page, ext_ci_url, 'Extension CI')
        END AS extension_link,
        -- Docs link with badge - optional
        CASE WHEN docs IS NOT NULL
            THEN render_link_with_badge('Docs', docs, docs_badge_url, 'Docs')
        END AS docs_link
    FROM projects_with_urls
),

-- Header blocks using doc_heading and doc_paragraph
header AS (
    SELECT * FROM (VALUES
        (1, doc_heading('DuckDB Extensions Dashboard', 1)),
        (2, doc_paragraph(format(
            'A collection of DuckDB extensions and related projects by %s.',
            (SELECT default_owner FROM owner)
        )))
    ) AS t(sort_order, block)
),

-- Category headers using doc_heading
category_headers AS (
    SELECT c.sort_order, doc_heading(c.name, 2) AS block
    FROM categories c
    WHERE c.name IN (SELECT DISTINCT category FROM projects)
),

-- Project blocks: heading + description + links list
project_blocks AS (
    SELECT
        cat_order + row_number() OVER (PARTITION BY category ORDER BY name) AS sort_order,
        -- Project name as h3 heading
        doc_heading(name, 3) AS heading_block,
        -- Description as paragraph
        doc_paragraph(coalesce(description, '_No description_')) AS desc_block,
        -- Links as bulleted list using doc_list_block
        doc_list_block(
            list_filter([github_link, extension_link, docs_link], x -> x IS NOT NULL),
            false  -- unordered list
        ) AS links_block
    FROM projects_with_links
),

-- Flatten project blocks (each project produces 3 blocks)
project_blocks_flat AS (
    SELECT sort_order, heading_block AS block FROM project_blocks
    UNION ALL
    SELECT sort_order + 0.1, desc_block FROM project_blocks
    UNION ALL
    SELECT sort_order + 0.2, links_block FROM project_blocks
),

-- Footer using doc_hr and doc_paragraph
footer AS (
    SELECT * FROM (VALUES
        (998, doc_hr()),
        (999, doc_paragraph('Generated with DuckDB extensions: yaml, duck_block_utils, markdown'))
    ) AS t(sort_order, block)
)

-- Combine all blocks
SELECT sort_order, block FROM header
UNION ALL SELECT * FROM category_headers
UNION ALL SELECT * FROM project_blocks_flat
UNION ALL SELECT * FROM footer
ORDER BY sort_order;

SELECT count(*) AS total_blocks FROM page_blocks;
