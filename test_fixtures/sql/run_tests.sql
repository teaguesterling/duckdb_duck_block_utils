-- Test runner for duck_block fixtures
-- This file demonstrates how to use the fixtures to test markdown/html rendering
--
-- Prerequisites:
--   - duck_block_utils extension loaded
--   - Fixture tables loaded (run load_fixtures.sql first)
--   - For markdown tests: markdown extension with duck_block support
--   - For HTML tests: webbed extension with duck_block support

-- ============================================================================
-- MARKDOWN TESTS (requires duckdb_markdown extension)
-- ============================================================================

-- Test block types against expected markdown output
-- Uncomment when testing markdown extension:
/*
SELECT
    name,
    CASE
        WHEN db_blocks_to_markdown(blocks) = expected_markdown THEN 'PASS'
        ELSE 'FAIL'
    END as result,
    CASE
        WHEN db_blocks_to_markdown(blocks) != expected_markdown THEN
            'Expected: ' || expected_markdown || ' | Got: ' || db_blocks_to_markdown(blocks)
        ELSE NULL
    END as details
FROM block_type_fixtures
WHERE expected_markdown IS NOT NULL;
*/

-- ============================================================================
-- HTML TESTS (requires duckdb_webbed extension)
-- ============================================================================

-- Test block types against expected HTML output
-- Uncomment when testing webbed extension:
/*
SELECT
    name,
    CASE
        WHEN db_blocks_to_html(blocks) = expected_html THEN 'PASS'
        ELSE 'FAIL'
    END as result,
    CASE
        WHEN db_blocks_to_html(blocks) != expected_html THEN
            'Expected: ' || expected_html || ' | Got: ' || db_blocks_to_html(blocks)
        ELSE NULL
    END as details
FROM block_type_fixtures
WHERE expected_html IS NOT NULL;
*/

-- Pandoc JSON and round-trip tests moved to panduck with the Pandoc converter;
-- pandoc_*_expectations (04_pandoc_expectations.sql) stay here as data.

-- ============================================================================
-- VALIDATION TESTS
-- ============================================================================

-- Check fixture validation (note: element_order duplicates are expected when
-- combining multiple builder function calls, so validation may show warnings)
SELECT
    name,
    CASE
        WHEN db_blocks_validate(blocks).valid THEN 'PASS'
        ELSE 'WARN: ' || db_blocks_validate(blocks).errors[1].message
    END as validation_result
FROM all_fixtures
LIMIT 10;

-- ============================================================================
-- TEXT EXTRACTION TESTS
-- ============================================================================

-- Test plain text extraction from fixtures
SELECT
    name,
    db_blocks_to_text(blocks) as extracted_text
FROM all_fixtures
WHERE name LIKE 'paragraph%'
LIMIT 5;
