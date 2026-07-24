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

| Macro | Description |
|-------|-------------|
| `db_render_blocks(blocks)` | Render `LIST(duck_block)` to a full ANSI document (inline-kind elements are skipped) |
| `db_render_block(element_type, content, attributes)` | Render a single block element |
| `db_render_query(sql)` | Table macro: run `sql` via `query()` and render the result as an ANSI table (column `rendered`) |
| `db_json_to_table_block(json)` | JSON array of objects → `table` duck_block (headers from the first object's keys) |
| `db_ansi(code, s)` | Wrap `s` in an SGR escape (e.g. `db_ansi('1;31', 'bold red')`) |
| `db_ansi_inline(s)` | Inline markdown (`**bold**`, `*italic*`, `` `code` ``, `[text](url)`) → ANSI |
| `db_render_table_json(j)` | `{"headers": [...], "rows": [[...]]}` JSON → aligned box table |
| `db_render_list_json(j, ordered)` | JSON array of items → bulleted/numbered list |
| `db_render_code(content, lang)` | Code block with dim gutter and language tag |
| `db_ansi_pad(s, w)` | Space-pad to width (naive `length`-based) |

## Known limitations (v1, pure-macro implementation)

- **No word wrapping** — long lines are left to the terminal to wrap, which can
  break gutter/list alignment. Proper wrapping needs escape-aware display-width
  math (utf8proc) in a future C++ `db_blocks_render_ansi` function.
- **Inline styling is regex-based** — nested markup (e.g. `` **bold `code`** ``)
  and inline duck_block children (containers with `content=''`) are not walked;
  paragraphs are styled from their raw markdown text as produced by
  `read_markdown_blocks`.
- **Table/list content must be JSON-encoded** (the shape the markdown reader
  emits); nested lists render flat.
- **No syntax highlighting** inside code blocks.
- Column alignment uses `length()` (codepoints), so double-width CJK/emoji cells
  will misalign.
