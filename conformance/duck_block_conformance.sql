-- duck_block conformance checks, PURE SQL -- no duck_block_utils required.
--
-- WHY THIS FILE EXISTS. duck_blocks_validate() ships inside an extension built for a
-- specific DuckDB version, and DuckDB matches extension ABI by EXACT version string.
-- An extension that vendors its own DuckDB submodule pinned off-release reports e.g.
-- 'v1.5.5-dev154' and CANNOT LOAD duck_block_utils by any route -- not INSTALL, not
-- LOAD '<path>', and publishing would not change it.
--
-- Raised by the webbed session, whose metadata blocks carried a NULL level for three
-- major spec versions with nothing in a position to object: the check that would have
-- caught it was one they structurally could not run. If markdown and panduck vendor
-- duckdb too, none of the three consuming extensions could run the validator, which
-- explains a great deal about how long these defects survived.
--
-- So conformance for that class of consumer has to be SHAPE-BASED, not
-- extension-based. Copy this file; it needs nothing but DuckDB.
--
-- It is kept honest by test/check_conformance_macro.py, which runs both these macros
-- and the real duck_blocks_validate() over the same corpus and FAILS if they disagree.
-- Two copies of one rule checked by the same party cannot detect their own
-- disagreement -- this repo shipped exactly that defect in an image's alt text -- so
-- the two implementations are compared rather than merely both maintained.

-- Per-element shape. Covers 4 of the 6 things duck_blocks_validate reports.
CREATE OR REPLACE MACRO duck_block_is_valid(elem) AS (
    elem.kind IN ('block', 'inline', 'value')
    AND elem.element_type IS NOT NULL
    AND elem.encoding IN ('text', 'json', 'yaml', 'html', 'xml', 'latex', 'markdown')
    AND elem.level IS NOT NULL
    AND elem.level >= 1
    AND elem.element_order IS NOT NULL
    AND elem.element_order >= 0
);

-- Document shape. The other 2, and they are the ones a per-element check CANNOT see:
--
--   duplicate element_order  needs the whole list
--   level jumping by >1      needs ADJACENCY. `level` is structural depth in a
--                            depth-first ordering, so a document descends one at a
--                            time; a jump means the element's parent is missing.
--                            This is the rule whose absence caused a year of drift
--                            across four extensions, and it is precisely the one a
--                            per-element macro cannot express -- so a consumer given
--                            only the element macro is unguarded against the defect
--                            most likely to bite them.
CREATE OR REPLACE MACRO duck_blocks_are_valid(blocks) AS (
    -- every element individually
    NOT EXISTS (SELECT 1 FROM unnest(blocks) AS t(e) WHERE NOT duck_block_is_valid(e))
    -- element_order unique
    AND (SELECT count(DISTINCT e.element_order) = count(*) FROM unnest(blocks) AS t(e))
    -- depth-first ordering descends one level at a time
    AND NOT EXISTS (
        SELECT 1 FROM (
            SELECT e.level AS lvl,
                   lag(e.level) OVER (ORDER BY i) AS prev
            FROM unnest(blocks) WITH ORDINALITY AS t(e, i)
        ) WHERE prev IS NOT NULL AND lvl > prev + 1
    )
);
