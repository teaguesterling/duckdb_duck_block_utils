# ANSI Terminal Rendering

`PRAGMA duck_block_render` registers SQL macros that render duck_blocks to plain
UTF-8 text with ANSI SGR escape sequences — the same output format `glow`/`glamour`
produce — so documents and query results can be pretty-printed directly in a
terminal (`... | less -R`).

```sql
LOAD duck_block_utils;
PRAGMA duck_block_render;
```

The macros require the `json` extension (autoloaded in standard builds).

## Rendering documents

```sql
-- Render blocks built with the builder API. Rich text is structured inline
-- elements (duck_block_bold/duck_block_italic/duck_block_inline_code/duck_block_link), NOT markdown in content:
SELECT duck_blocks_render(
    duck_block_heading(1, 'Report')
    || duck_block_paragraph([duck_block_text('All systems '), duck_block_bold('nominal'), duck_block_text('.')])
);

-- Render a markdown file: read_markdown_blocks emits structured inlines, so the
-- formatting renders directly (requires the markdown extension >= the structured
-- release).
LOAD markdown;
SELECT duck_blocks_render(list(b ORDER BY b.element_order))
FROM (SELECT read_markdown_blocks('README.md') AS b);
```

## Pretty-printing query results

```sql
-- Any query -> ANSI table
SELECT rendered FROM duck_blocks_render_query('SELECT * FROM my_table LIMIT 10');

-- JSON array of objects -> table block (composable with other blocks)
SELECT duck_blocks_render([
    duck_block_json_to_table('[{"name": "Ada", "age": 36}, {"name": "Grace", "age": 85}]')
]);
```

Tip: from the shell, use list mode so the string renders raw:

```bash
duckdb -noheader -list -c "LOAD duck_block_utils; PRAGMA duck_block_render;
SELECT rendered FROM duck_blocks_render_query('SELECT * FROM range(5)');" | less -R
```

## Composing pages with embedded query results

`duck_blocks_page(title, blocks)` builds a titled page (an `h1` plus a list of block-lists,
assembled with sequential `element_order`), and `duck_blocks_query_table(q)` turns a SQL
query's results into an embeddable table block — so a whole dashboard-style
document is one expression:

```sql
SELECT duck_blocks_render(duck_blocks_page('Sales Report', [
    duck_block_paragraph([duck_block_bold('Rows'), duck_block_text(' by id:')]),
    duck_blocks_query_table('SELECT * FROM t ORDER BY id'),
    duck_block_paragraph('Aggregate:'),
    duck_blocks_query_table('SELECT count(*) AS n, round(avg(score), 2) AS avg_score FROM t')
]));
```

`duck_blocks_query_table` runs the query inside a scalar subquery so
`duck_block_json_to_table` receives a plain column rather than a subquery argument
(which would land inside a lambda — see the limitation below). `duck_blocks_table(json)`
does the same for a JSON array you already have, returning a one-element
`LIST(duck_block)` that composes with the other builders.

## Theme Palettes (Dark and Light)

The renderer supports two color palettes optimized for terminal backgrounds:

- **Dark Theme (Default)**: Vibrant, saturated 256-color palette (H1: Pink 219, H2: Lavender 141, H3: Sky Blue 75, Code: Salmon 203, Link: Sky Blue 75, Quote: Mint 115).
- **Light Theme**: High-contrast, deep jewel tones (H1: Ruby Red 125, H2: Deep Purple 55, H3: Navy Blue 25, Code: Crimson 160, Link: Royal Blue 27, Quote: Forest Green 28).

### Setting the Theme

1. **SQL Argument**:
   ```sql
   -- Explicit theme in C++ renderer:
   SELECT duck_blocks_render_ansi(blocks, 'light');
   SELECT duck_blocks_render_ansi(blocks, 80, 'light');

   -- Explicit theme in macro:
   SELECT duck_blocks_render(blocks, theme := 'light');
   SELECT duck_blocks_render(blocks, 80, 'light');
   ```

2. **Environment Auto-Detection**:
   When set to `'auto'` (the default), the renderer resolves the theme in order:
   - `$DUCK_BLOCK_THEME` (`light` / `dark`)
   - `$DUCKEYE_THEME` (`light` / `dark`)
   - `$COLORFGBG` (detects light background when background color index is 7 or 15)
   - Fallback: `dark`

## Macro reference

### C++ renderer (width-aware)

