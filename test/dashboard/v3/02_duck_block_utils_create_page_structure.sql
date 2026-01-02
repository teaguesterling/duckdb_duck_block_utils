-- 02_duck_block_utils_create_page_structure.sql
-- Build db_blocks from project data using templates
--
-- Run: ./build/release/duckdb /tmp/dashboard.duckdb -f test/dashboard/v3/02_duck_block_utils_create_page_structure.sql

------------------------------------------------------------
-- Templates
------------------------------------------------------------

SET VARIABLE project_template TO $$
### {0}

{1}

{2}$$;

SET VARIABLE link_github TO $$- [GitHub]({0}) • ![CI]({1})$$;
SET VARIABLE link_extension TO $$- [Extension]({0}) • ![Extension CI]({1})$$;
SET VARIABLE link_docs TO $$- [Docs]({0}) • ![Docs]({1})$$;

SET VARIABLE footer_template TO $$---

_Generated with DuckDB extensions: yaml, duck_block_utils, markdown_$$;

------------------------------------------------------------
-- URL Patterns
------------------------------------------------------------

SET VARIABLE url_github TO 'https://github.com/{0}/{1}';
SET VARIABLE url_github_ci TO 'https://github.com/{0}/{1}/actions/workflows/ci.yml/badge.svg';
SET VARIABLE url_extension_ci TO 'https://github.com/duckdb/community-extensions/actions/workflows/{0}.yml/badge.svg';
SET VARIABLE url_rtd_badge TO 'https://readthedocs.org/projects/{0}/badge/';

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
        -- GitHub URLs
        format(getvariable('url_github'), o.default_owner, p.name) AS github_url,
        format(getvariable('url_github_ci'), o.default_owner, p.name) AS github_ci_url,
        -- Extension URLs
        format(getvariable('url_extension_ci'), coalesce(p.extension_name, p.name)) AS ext_ci_url,
        -- Docs URLs
        format(getvariable('url_rtd_badge'), replace(p.name, '_', '-')) AS docs_badge_url
    FROM projects p
    JOIN categories c ON p.category = c.name
    CROSS JOIN owner o
),

-- Build link lines for each project
projects_with_links AS (
    SELECT
        *,
        array_to_string(
            list_filter([
                format(getvariable('link_github'), github_url, github_ci_url),
                CASE WHEN extension_page IS NOT NULL
                    THEN format(getvariable('link_extension'), extension_page, ext_ci_url)
                END,
                CASE WHEN docs IS NOT NULL
                    THEN format(getvariable('link_docs'), docs, docs_badge_url)
                END
            ], x -> x IS NOT NULL),
            E'\n'
        ) AS links_block
    FROM projects_with_urls
),

-- Header
header AS (
    SELECT * FROM (VALUES
        (1, db_heading('DuckDB Extensions Dashboard', 1)),
        (2, db_paragraph(format(
            'A collection of DuckDB extensions and related projects by {}.',
            (SELECT default_owner FROM owner)
        )))
    ) AS t(sort_order, block)
),

-- Category headers
category_headers AS (
    SELECT c.sort_order, db_heading(c.name, 2) AS block
    FROM categories c
    WHERE c.name IN (SELECT DISTINCT category FROM projects)
),

-- Project blocks
project_blocks AS (
    SELECT
        cat_order + row_number() OVER (PARTITION BY category ORDER BY name) AS sort_order,
        db_paragraph(format(
            getvariable('project_template'),
            name,
            coalesce(description, '_No description_'),
            links_block
        )) AS block
    FROM projects_with_links
),

-- Footer
footer AS (
    SELECT 999 AS sort_order,
           db_paragraph(getvariable('footer_template')) AS block
)

-- Combine
SELECT sort_order, block FROM header
UNION ALL SELECT * FROM category_headers
UNION ALL SELECT * FROM project_blocks
UNION ALL SELECT * FROM footer
ORDER BY sort_order;

SELECT count(*) AS total_blocks FROM page_blocks;
