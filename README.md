# DuckDB Document Block Utilities

A DuckDB extension providing format-agnostic utilities for working with document elements.

## Overview

This extension complements format-specific document extensions (markdown, HTML, XML, YAML) by providing common utilities that work with any document represented using the [Duck Blocks Specification](docs/duck_blocks_spec.md).

## Features

- **Unified duck_block type**: One type for block, inline and value elements, told apart by the `kind` discriminator
- **Block builders**: Declarative document construction from SQL queries
- **Inline builders**: Rich text formatting with links, bold, italic, code, and more
- **Block manipulation**: Filter, transform, merge, and reorder elements
- **Content extraction**: Extract plain text, headings, and generate TOCs
- **Validation**: Check schema compliance and lint for common issues
- **Pandoc AST conversion**: Bidirectional JSON AST ↔ duck_blocks (no Pandoc required) — complete coverage of pandoc-types 1.23, including document metadata, verified against a real pandoc by `test/pandoc/check_pandoc_alignment.py`
- **Conversion helpers**: Normalize blocks and track provenance
- **ANSI terminal rendering**: `PRAGMA duck_block_render` — render documents and query results as styled terminal output, glow-style ([docs](docs/rendering.md))
- **Page composition & query tables**: `duck_blocks_page`, `duck_blocks_query_table`, `duck_blocks_table` — assemble dashboards that embed live query results as rendered tables
- **Vocabulary introspection**: `duck_block_kind_names`, `duck_block_type_names`, `duck_block_encoding_names`, `duck_block_spec_version` — so sibling extensions can *assert* they agree with the vocabulary rather than mirroring a header
> **DEPRECATED — the Pandoc converter moves to [panduck](https://github.com/teaguesterling/duckdb_panduck).**
> `pandoc_ast_to_blocks`, `duck_blocks_to_pandoc_blocks`, `duck_blocks_to_pandoc_ast`,
> `read_pandoc_ast`, `write_pandoc_ast`, `pandoc_inlines_to_db_inlines`,
> `duck_blocks_inlines_to_pandoc` and `pandoc_inlines_to_text` all still work and now
> print a one-time notice to stderr. panduck owns format knowledge; this library is
> utilities over the `duck_block` vocabulary and the spec it validates. They will be
> removed in a later release, once panduck's converter is installable — not before,
> because deleting them first would leave a window with the converter in neither.

- **Document queries over blocks**: `duck_blocks_toc_rows`, `duck_blocks_get_section`, `duck_blocks_sections_like`, and on the separate page axis `duck_blocks_page_rows` (which carries each page's `blocks`), `duck_blocks_get_pages` — these take `LIST(duck_block)`, not file paths
  - The extractors — `duck_blocks_get_section`, `duck_blocks_get_pages`, `duck_blocks_sections_like`, `duck_blocks_page_rows` — **return `duck_block`s, not text.** The text they used to return is one rename away: `duck_blocks_get_section_text`, `duck_blocks_get_pages_text`, `duck_blocks_sections_like_text`.
  - The projections — `duck_blocks_headings`, `duck_blocks_toc`, `duck_blocks_code_blocks`, `duck_blocks_links` — **also return `duck_block`s**; the struct projections they used to return are `duck_blocks_headings_structs`, `duck_blocks_toc_structs`, `duck_blocks_code_blocks_structs`, `duck_blocks_links_structs`, byte-for-byte and permanent. `element_order` is preserved unrenumbered through every one of them, so a result joins back to its document.
    They took an `output_format` that could not work: a macro has one return type, so every
    branch — `'blocks'` included — had to collapse to `VARCHAR`, and `'blocks'` handed back
    `to_json(...)::VARCHAR`. Blocks in, blocks out; render with `duck_blocks_to_text`,
    `duck_blocks_render_ansi`, `duck_blocks_to_md`, or `duck_blocks_to_pandoc_ast`.
- **Comparing and reviewing documents**: `duck_blocks_diff` (what changed between two versions, as ADDED/REMOVED/MOVED) and `duck_blocks_quality` (document-quality rules that are NOT spec conformance — empty sections, duplicate headings, links with no text)

## Naming

Every function is prefixed by what it operates on:

| prefix | means | examples |
|---|---|---|
| `duck_block_*` | one element — accessing **or** constructing it | `duck_block_content`, `duck_block_paragraph`, `duck_block_bold` |
| `duck_blocks_*` | a collection | `duck_blocks_to_text`, `duck_blocks_toc`, `duck_blocks_validate` |
| `pandoc_*` | Pandoc AST conversion | `pandoc_ast_to_blocks` |

These were `db_*` until v1.1. `db_` reads as *database* everywhere else in SQL, and
`duck_block_*`/`duck_blocks_*` was already the convention for the accessors and for the
cross-extension writers (`duck_blocks_to_md`, `duck_blocks_to_html`), so this makes one
family instead of two.

**For brevity, `PRAGMA duck_block_aliases` gives HTML-style short names** — `p`, `h1`–`h6`,
`b`, `em`, `s`, `a`, `img`, `ul`, `ol`, `li`, `div`, `span`, `pre`, `bq`, `code`. That is
where terseness belongs: the builders you type constantly. Everything else is typed rarely
and reads better spelled out.

## Scope

This extension defines the `duck_block` vocabulary, owns it through validation, and
provides construction, query and rendering utilities over it. It does **not** read files
and does **not** write format-specific output:

**This extension depends on nothing.** Not on `markdown`, not on `webbed`, not on
`panduck`. `json` is the one exception, and it is a *core* DuckDB extension rather than a
sibling document-format one.

| concern | lives in |
|---|---|
| path → blocks (any format) | `panduck` — `read_panduck_doc`, `panduck_read_blocks` |
| blocks → markdown | `duckdb_markdown` — `duck_blocks_to_md` |
| blocks → HTML | `duckdb_webbed` — `duck_blocks_to_html` |
| blocks → text / ANSI / Pandoc AST | here |

There is no general `render(blocks, format)` entry point: format-specific writers are
composed directly, so this extension never has to know they exist.

```sql
SELECT duck_blocks_to_text(blocks);       -- here
SELECT duck_blocks_render_ansi(blocks);   -- here
LOAD markdown;  SELECT duck_blocks_to_md(blocks);
LOAD webbed;    SELECT duck_blocks_to_html(blocks);
```

ANSI rendering stays because it is the one output that is not a *format*: it knows only the
`duck_block` vocabulary, which makes it a utility over the spec rather than a converter.
`md` and `html` are formats, so the same test sends them out.

Each layer is useful alone. Converting HTML to markdown needs neither this extension nor
panduck (`LOAD webbed; LOAD markdown;` and compose), and building blocks by hand needs only
this one.

### Migration from the `doc_*` dispatch macros

Reader dispatch moved to `panduck`. Replacements:

| removed | replacement |
|---|---|
| `doc_to_blocks(path)` | `panduck_read_blocks(path)`, or `FROM read_panduck_doc(path)` to stream |
| `doc_read(path, output_format := X)` | `doc_render(panduck_read_blocks(path), X)` |
| `doc_is_supported(path)` | `panduck_can_read(path)` |
| `doc_supported_extensions()` | `panduck_supported_paths()` |
| `doc_select_blocks(path, sel)` | `panduck_select_blocks(path, sel)` |
| `doc_toc(path)` | `duck_blocks_toc_rows(panduck_read_blocks(path))` |
| `doc_section(...)` | `duck_blocks_get_section(...)` — returns `LIST(duck_block)`; **not** `duck_block_section`, which is an existing builder |
| `doc_search(...)` | `duck_blocks_sections_like(...)` — returns a `blocks` column, not rendered `content` |
| `doc_render(blocks, 'text')` | `duck_blocks_to_text(blocks)` |
| `doc_render(blocks, 'ansi')` | `duck_blocks_render_ansi(blocks)` |
| `doc_render(blocks, 'md')` | `duck_blocks_to_md(blocks)` — `LOAD markdown` |
| `doc_render(blocks, 'html')` | `duck_blocks_to_html(blocks)` — `LOAD webbed` |

The `doc_*` prefix now belongs to panduck; everything here is `db_*`.

Three behaviour changes came with it:

1. **Reading a file by path requires `LOAD panduck`**, `.md` included. Accepted so the
   extension→reader mapping lives in exactly one place instead of two during a transition.
2. **`output_format := 'md'` returns markdown**, where it previously returned Pandoc AST
   JSON under a misleading name.
3. **Unroutable extensions and data formats raise** instead of silently falling through to
   the markdown reader.

## Installation

```sql
INSTALL duck_block_utils FROM community;
LOAD duck_block_utils;
```

## Quick Start

```sql
-- Load markdown extension for reading
LOAD markdown;
LOAD duck_block_utils;

-- Extract all headings as a table of contents (row-shaped: level, title, id, indent, element_order)
SELECT * FROM duck_blocks_toc_rows(
    (SELECT list(b) FROM read_markdown_blocks('README.md') b)
);

-- Get plain text content
SELECT duck_blocks_to_text(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b)
);

-- Filter to specific block types
SELECT unnest(duck_blocks_filter(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b),
    ['heading', 'code']
));

-- Validate block schema
SELECT duck_blocks_validate(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b)
);
```

## The Unified duck_block Type

Both block-level and inline elements use the same unified type, distinguished by the `kind` field:

```sql
STRUCT(
    kind VARCHAR,                       -- 'block', 'inline' or 'value'
    element_type VARCHAR,               -- 'heading', 'paragraph', 'text', 'link', etc.
    content VARCHAR,                    -- Primary content
    level INTEGER,                      -- Structural nesting depth (NOT heading level)
    encoding VARCHAR,                   -- 'text', 'json', 'yaml', 'html', 'xml', 'latex', 'markdown'
    attributes MAP(VARCHAR, VARCHAR),   -- Type-specific metadata (heading_level, href, etc.)
    element_order INTEGER               -- Position in document (0-indexed)
)
```

### Block Types (kind='block')

| Type | Description | Key Attributes |
|------|-------------|----------------|
| `heading` | Section heading | `heading_level` (1-6) |
| `paragraph` | Body text | |
| `plain` | A block-level text run with no paragraph semantics | |
| `code` | Code block | `language` |
| `blockquote` | Quoted content | |
| `list` | List container | `list_type`: `bullet` / `ordered` / `definition`; for ordered also `start`, `number_style`, `number_delim` |
| `list_item` | An item in a list | `role` — `term` / `definition` in a definition list |
| `table` | Tabular data | `pandoc_ast` (the verbatim source tuple) |
| `hr` | Horizontal rule | |
| `lineblock` | Preserved line breaks | |
| `page_break` | Physical page boundary — a marker | `page_number` |
| `metadata` | A verbatim metadata blob | `role` — e.g. `frontmatter` |
| `image` | Block-level image | `src`, `alt`, `title` |
| `raw` | Raw content in a named format | `format` |
| `div` | Generic container | `id`, `class` |
| `section` | Semantic sectioning container | `role`, `id`, `class` |
| `figure` | Figure with a caption | `id`, `class` |
| `caption` | Caption belonging to the container before it | `short_caption` |
| `generic` | Structurally valid, type not in this vocabulary | `source_type` |
| `deflist` | **Deprecated.** Use `list` with `list_type='definition'` | |

The authoritative list is `duck_block_type_names()`; `docs/duck_blocks_spec.md` documents
every type in full, including the inline and `kind='value'` vocabularies.

**`level` is not in the table because it is the same for every type: structural nesting
depth in a depth-first ordering.** Top level is 1, a child is its parent's level + 1, and
it is **never NULL** — `duck_blocks_validate()` rejects a NULL level outright. Level and
adjacency together describe the whole document tree; there is no separate child-list
field.

**A heading's rank is NOT its level.** `h1`-`h6` lives in
`attributes['heading_level']`; the `level` field is structural depth. Do **not** fall
back to `level` when the attribute is missing — a heading nested inside two containers
would read as `h3` whatever it actually is. That fallback was once documented advice and
is now the single most costly mistake a producer can make here.

### Inline Types (kind='inline')

| Type | Description | Attributes |
|------|-------------|------------|
| `text` | Plain text | none |
| `link` | Hyperlink | `href`, `title` |
| `image` | Inline image | `src`, `alt`, `title` |
| `bold` | Bold/strong text | none |
| `italic` | Italic/emphasis | none |
| `code` | Inline code | none |
| `strikethrough` | Strikethrough | none |

## Functions

### Block Manipulation

| Function | Description |
|----------|-------------|
| `duck_blocks_filter(blocks, types[])` | Filter blocks to specific types |
| `duck_blocks_exclude(blocks, types[])` | Exclude specific block types |
| `db_blocks_transform(blocks, mappings)` | Apply type/content transformations |
| `duck_blocks_merge(blocks1, blocks2)` | Merge two block sequences |
| `duck_blocks_reorder(blocks)` | Renumber element_order sequentially |
| `duck_blocks_slice(blocks, start, end)` | Extract block range |

### Content Extraction

| Function | Description |
|----------|-------------|
| `duck_blocks_to_text(blocks)` | Extract plain text content |
| `duck_blocks_headings(blocks)` | The headings as `duck_block`s: flattened title, `attributes['outline']` |
| `duck_blocks_headings_structs(blocks)` | The heading projection: `level, title, id, element_order` |
| `duck_blocks_toc(blocks)` | The headings as `duck_block`s plus `attributes['indent']` |
| `duck_blocks_toc_structs(blocks)` | The TOC projection: `level, title, id, indent, element_order` |
| `duck_blocks_code_blocks(blocks)` | The `code` blocks, as they are |
| `duck_blocks_code_blocks_structs(blocks)` | The code projection: `language, content, element_order` |
| `duck_blocks_links(blocks)` | The URL-carrying elements (links and images), as they are |
| `duck_blocks_links_structs(blocks)` | The link projection: `href, text, title, element_order` |
| `duck_blocks_to_match_text(blocks)` | Text flattened with spaces, for `ILIKE`/regex search; `duck_blocks_to_text` is for rendering |
| `duck_blocks_get_section_text(blocks, pattern)` | Original text form of `duck_blocks_get_section` |
| `duck_blocks_get_pages_text(blocks, first, last)` | Original text form of `duck_blocks_get_pages` |
| `duck_blocks_sections_like_text(doc, term)` | Original `(section, start_order, content)` form of `duck_blocks_sections_like` |

### Validation & Analysis

| Function | Description |
|----------|-------------|
| `duck_blocks_validate(blocks)` | Check schema compliance — `{valid, errors}` with `{element_order, field, message}` per error |
| `duck_blocks_lint(blocks)` | Advisory warnings: shapes that are legal but superseded, or that lose information |
| `duck_blocks_normalize(blocks)` | Apply the content rule — collapse a lone `plain` into its container. Idempotent |
| `duck_blocks_stats(blocks)` | Block type statistics |
| `duck_blocks_structure(blocks)` | Analyze document structure |

### Conversion Helpers

| Function | Description |
|----------|-------------|
| `db_blocks_normalize(blocks)` | Convert to core types only |
| `db_blocks_map_types(blocks, mapping)` | Remap block types |

### Block Builders (Declarative Construction)

Builders are **config-first, content-last** and each returns a `LIST(duck_block)`:

| Function | Description |
|----------|-------------|
| `duck_block_heading(level, content)` | Create heading block |
| `duck_block_paragraph(content)` | Create paragraph block |
| `duck_block_plain(content)` | Block-level text run with no paragraph semantics |
| `duck_block_code(language, content)` | Create code block |
| `duck_block_blockquote(content)` / `duck_block_blockquote(level, content)` | Create blockquote block |
| `duck_block_list_block(items[])` / `duck_block_list_block(ordered, items[])` | Create list block (strings or rich items) |
| `duck_block_div(children[])` / `duck_block_div(id, class, children[])` | Create generic container |
| `duck_block_hr()` | Create horizontal rule |
| `duck_block_metadata(yaml_content)` | Create metadata block |
| `duck_block_image(src, alt, title)` | Create image block |
| `duck_block_raw(format, content)` | Create raw content block |

### Flattening Builder Overloads

These overloads accept a list of children and return a flattened list with parent at level 1 and children at level 2:

| Function | Description |
|----------|-------------|
| `duck_block_paragraph(children[])` | Paragraph with inline children |
| `duck_block_heading(level, children[])` | Heading with inline children |
| `duck_block_blockquote(level, children[])` | Blockquote with children |
| `duck_block_code(language, children[])` | Code block with children |
| `duck_block_bold(children[])` | Bold with inline children |
| `duck_block_italic(children[])` | Italic with inline children |
| `duck_block_link(href, children[], title)` | Link with inline children |

### Inline Builders

| Function | Description |
|----------|-------------|
| `duck_block_text(content)` | Create plain text inline |
| `duck_block_space()` | Create space inline |
| `duck_block_softbreak()` | Create soft break |
| `duck_block_linebreak()` | Create line break |
| `duck_block_bold(content)` | Create bold text |
| `duck_block_italic(content)` | Create italic text |
| `duck_block_strikethrough(content)` | Create strikethrough text |
| `duck_block_superscript(content)` | Create superscript |
| `duck_block_subscript(content)` | Create subscript |
| `duck_block_smallcaps(content)` | Create small caps |
| `duck_block_underline(content)` | Create underlined text |
| `duck_block_inline_code(content)` | Create inline code |
| `duck_block_math(content, display)` | Create math expression |
| `duck_block_link(href, text, title)` | Create hyperlink |
| `duck_block_inline_image(src, alt, title)` | Create inline image |
| `duck_block_quoted(content, quote_type)` | Create quoted text |
| `duck_block_cite(key, prefix, suffix)` | Create citation |
| `duck_block_note(content)` | Create footnote |
| `duck_block_span(content, id, classes)` | Create span |
| `duck_block_raw_inline(format, content)` | Create raw inline |

### Pandoc AST Conversion

| Function | Description |
|----------|-------------|
| `read_pandoc_ast(file_path)` | Read Pandoc JSON file and convert to duck_blocks |
| `pandoc_ast_to_blocks(json)` | Convert Pandoc JSON AST string to duck_blocks |
| `duck_blocks_to_pandoc_blocks(blocks)` | Convert duck_blocks to Pandoc JSON blocks array |
| `duck_blocks_to_pandoc_ast(blocks)` | Convert to complete Pandoc AST struct |
| `pandoc_ast(blocks, meta, api_version)` | Table function for JSON file output |
| `write_pandoc_ast(path, blocks)` | Write duck_blocks directly to Pandoc JSON file |
| `pandoc_inlines_to_text(inlines)` | Convert inline elements to text |
| `pandoc_inlines_to_db_inlines(inlines)` | Convert Pandoc inlines to duck_block inlines |
| `duck_blocks_inlines_to_pandoc(inlines)` | Convert duck_block inlines to Pandoc JSON |

## Examples

### Generate Table of Contents

```sql
SELECT
    repeat('  ', level - 1) || '- ' || title as toc_line,
    id
FROM duck_blocks_toc_rows(
    (SELECT list(b) FROM read_markdown_blocks('docs/**/*.md') b)
)
ORDER BY element_order;
```

### Extract Code Examples by Language

```sql
SELECT
    language,
    content as code,
    filename
FROM (
    SELECT filename, unnest(duck_blocks_code_blocks_structs(list(b)), recursive := true)
    FROM read_markdown_blocks('tutorial/*.md', filename := true) b
    GROUP BY filename
)
WHERE language = 'python';
```

### Merge Documents with Separator

```sql
SELECT unnest(duck_blocks_merge(
    duck_blocks_merge(
        (SELECT list(b) FROM read_markdown_blocks('intro.md') b),
        [duck_block_hr()]
    ),
    (SELECT list(b) FROM read_markdown_blocks('main.md') b)
));
```

### Validate and Report Issues

```sql
SELECT * FROM duck_blocks_lint(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b)
)
WHERE severity = 'error';
```

### Build Rich Text with Inline Elements

```sql
-- Create a paragraph with formatted inline content
SELECT [
    duck_block_text('Click '),
    duck_block_link('https://example.com', 'here'),
    duck_block_text(' to learn more about '),
    duck_block_bold('DuckDB'),
    duck_block_text('.')
];

-- Compose a paragraph from mixed inline builders
SELECT duck_block_paragraph([
    duck_block_text('This is '),
    duck_block_bold('bold'),
    duck_block_text(' and '),
    duck_block_italic('italic'),
    duck_block_text(' text.')
]);
```

### Convert Pandoc JSON to Blocks

```sql
-- Read Pandoc JSON file directly (simplest approach)
SELECT * FROM unnest(read_pandoc_ast('exported_from_pandoc.json'));

-- Or use pandoc_ast_to_blocks with a JSON string
SELECT unnest(pandoc_ast_to_blocks(
    (SELECT content FROM read_text('exported_from_pandoc.json'))
));
```

### Export to Pandoc JSON (for PDF, Word, etc.)

```sql
LOAD json;  -- Required for COPY FORMAT JSON

-- Basic export - creates Pandoc-compatible JSON
COPY (
    SELECT * FROM pandoc_ast(duck_blocks_assemble([
        duck_block_heading(1, 'My Document'),
        duck_block_paragraph('Hello world.')
    ]))
) TO 'document.json' (FORMAT JSON);

-- With document metadata (title, author, date)
COPY (
    SELECT * FROM pandoc_ast(
        duck_blocks_assemble([
            duck_block_heading(1, 'Report'),
            duck_block_paragraph('Introduction text...')
        ]),
        meta := {'title': 'Annual Report', 'author': 'Jane Doe', 'date': '2024-01-05'}
    )
) TO 'report.json' (FORMAT JSON);

-- Then convert with Pandoc CLI:
-- pandoc report.json -f json -o report.pdf
-- pandoc report.json -f json -o report.docx
```

## Short Aliases

For less verbose document composition, enable HTML-inspired short aliases:

```sql
PRAGMA duck_block_aliases;

-- Now you can use short names like h1, p, b, a, etc.
SELECT page([
    h1('DuckDB Search'),
    p([text('Found '), b('3'), text(' results.')]),
    pre('sql', 'LOAD extension;')
]);
```

Available aliases include: `h1`-`h6`, `p`, `pre`, `ul`, `ol`, `li`, `div`, `b`, `i`, `a`, `code`, and more. See [Block Builders](docs/block_builders.md) for the complete list.

## Terminal Rendering

Render documents — or any query result — as styled ANSI output for the terminal:

```sql
PRAGMA duck_block_render;

-- Documents: width-aware word wrap, styled headings/lists/tables
SELECT duck_blocks_render_ansi(
    duck_block_heading(1, 'Report')
    || duck_block_paragraph([duck_block_text('All systems '), duck_block_bold('nominal'), duck_block_text('.')])
);

-- Any query as a pretty ANSI table
SELECT rendered FROM duck_blocks_render_query('SELECT * FROM my_table LIMIT 10');
```

### Composing pages with embedded query results

`duck_blocks_page(title, blocks)` assembles a titled page, `duck_blocks_query_table(sql)` embeds a
query's results as a table block, and `duck_blocks_table(json)` does the same for a JSON
array — a dashboard in one expression:

```sql
PRAGMA duck_block_render;

SELECT duck_blocks_render(duck_blocks_page('Sales Report', [
    duck_block_paragraph('Top rows:'),
    duck_blocks_query_table('SELECT * FROM t ORDER BY id'),
    duck_block_paragraph('Aggregate:'),
    duck_blocks_query_table('SELECT count(*) AS n, round(avg(score), 2) AS avg_score FROM t')
]));
```

Text-based charts compose too — `textplot` output drops straight into block
content (see [`examples/textplot_dashboard.sql`](examples/textplot_dashboard.sql)).
On the terminal-graphics landscape and where a bitmap tier could go, see
[Terminal Graphics (Notes)](docs/terminal_graphics.md).

Width is auto-detected from the terminal (even when piped to `less -R`); pass
it explicitly with `duck_blocks_render_ansi(blocks, width)`. See
[Terminal Rendering](docs/rendering.md) for details, or try
`examples/duckglow.sql` for glow-style markdown file viewing:

```bash
duckdb -noheader -list \
  -c ".read examples/duckglow.sql" \
  -c "SELECT doc FROM glow('README.md');" | less -R
```

## Type Casting

Cast VARCHAR to duck_block to create text inline elements:

```sql
SELECT 'hello'::duck_block;  -- Creates text inline element
```

## Building

### Managing dependencies

DuckDB extensions use VCPKG for dependency management. Enabling VCPKG is simple:

```shell
git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
export VCPKG_TOOLCHAIN_PATH=`pwd`/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### Build steps

```sh
make
```

The main binaries that will be built are:
```sh
./build/release/duckdb
./build/release/test/unittest
./build/release/extension/duck_block_utils/duck_block_utils.duckdb_extension
```

### Running the tests

```sh
make test
```

## Ecosystem

This extension is part of the DuckDB document processing ecosystem:

| Extension | Purpose |
|-----------|---------|
| [duckdb_markdown](https://github.com/teaguesterling/duckdb_markdown) | Markdown reading/writing |
| [duckdb_webbed](https://github.com/teaguesterling/duckdb_webbed) | HTML/XML parsing |
| [duckdb_yaml](https://github.com/teaguesterling/duckdb_yaml) | YAML document parsing |
| [textplot](https://community-extensions.duckdb.org/extensions/textplot.html) | Text-based charts/sparklines (compose into rendered pages) |
| [panduck](https://github.com/teaguesterling/panduck) | Pandoc integration (planned) |

## Documentation

- [Getting Started](docs/getting_started.md) - Quickstart and common patterns
- [Design Document](docs/design.md) - Architecture and implementation details
- [API Reference](docs/api.md) - Complete function reference
- [Block Builders](docs/block_builders.md) - Declarative document construction
- [Inline Builders](docs/inline_builders.md) - Rich text inline elements
- [Duck Blocks Spec](docs/duck_blocks_spec.md) - Unified duck_block type specification
- [Pandoc AST Spec](docs/pandoc_ast_spec.md) - Pandoc JSON conversion rules
- [Terminal Rendering](docs/rendering.md) - ANSI output, word wrapping, pretty query tables
- [Terminal Graphics (Notes)](docs/terminal_graphics.md) - Bitmap-graphics landscape and roadmap

## License

MIT License - see [LICENSE](LICENSE) for details.

## Contributing

Contributions welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.
