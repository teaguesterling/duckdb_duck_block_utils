-- textplot_dashboard.sql
-- Using textplot (community) on its own and composed into duck_block_utils pages.
-- Run raw (ANSI escapes render) with:
--   duckdb -noheader -list < examples/textplot_dashboard.sql | less -R
--
-- Requires: textplot, duck_block_utils >= v1.3.0 (terminal rendering landed in
-- v1.3.0), duckdb >= v1.5.x. Until v1.3.0 is published to community, LOAD a
-- local build instead of INSTALL ... FROM community for duck_block_utils.

INSTALL textplot FROM community;
INSTALL duck_block_utils FROM community;
LOAD textplot;
LOAD duck_block_utils;
PRAGMA duck_block_render;

.mode line

-- ── textplot primitives ────────────────────────────────────────────────────

-- Sparkline from a list of values
SELECT tp_sparkline([1,3,2,7,5,9,4,8,6,10]) AS sparkline;

-- Fractional bar (0..1) and a value scaled between min/max at a given width
SELECT tp_bar(0.65) AS tp_bar, bar(65, 0, 100, 20) AS bar_scaled;

-- Density strip
SELECT tp_density([1,1,2,2,2,3,3,4,5,5,5,5,6,7,8,8,9,10]) AS density;

-- Per-group horizontal bar chart from a table
.mode duckbox
WITH s(cat, n) AS (VALUES ('alpha',12),('beta',30),('gamma',7),('delta',22))
SELECT cat, n, bar(n, 0, 30, 24) AS chart FROM s ORDER BY n DESC;

-- ── Composed into a duck_block_utils page ──────────────────────────────────
-- textplot returns strings, so they drop straight into block content; bar()
-- also works as a column inside db_query_table.

.mode line
WITH m(mon, cnt) AS (VALUES (1,5),(2,8),(3,3),(4,12),(5,9),(6,15))
SELECT db_render_blocks(db_assemble([
    db_heading(1, 'Monthly Activity'),
    db_paragraph('Trend: ' || (SELECT tp_sparkline(list(cnt ORDER BY mon)) FROM m)),
    db_query_table('SELECT mon, cnt, bar(cnt, 0, 15, 12) AS chart
                    FROM (VALUES (1,5),(2,8),(3,3),(4,12),(5,9),(6,15)) t(mon,cnt)
                    ORDER BY mon')
])) AS dashboard;
