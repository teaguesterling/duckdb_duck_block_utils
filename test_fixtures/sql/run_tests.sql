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

-- ============================================================================
-- PANDOC JSON TESTS (tests duck_blocks_to_pandoc_ast)
-- ============================================================================

-- Test Pandoc block conversion
SELECT
    name,
    CASE
        WHEN duck_blocks_to_pandoc_ast(blocks) LIKE '%' || expected_pandoc_json || '%' THEN 'PASS'
        ELSE 'FAIL'
    END as result
FROM pandoc_block_expectations
LIMIT 5;  -- Sample test

-- ============================================================================
-- ROUND-TRIP TESTS
-- ============================================================================

-- Test that blocks survive a round-trip through Pandoc JSON
-- (Requires both directions of conversion to be implemented)
/*
SELECT
    name,
    blocks as original,
    pandoc_ast_to_duck_blocks(duck_blocks_to_pandoc_ast(blocks)) as round_tripped
FROM block_type_fixtures
WHERE name = 'paragraph_simple';
*/

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
