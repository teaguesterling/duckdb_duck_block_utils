# Terminal Graphics — Notes & Roadmap

A design note on where `duck_block_utils` rendering sits today, how it composes
with the wider ecosystem, and what a *bitmap* rendering tier would take. This is
exploratory — nothing here is committed to a release.

## Where we are (v1.3.0)

`PRAGMA duck_block_render` + `db_blocks_render_ansi` render structured documents
to **text cells with ANSI SGR color**: headings, paragraphs with inline markdown,
box-drawing tables, lists, code gutters, blockquotes — width-aware, utf8proc-measured.
That's the *text-cell tier*: everything is a character in the terminal grid.

## The community landscape (surveyed 2026-07)

Across the 288 community extensions, **none render bitmap graphics to the
terminal.** Visualization splits two ways:

- **Text-cell viz** — [`textplot`](https://community-extensions.duckdb.org/extensions/textplot.html):
  ASCII/Unicode bar charts, sparklines, density strips. Same tier as our ANSI
  renderer, and *complementary* (see below).
- **Browser/HTML viz** — `quackplot` (deck.gl/MapLibre), `miniplot` (Plotly-like),
  `ggsql` and the stats extension's `VISUALIZE → Vega-Lite`, a browser BI
  `dashboard`. These leave the terminal entirely.

Raster-*data* extensions (`raster`, `raquet`, `zarr`) and `pdf` (renders pages to
image *files*) don't paint the terminal either. So `duck_block_utils` already
holds the text-cell niche for *structured documents* uniquely, and
**bitmap-to-terminal is an open gap** in the ecosystem.

## Composing with textplot (works today)

Because the renderer embeds plain strings, `textplot`'s string outputs drop
straight into `duck_block` content — no new code needed. Real output:

```sql
LOAD textplot; LOAD duck_block_utils;
PRAGMA duck_block_render;

WITH m(mon, cnt) AS (VALUES (1,5),(2,8),(3,3),(4,12),(5,9),(6,15))
SELECT db_render_blocks(db_assemble([
    db_heading(1, 'Monthly Activity'),
    db_paragraph('Trend: ' || (SELECT tp_sparkline(list(cnt ORDER BY mon)) FROM m)),
    db_query_table('SELECT mon, cnt, bar(cnt, 0, 15, 12) AS chart
                    FROM (VALUES (1,5),(2,8),(3,3),(4,12),(5,9),(6,15)) t(mon,cnt)
                    ORDER BY mon')
]));
```

```
▍ Monthly Activity

Trend: ▁▁▁▁▃▃▃ ▆▆▆▆▄▄▄███

mon │ cnt │ chart
────┼─────┼─────────────
1   │ 5   │ ████
2   │ 8   │ ██████▍
3   │ 3   │ ██▍
4   │ 12  │ █████████▌
5   │ 9   │ ███████▏
6   │ 15  │ ████████████
```

Useful `textplot` primitives: `tp_sparkline(values)`, `tp_bar(fraction)`,
`bar(x, min, max, width)`, `tp_density(values)`, and the `histogram*` family.
A runnable version lives in [`examples/textplot_dashboard.sql`](../examples/textplot_dashboard.sql).

## Can terminals render real bitmaps? Yes — but no single standard

| Protocol | Mechanism | Notable terminals |
|----------|-----------|-------------------|
| **Sixel** | DEC-era bitmap encoded as text escapes | xterm (sixel build), foot, WezTerm, mlterm, contour, mintty, Konsole, recent Windows Terminal |
| **Kitty graphics** | Most capable: 24-bit, alpha, z-layering, animation, image IDs, escape/file/shmem transport | kitty, Ghostty, WezTerm (partial), recent Konsole |
| **iTerm2 inline images** | `OSC 1337;File=…` + base64 | iTerm2, WezTerm (what `imgcat` targets) |
| **Unicode-cell "pixels"** *(fallback, not true bitmap)* | truecolor + half/quadrant/sextant blocks (`▀▄`) or **Braille** 2×4 dots | any truecolor terminal |

Caveats: capability detection is heuristic (DA/XTVERSION queries, `$TERM`);
`tmux`/`screen` passthrough is fussy (tmux gained sixel only in 3.4+); it's
bandwidth-heavy over SSH.

## What a bitmap tier would mean here

The `duck_block` **`image`** element currently renders as alt-text/link in ANSI.
A bitmap tier would emit sixel/kitty/iterm escape strings for image content on a
capable terminal, falling back to unicode-cell approximation, then alt-text.

Mechanically it's the same shape as today — the renderer already returns escape
sequences as `VARCHAR`; sixel/kitty is just a bigger payload. The real costs:

- **Image decoding.** Turning a PNG/JPEG into pixels needs libpng/jpeg/webp — a
  **vcpkg dependency** that breaks the current pure-SQL + utf8proc-only footprint.
  Alternatives: accept pre-decoded RGBA arrays, or delegate decode to a sibling
  extension.
- **Capability detection.** Query the terminal, read env/`$TERM`, default
  *conservatively* to the fallback tier when unknown.
- **Coverage.** sixel/kitty/iterm cover ~half the terminal ecosystem; the
  unicode-cell fallback works everywhere but is low-res.
- **Multiplexers & pipes** complicate passthrough.

### Suggested phasing

1. ✅ **Text-cell ANSI** (shipped) — the renderer we have.
2. **Unicode-cell image approximation** (half-block / Braille) — universal, no
   escape-protocol detection, but still needs image *decode*.
3. **True bitmap** (sixel/kitty/iterm) — decode deps + capability detection.

### Open questions

- Where does image decoding live — a vcpkg dep, a pre-decoded RGBA input, or a
  sibling extension?
- Keep `db_render_blocks` pure-text and add a separate capability-gated
  `db_blocks_render_graphics(...)`, so text output stays dependency-free?

**Recommendation:** not near-term. The text-cell tier + `textplot` composition
already covers a lot of ground dependency-free. Revisit the bitmap tier only if
there's real demand; if so, phase 2 (unicode-cell) is the pragmatic middle.
