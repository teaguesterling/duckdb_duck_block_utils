-- duck_blocks_validation.sql
-- Standalone validation macros for duck_blocks
--
-- These macros can be used by any extension without depending on duck_block_utils.
-- Copy this file or include these definitions in your extension.
--
-- Spec: docs/duck_blocks_spec.md

--------------------------------------------------------------------------------
-- Type Constants (for reference - use string literals in macros)
--------------------------------------------------------------------------------
-- Block types: heading, paragraph, code, blockquote, list, hr, metadata, image, raw
-- Inline leaf types: text, space, softbreak, linebreak, code, math, raw
-- Inline container types: bold, italic, strikethrough, superscript, subscript,
--                         smallcaps, underline, link, image, quoted, span, cite, note

--------------------------------------------------------------------------------
-- Block Validation Macros
--------------------------------------------------------------------------------

-- Check if a block_type is a known type
CREATE OR REPLACE MACRO doc_block_type_is_known(block_type) AS (
    block_type IN (
        'heading', 'paragraph', 'code', 'blockquote', 'list',
        'hr', 'metadata', 'image', 'raw'
    )
    OR block_type LIKE 'x-%'  -- Custom extension types
);

-- Check if a doc_block has valid structure
CREATE OR REPLACE MACRO doc_block_is_valid(block) AS (
    block IS NOT NULL
    AND block.block_type IS NOT NULL
    AND block.block_order >= 0
    AND (
        block.block_type != 'heading'
        OR (block.level IS NOT NULL AND block.level BETWEEN 1 AND 6)
    )
);

-- Check if block content encoding is valid
CREATE OR REPLACE MACRO doc_block_encoding_is_valid(block) AS (
    block.encoding IS NULL
    OR block.encoding IN ('text', 'markdown', 'doc_inlines', 'yaml', 'json', 'html', 'latex')
);

--------------------------------------------------------------------------------
-- Inline Validation Macros
--------------------------------------------------------------------------------

-- Check if an inline_type is a known leaf type (has content, no children)
CREATE OR REPLACE MACRO doc_inline_is_leaf_type(inline_type) AS (
    inline_type IN ('text', 'code', 'math', 'raw', 'cite', 'note')
);

-- Check if an inline_type is a whitespace type (content should be empty)
CREATE OR REPLACE MACRO doc_inline_is_whitespace_type(inline_type) AS (
    inline_type IN ('space', 'softbreak', 'linebreak')
);

-- Check if an inline_type is a container type (may have children)
CREATE OR REPLACE MACRO doc_inline_is_container_type(inline_type) AS (
    inline_type IN (
        'bold', 'italic', 'strikethrough', 'superscript', 'subscript',
        'smallcaps', 'underline', 'link', 'image', 'quoted', 'span'
    )
);

-- Check if an inline_type is known
CREATE OR REPLACE MACRO doc_inline_type_is_known(inline_type) AS (
    doc_inline_is_leaf_type(inline_type)
    OR doc_inline_is_whitespace_type(inline_type)
    OR doc_inline_is_container_type(inline_type)
    OR inline_type LIKE 'x-%'  -- Custom extension types
);

-- Check if a doc_inline has valid basic structure
CREATE OR REPLACE MACRO doc_inline_is_valid(inline) AS (
    inline IS NOT NULL
    AND inline.inline_type IS NOT NULL
    AND inline.level >= 1
    AND inline.inline_order >= 0
);

-- Check if whitespace inline has correct empty content
CREATE OR REPLACE MACRO doc_inline_whitespace_is_valid(inline) AS (
    NOT doc_inline_is_whitespace_type(inline.inline_type)
    OR coalesce(inline.content, '') = ''
);

--------------------------------------------------------------------------------
-- Canonical Representation Checks
--------------------------------------------------------------------------------

-- Check if a container inline's content usage is canonical
-- Rule: content is populated IFF the node has a single text child
--
-- Parameters:
--   inline: the doc_inline struct
--   has_nested_children: boolean indicating if there are children at level+1
CREATE OR REPLACE MACRO doc_inline_content_is_canonical(inline, has_nested_children) AS (
    CASE
        -- Leaf types: always use content field
        WHEN doc_inline_is_leaf_type(inline.inline_type) THEN
            true
        -- Whitespace: content must be empty
        WHEN doc_inline_is_whitespace_type(inline.inline_type) THEN
            coalesce(inline.content, '') = ''
        -- Container types: content XOR nested children (not both, not neither)
        WHEN doc_inline_is_container_type(inline.inline_type) THEN
            (length(coalesce(inline.content, '')) > 0) != has_nested_children
        -- Unknown types: accept any
        ELSE true
    END
);

--------------------------------------------------------------------------------
-- List Validation Helpers
--------------------------------------------------------------------------------

-- Check if all blocks in a list are valid
CREATE OR REPLACE MACRO doc_blocks_all_valid(blocks) AS (
    list_bool_and([doc_block_is_valid(b) FOR b IN blocks])
);

-- Check if all inlines in a list are valid
CREATE OR REPLACE MACRO doc_inlines_all_valid(inlines) AS (
    list_bool_and([doc_inline_is_valid(i) FOR i IN inlines])
);

-- Check if inline levels form valid nesting (each child level = parent level + 1)
-- This is a simplified check - full validation requires tracking parent-child relationships
CREATE OR REPLACE MACRO doc_inlines_levels_valid(inlines) AS (
    list_bool_and([i.level >= 1 FOR i IN inlines])
);

--------------------------------------------------------------------------------
-- Diagnostic Helpers
--------------------------------------------------------------------------------

-- Get validation errors for a block (returns NULL if valid)
CREATE OR REPLACE MACRO doc_block_validation_error(block) AS (
    CASE
        WHEN block IS NULL THEN 'block is NULL'
        WHEN block.block_type IS NULL THEN 'block_type is NULL'
        WHEN block.block_order < 0 THEN 'block_order is negative'
        WHEN block.block_type = 'heading' AND block.level IS NULL THEN 'heading missing level'
        WHEN block.block_type = 'heading' AND block.level NOT BETWEEN 1 AND 6 THEN 'heading level out of range'
        ELSE NULL
    END
);

-- Get validation errors for an inline (returns NULL if valid)
CREATE OR REPLACE MACRO doc_inline_validation_error(inline) AS (
    CASE
        WHEN inline IS NULL THEN 'inline is NULL'
        WHEN inline.inline_type IS NULL THEN 'inline_type is NULL'
        WHEN inline.level < 1 THEN 'level must be >= 1'
        WHEN inline.inline_order < 0 THEN 'inline_order is negative'
        WHEN doc_inline_is_whitespace_type(inline.inline_type)
             AND coalesce(inline.content, '') != '' THEN 'whitespace type has non-empty content'
        ELSE NULL
    END
);
