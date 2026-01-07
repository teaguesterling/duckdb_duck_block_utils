-- Master loader for duck_block test fixtures
-- Run this script to load all test fixture tables
--
-- Usage:
--   .read path/to/test_fixtures/sql/load_fixtures.sql
--
-- After loading, you'll have access to these tables:
--   - block_type_fixtures    (individual block types with markdown/html expectations)
--   - list_fixtures          (list structures with nesting)
--   - document_fixtures      (complete multi-block documents)
--   - pandoc_block_expectations    (Pandoc JSON for blocks)
--   - pandoc_inline_expectations   (Pandoc JSON for inlines)
--   - pandoc_list_expectations     (Pandoc JSON for lists)

-- Ensure duck_block_utils is loaded
LOAD duck_block_utils;

-- Load all fixture files
.read 01_block_types.sql
.read 02_lists.sql
.read 03_documents.sql
.read 04_pandoc_expectations.sql

-- Create a unified view of all fixtures
CREATE OR REPLACE VIEW all_fixtures AS
SELECT 'block_type' as category, name, blocks, expected_markdown, expected_html
FROM block_type_fixtures
UNION ALL
SELECT 'list' as category, name, blocks, expected_markdown, expected_html
FROM list_fixtures
UNION ALL
SELECT 'document' as category, name, blocks, expected_markdown, expected_html
FROM document_fixtures;

-- Summary of loaded fixtures
SELECT
    'Loaded test fixtures:' as status,
    (SELECT count(*) FROM block_type_fixtures) as block_types,
    (SELECT count(*) FROM list_fixtures) as lists,
    (SELECT count(*) FROM document_fixtures) as documents,
    (SELECT count(*) FROM pandoc_block_expectations) as pandoc_blocks,
    (SELECT count(*) FROM pandoc_inline_expectations) as pandoc_inlines,
    (SELECT count(*) FROM pandoc_list_expectations) as pandoc_lists;
