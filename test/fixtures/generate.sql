-- generate.sql — regenerate the spec-conformant duck_block fixtures.
--   duckdb < test/fixtures/generate.sql        (with duck_block_utils loaded/built-in)
--
-- These fixtures are the canonical structured representation (rich text as
-- kind='inline' children with literal content + encoding='text'), NOT markdown
-- syntax embedded in content. They exist so the renderer can be tested against
-- the spec's model rather than against one downstream format's syntax.

CREATE OR REPLACE TABLE doc AS
SELECT unnest(db_assemble([
    db_heading(1, 'Duck Blocks Fixture'),
    db_paragraph([
        db_text('This paragraph has '), db_bold('bold'), db_text(', '),
        db_italic('italic'), db_text(', '), db_inline_code('code'),
        db_text(', and a '), db_link('https://duckdb.org', 'link'), db_text('.')
    ]),
    db_heading(2, 'A list'),
    db_list_block(['first item', 'second item', 'third item']),
    db_heading(2, 'Code'),
    db_code('sql', 'SELECT 42;'),
    db_heading(2, 'Quote'),
    db_blockquote('Structured inlines, not markdown syntax.')
])) AS block;

COPY (SELECT block.* FROM doc) TO 'test/fixtures/sample_document.parquet' (FORMAT parquet);
COPY (SELECT block.* FROM doc) TO 'test/fixtures/sample_document.json'    (FORMAT json, ARRAY true);