`duck_blocks_render_ansi(blocks[, width][, theme])` is a native scalar function that adds
what the pure-SQL macros cannot: word wrapping at a display width and ANSI color palettes. It measures
columns with utf8proc (SGR escapes are zero width, CJK/emoji are two), re-opens
active styles after each line break and continuation prefix (list indents,
quote bars), hard-breaks unbreakable words, and wraps table cells to fit the
width budget. Omit `width` (or pass `<= 0`) to auto-detect the terminal size
via `/dev/tty`, falling back to `$COLUMNS`, then 80 — so it does the right
thing even when stdout is piped to `less -R`.

`duck_block_terminal_width()` exposes the detected width directly.

`duck_blocks_render(blocks, width := 0, theme := 'auto')` (from the pragma) delegates to this renderer, so
macro users get wrapping and theme palettes for free.

## Macro reference

| Macro | Description |
|-------|-------------|
| `duck_blocks_render_ansi(blocks[, width][, theme])` | C++ renderer: `LIST(duck_block)` → wrapped ANSI document with dark/light themes |
| `duck_block_terminal_width()` | Detected terminal width (`/dev/tty`, `$COLUMNS`, default 80) |
| `duck_blocks_render(blocks, width := 0, theme := 'auto')` | Render `LIST(duck_block)` to a full ANSI document with word wrapping and theme support; each block's structured `kind='inline'` children are styled by `element_type` (bold/italic/code/link). Delegates to `duck_blocks_render_ansi` |
| `duck_block_render_one(element_type, content, attributes)` | Render a single block element |
| `duck_blocks_render_query(sql)` | Table macro: run `sql` via `query()` and render the result as an ANSI table (column `rendered`) |
| `duck_block_json_to_table(json)` | JSON array of objects → `table` duck_block (headers from the first object's keys) |
| `duck_blocks_table(json)` | JSON array of objects → `table` block as a one-element `LIST(duck_block)` (composes with the builders / `duck_blocks_assemble` / `duck_blocks_page`) |
| `duck_blocks_query_table(sql)` | Run `sql` and return its results as an embeddable `table` block list (safe to nest in `duck_blocks_page`) |
| `duck_blocks_page(title, blocks)` | Compose an `h1` title + a list of block-lists into one assembled `LIST(duck_block)` |
| `duck_block_ansi(code, s)` | Wrap `s` in an SGR escape (e.g. `duck_block_ansi('1;31', 'bold red')`) |
| `duck_block_ansi_inline(s)` | Returns `s` unchanged — content is literal; inline formatting comes from structured inline elements, not markdown syntax |
| `duck_block_render_table_json(j)` | `{"headers": [...], "rows": [[...]]}` JSON → aligned box table |
| `duck_block_render_list_json(j, ordered)` | JSON array of items → bulleted/numbered list |
| `duck_block_render_code(content, lang)` | Code block with dim gutter and language tag |
| `duck_block_ansi_pad(s, w)` | Space-pad to width (naive `length`-based) |

## Rich text is structural, not markdown

The renderer is **format-agnostic**: it styles a block's `kind='inline'` children
by `element_type` (`bold`, `italic`, `code`, `underline`, `strikethrough`,
`link`, `image`) and treats `content` as **literal** text. Markdown syntax in a
`content` string (`**bold**`, `[x](y)`) is *not* interpreted — it prints
verbatim. Build rich text with the inline builders (`duck_block_bold`, `duck_block_italic`,
`duck_block_link`, `duck_block_inline_code`), or get it from a producer that emits structured
inlines (`read_markdown_blocks` / `parse_markdown_to_duck_blocks`).

## Known limitations

- **Table/list content must be JSON-encoded** (the shape the builders and the
  markdown reader emit); inline styling inside list items / table cells is not
  yet walked.
- **Code blocks are not wrapped** (wrapping would corrupt meaning); long code
  lines overflow the width.
- **No syntax highlighting** inside code blocks.
- The `db_render_*` JSON macros (table/list/inline helpers) still measure with
  `length()` (codepoints); the C++ renderer is the width-correct path.
- **Macro arguments cannot contain subqueries** (`duck_block_json_to_table`,
  `duck_block_render_table_json`, `duck_block_render_list_json` use lambda expressions
  internally, and DuckDB rejects subqueries inside lambdas). Compute the JSON
  in a CTE and pass the column instead — or use `duck_blocks_query_table(sql)`, which
  wraps the subquery *around* the table builder so the query results embed
  directly.
