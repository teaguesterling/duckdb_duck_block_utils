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
-- Render blocks built with the builder API
SELECT db_render_blocks(
    db_heading(1, 'Report')
    || db_paragraph('All systems **nominal**.')
);

-- Render a markdown file (requires the markdown extension)
LOAD markdown;
SELECT db_render_blocks(list(b ORDER BY b.element_order))
FROM (SELECT read_markdown_blocks('README.md') AS b);
```

## Pretty-printing query results

```sql
-- Any query -> ANSI table
SELECT rendered FROM db_render_query('SELECT * FROM my_table LIMIT 10');

-- JSON array of objects -> table block (composable with other blocks)
SELECT db_render_blocks([
    db_json_to_table_block('[{"name": "Ada", "age": 36}, {"name": "Grace", "age": 85}]')
]);
```

Tip: from the shell, use list mode so the string renders raw:

```bash
duckdb -noheader -list -c "LOAD duck_block_utils; PRAGMA duck_block_render;
SELECT rendered FROM db_render_query('SELECT * FROM range(5)');" | less -R
```

## Macro reference

### C++ renderer (width-aware)

`db_blocks_render_ansi(blocks[, width])` is a native scalar function that adds
what the pure-SQL macros cannot: word wrapping at a display width. It measures
columns with utf8proc (SGR escapes are zero width, CJK/emoji are two), re-opens
active styles after each line break and continuation prefix (list indents,
quote bars), hard-breaks unbreakable words, and wraps table cells to fit the
width budget. Omit `width` (or pass `<= 0`) to auto-detect the terminal size
via `/dev/tty`, falling back to `$COLUMNS`, then 80 — so it does the right
thing even when stdout is piped to `less -R`.

`db_terminal_width()` exposes the detected width directly.

`db_render_blocks(blocks)` (from the pragma) delegates to this renderer, so
macro users get wrapping for free.

## Macro reference

| Macro | Description |
|-------|-------------|
| `db_blocks_render_ansi(blocks[, width])` | C++ renderer: `LIST(duck_block)` → wrapped ANSI document |
| `db_terminal_width()` | Detected terminal width (`/dev/tty`, `$COLUMNS`, default 80) |
| `db_render_blocks(blocks)` | Render `LIST(duck_block)` to a full ANSI document (inline-kind elements are skipped; delegates to `db_blocks_render_ansi`) |
| `db_render_block(element_type, content, attributes)` | Render a single block element |
| `db_render_query(sql)` | Table macro: run `sql` via `query()` and render the result as an ANSI table (column `rendered`) |
| `db_json_to_table_block(json)` | JSON array of objects → `table` duck_block (headers from the first object's keys) |
| `db_ansi(code, s)` | Wrap `s` in an SGR escape (e.g. `db_ansi('1;31', 'bold red')`) |
| `db_ansi_inline(s)` | Inline markdown (`**bold**`, `*italic*`, `` `code` ``, `[text](url)`) → ANSI |
| `db_render_table_json(j)` | `{"headers": [...], "rows": [[...]]}` JSON → aligned box table |
| `db_render_list_json(j, ordered)` | JSON array of items → bulleted/numbered list |
| `db_render_code(content, lang)` | Code block with dim gutter and language tag |
| `db_ansi_pad(s, w)` | Space-pad to width (naive `length`-based) |

## Known limitations

- **Inline styling parses raw markdown text** (`**`, `*`, `` ` ``, `[](...)`)
  from the `content` field; inline duck_block children (containers with
  `content=''`) are not walked.
- **Table/list content must be JSON-encoded** (the shape the markdown reader
  emits).
- **Code blocks are not wrapped** (wrapping would corrupt meaning); long code
  lines overflow the width.
- **No syntax highlighting** inside code blocks.
- **No theming yet** — the palette is fixed (glamour-style JSON themes are a
  natural follow-up).
- The `db_render_*` JSON macros (table/list/inline helpers) still measure with
  `length()` (codepoints); the C++ renderer is the width-correct path.
