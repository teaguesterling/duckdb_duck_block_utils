# Reader Dispatch Implementation Plan (Phase 2 of 3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move path→blocks reader dispatch out of `duck_block_utils` and into `panduck`, derived from self-describing readers, leaving `duck_block_utils` as utilities over the `duck_block` vocabulary.

**Architecture:** Two repos, one coordinated change. **Part A builds `panduck_read()` in panduck. Part B strips the corresponding macros from duck_block_utils.** Part A must land first — deleting `doc_to_blocks` before `panduck_read` exists leaves a window where nothing can read a file by path.

**Tech Stack:** DuckDB SQL macros, C++17 (registration), sqllogictest.

**Spec:** `docs/superpowers/specs/2026-08-31-pandoc-gaps-and-reader-dispatch-design.md` (§2, §4, §5 — note §4 is explicitly implemented in panduck, not here)

**Depends on:** Phase 1 only for a green build. No code dependency.

**Coordination:** Another session is actively working in `~/Projects/duckdb_panduck` with uncommitted changes (the RTF reader and `panduck_supported_extensions()`). **Confirm with that session before starting Part A.**

## Global Constraints

- **No new third-party dependencies** in either repo.
- **A format with a native DuckDB reader never routes to sitting_duck.** sitting_duck is the fallback for extensions nothing else claims.
- **Never hardcode an extension→reader mapping** for a reader that can describe itself.
- **Filter panduck's own registry on `status = 'implemented'`.** Today that yields `rtf` → `read_rtf_blocks`; the other seven formats are `planned` with `reader = NULL`.
- **Extensions are lowercase with no leading dot** inside `panduck_supported_extensions()` and `ast_supported_languages()`; the registry adds the dot.
- **Build/test both repos:** `make && make format-fix && make format-check && make test`.
- Conventional commit prefixes.

## Accepted Costs (state these in the PR)

1. **Reading a file by path now requires `LOAD panduck`**, including `.md`. Accepted deliberately — the alternative is maintaining the registry in two places during a transition, which is the defect this work removes. duckeye should add `panduck` to `DUCKEYE_BASE`.
2. **`doc_toc('README.md')` stops working.** Replacement: `doc_toc(panduck_read_blocks('README.md'))`. The `LIST`-materialising wrap does not appear from nowhere — it lives inside `doc_to_blocks` today (`SELECT list(b::duck_block) FROM query(...)`); it moves to the call site, where it becomes a choice rather than the only option.
3. **`output_format := 'md'` returns markdown**, not Pandoc AST JSON.
4. **Unroutable extensions and data formats raise** instead of falling through to the markdown reader.

---

# Part A — panduck: build the dispatcher

### Task A1: `panduck_reader_registry()` — derived, with the exclusion rule

**Files (in `~/Projects/duckdb_panduck`):**
- Create: `src/reader_registry.cpp`, `src/include/reader_registry.hpp`
- Modify: `CMakeLists.txt`, `src/panduck_extension.cpp`
- Test: `test/sql/reader_registry.test`

**Interfaces:**
- Produces: pragma `panduck_reader_macros` registering `panduck_reader_registry()` → `LIST(STRUCT(ext VARCHAR, format VARCHAR, reader_ext VARCHAR))`.
  - `ext` dot-prefixed lowercase (`.py`); `format` from the closed set `markdown|html|pdf|pandoc_ast|rtf|code|data`; `reader_ext` the DuckDB extension to load.
- Follow the existing registration pattern in `src/supported_extensions.cpp` — read it first.

**Layout invariant — a separate translation unit, and the dependency runs one way.**
`reader_registry.cpp` **includes `supported_extensions.hpp`, never the reverse.** If that
ever inverts, the layout is wrong.

The reason is not tidiness. `supported_extensions.cpp` is a static table plus a table
function over it: no I/O, no dependency on any other extension, and it must stay answerable
when nothing else is loaded — it is what a dispatcher interrogates *before* deciding
anything. `panduck_read()` is the opposite: it autoloads extensions, builds SQL strings, and
unions in sitting_duck. Putting the consumer in the same TU as the self-description makes
the self-description depend on the thing consuming it, which is the cycle the split exists
to prevent.

- [ ] **Step 1: Write the failing tests**

Create `test/sql/reader_registry.test`:

```
# name: test/sql/reader_registry.test
# description: derived reader dispatch — one reader per extension, no drift
# group: [sql]

require panduck

statement ok
PRAGMA panduck_reader_macros;

# .md belongs to markdown, NOT sitting_duck, even though sitting_duck claims it
query I
SELECT (r).format FROM (SELECT unnest(panduck_reader_registry()) r) WHERE (r).ext = '.md';
----
markdown

query I
SELECT (r).format FROM (SELECT unnest(panduck_reader_registry()) r) WHERE (r).ext = '.html';
----
html

# .py is sitting_duck's, uncontested
query I
SELECT (r).format FROM (SELECT unnest(panduck_reader_registry()) r) WHERE (r).ext = '.py';
----
code

# panduck's own implemented format
query I
SELECT (r).format FROM (SELECT unnest(panduck_reader_registry()) r) WHERE (r).ext = '.rtf';
----
rtf

# THE DRIFT REGRESSION TEST: every extension resolves to exactly one reader
query I
SELECT count(*) FROM (
  SELECT (r).ext AS ext, count(*) AS n FROM (SELECT unnest(panduck_reader_registry()) r) GROUP BY (r).ext
) WHERE n > 1;
----
0

# planned-but-unimplemented panduck formats are NOT in the registry
query I
SELECT count(*) FROM (SELECT unnest(panduck_reader_registry()) r) WHERE (r).ext = '.docx';
----
0

# SCHEMA CONFORMANCE. panduck_read's flat branches use `SELECT *`, which is only safe
# while every implemented reader really does emit the canonical duck_block schema.
# That is true today for read_markdown_blocks and read_rtf_blocks (verified identical
# with DESCRIBE), but the design now LEANS on it, so assert it rather than believe it.
# A future reader that adds a column or reorders fields fails here, at test time,
# instead of silently mapping content into encoding at runtime.
query I
WITH readers AS (
    SELECT reader FROM panduck_supported_extensions() WHERE status = 'implemented'
),
expected AS (
    SELECT ['kind','element_type','content','level','encoding','attributes','element_order'] AS cols
)
SELECT count(*) FROM readers r, expected e
WHERE (SELECT list(column_name) FROM (DESCRIBE SELECT * FROM query('SELECT * FROM ' || r.reader || '(''test/fixtures/heading.rtf'')'))) <> e.cols;
----
0
```

If that assertion is awkward to express against `DESCRIBE` in a macro context, hoist it to a small C++ test or a Python check instead — the requirement is that **the canonical schema is asserted somewhere that runs in CI**, not that it is asserted in SQL specifically.

**The schema guard is not sufficient on its own — pair it with a value check.**

A check on *shape* cannot see an error in *mapping*. Hoisting the column list into one
`panduck_block_cols()` removed six independent drifts, but it also changed the failure mode:
six branches drift **inconsistently** (one wrong, five right — almost any comparison finds
the odd one out), whereas one shared definition drifts **consistently**, with all six
branches wrong together and in agreement.

Measured, with `panduck_block_cols()` transposing `content` and `encoding`:

| drift written as | `DESCRIBE` guard | value check |
|---|---|---|
| `b.encoding, … b.content` (no aliases) | **catches it** — column order differs | catches it |
| `b.encoding AS content, … b.content AS encoding` | **PASSES** — canonical names and order | **catches it** — differing rows |

The aliased form is not contrived; it is how the list would be written by someone being
careful about names. So add, per format:

```
# FLAT FORMATS ARE AN EXACT IDENTITY: panduck_read must be indistinguishable from the
# reader it dispatches to. Symmetric, because EXCEPT is one-directional and would miss
# rows the dispatcher drops.
query I
SELECT (SELECT count(*) FROM ((SELECT * FROM panduck_read('test/fixtures/heading.rtf'))
                              EXCEPT (SELECT * FROM read_rtf_blocks('test/fixtures/heading.rtf'))))
     + (SELECT count(*) FROM ((SELECT * FROM read_rtf_blocks('test/fixtures/heading.rtf'))
                              EXCEPT (SELECT * FROM panduck_read('test/fixtures/heading.rtf'))));
----
0
```

For a list format, compare against the underlying scalar unnested the same way. **Keep both
guards** — `DESCRIBE` catches a reader that adds or reorders a column, which `EXCEPT` cannot
distinguish from a value bug; `EXCEPT` catches mis-mapping behind a correct-looking shape,
which `DESCRIBE` cannot see at all. They cover different failures.

*This is the third instance of one pattern today.* The registry's original "implemented
count = 0" checked a number when the invariant was about function existence; the schema
guard checks a shape when the invariant is about mapping. **Wherever something is copied
field by field, a shape-check needs a value-check beside it.**

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test 2>&1 | grep -A5 reader_registry`
Expected: FAIL — the pragma does not exist.

- [ ] **Step 3: Add the static core**

In `src/reader_registry.cpp`, inside the SQL literal. Everything here is a hardcoded fact to delete as each reader learns to self-describe:

```sql
-- The canonical duck_block column list, addressed BY NAME, in ONE place. The
-- list-producing dispatch branches interpolate this rather than each spelling out
-- seven columns -- six copies would be six chances to drift, and kind/element_type/
-- content/encoding are all VARCHAR, so a transposition type-checks and silently
-- corrupts. Flat branches never use it: they pass `SELECT *` straight through.
CREATE OR REPLACE MACRO panduck_block_cols() AS (
    'b.kind, b.element_type, b.content, b.level, b.encoding, b.attributes, b.element_order'
);

CREATE OR REPLACE MACRO panduck_static_readers() AS (
    [
        {'ext': '.md',       'format': 'markdown',   'reader_ext': 'markdown'},
        {'ext': '.markdown', 'format': 'markdown',   'reader_ext': 'markdown'},
        {'ext': '.txt',      'format': 'markdown',   'reader_ext': 'markdown'},
        {'ext': '.html',     'format': 'html',       'reader_ext': 'webbed'},
        {'ext': '.htm',      'format': 'html',       'reader_ext': 'webbed'},
        {'ext': '.xml',      'format': 'html',       'reader_ext': 'webbed'},
        {'ext': '.pdf',      'format': 'pdf',        'reader_ext': 'pdf'},
        {'ext': '.json',     'format': 'pandoc_ast', 'reader_ext': 'json'},
        {'ext': '.yaml',     'format': 'data',       'reader_ext': 'yaml'},
        {'ext': '.yml',      'format': 'data',       'reader_ext': 'yaml'},
        {'ext': '.toml',     'format': 'data',       'reader_ext': 'toml'},
        {'ext': '.csv',      'format': 'data',       'reader_ext': ''},
        {'ext': '.tsv',      'format': 'data',       'reader_ext': ''},
        {'ext': '.parquet',  'format': 'data',       'reader_ext': ''},
        {'ext': '.xlsx',     'format': 'data',       'reader_ext': 'excel'}
    ]
);
```

- [ ] **Step 4: Add the registry**

```sql
-- Readers describe themselves; this assembles their claims. One collision rule:
-- a format with a native DuckDB reader never goes to sitting_duck. sitting_duck
-- is the fallback for extensions nothing else claims.
CREATE OR REPLACE MACRO panduck_reader_registry() AS (
    [
        (
            WITH static_r AS (SELECT unnest(panduck_static_readers()) AS r),
                 own_r AS (
                     SELECT {'ext': '.' || lower(e), 'format': f, 'reader_ext': 'panduck'} AS r
                     FROM (SELECT unnest(extensions) AS e, format AS f
                           FROM panduck_supported_extensions()
                           WHERE status = 'implemented')
                     WHERE e IS NOT NULL AND e <> ''
                 ),
                 claimed AS (
                     SELECT (r).ext AS ext FROM static_r
                     UNION ALL SELECT (r).ext FROM own_r
                 ),
                 sd_r AS (
                     SELECT {'ext': '.' || lower(e), 'format': 'code', 'reader_ext': 'sitting_duck'} AS r
                     FROM query(
                         CASE WHEN panduck_ensure_extension('sitting_duck')
                             THEN 'SELECT unnest(extensions) AS e FROM ast_supported_languages()'
                             ELSE 'SELECT NULL AS e WHERE false'
                         END
                     ) t(e)
                     WHERE e IS NOT NULL AND e <> ''
                       AND '.' || lower(e) NOT IN (SELECT ext FROM claimed)   -- the exclusion rule
                 )
            SELECT list(r) FROM (
                SELECT r FROM static_r
                UNION ALL SELECT r FROM own_r
                UNION ALL SELECT r FROM sd_r
            )
        )
    ][1]
);
```

`panduck_ensure_extension(VARCHAR) -> BOOLEAN` is a C++ scalar wrapping `ExtensionHelper::TryAutoLoadExtension`. Port it from `duck_block_utils/src/doc_macros.cpp:10-16` (`DbEnsureExtensionFun`), which is nine lines and already does exactly this.

**Verified 2026-08-31 — the mechanism works, including inside the macro path.** This was
challenged on the grounds that community extensions might not autoload, which would make the
accepted cost "LOAD panduck AND LOAD the target" rather than just "LOAD panduck". Measured
against the built binary:

| check | result |
|---|---|
| `SELECT db_ensure_extension('markdown')` in a fresh session | returns `true`, and `duckdb_extensions()` shows `markdown` **loaded** |
| calling `parse_markdown_to_duck_blocks` immediately after | works |
| same call **without** the ensure | `Catalog Error` — so nothing implicit is happening |
| `doc_to_blocks('t.html')` — the full `CASE WHEN ensure THEN '<sql>' END` → `query()` path, webbed not loaded | returns blocks, `webbed` ends up **loaded** |
| `db_ensure_extension('definitely_not_real_xyz')` | returns `false` — no throw |
| `autoinstall_known_extensions` / `autoload_known_extensions` | **both `false`**, and it still worked |

Two conclusions that matter for this plan. First, `TryAutoLoadExtension` does not go through
the `autoload_known_extensions` setting — it loads an **installed** extension explicitly, so
it works in the default configuration. Second, the ensure genuinely completes before
`query()` binds the SQL string; the pattern is not relying on undefined evaluation order.

**Remaining caveat, and why the `ELSE error(...)` branches matter:** this only covers
extensions that are *installed*. If one is not, `panduck_ensure_extension` returns `false`
rather than throwing, and the `ELSE error(...)` in each dispatch branch is what turns that
into a message naming the missing extension. Do not simplify those branches away.

- [ ] **Step 5: Build and test**

```bash
make && make format-fix && make format-check && make test
```
Expected: all six assertions PASS.

- [ ] **Step 6: Commit**

```bash
git add src/reader_registry.cpp src/include/reader_registry.hpp CMakeLists.txt src/panduck_extension.cpp test/sql/reader_registry.test
git commit -m "feat: derive reader registry from self-describing readers"
```

---

### Task A2: `panduck_read()` — dispatch on format, error loudly

**Files:**
- Modify: `src/reader_registry.cpp`, `test/sql/reader_registry.test`

**Interfaces:**
- Consumes: `panduck_reader_registry()` (A1).
- Produces: `panduck_format_for(path)` → format name or NULL; `panduck_supported_paths()` → `VARCHAR[]`; `panduck_can_read(path)` → BOOLEAN.
- **`panduck_read(src, format := 'auto', pages := '')` is a TABLE macro** — the primary surface.
- **`panduck_read_blocks(src, format := 'auto', pages := '')` is a scalar** returning `LIST(duck_block)` — a one-line wrapper over the table macro, for the `db_*` utilities that take a list.
- `format := 'code'` bypasses the registry and forces sitting_duck.
- `panduck_format_for` performs **no I/O** — `panduck_can_read` calls it.

**Why a table function, not a scalar returning `LIST` (measured, not stylistic).**

The portfolio convention across all three extensions splits on *argument type*, not taste:
every function taking a **path** is a table function named `read_*`
(`read_markdown_blocks`, `read_markdown_sections`, `read_rtf_blocks`); every scalar takes
content or a list already in hand (`parse_markdown_to_duck_blocks`, `html_to_duck_blocks`,
`pandoc_ast_to_blocks`, `db_blocks_toc`, `duck_blocks_to_md`). `doc_to_blocks` — a scalar
taking a path — is the single exception in the portfolio, and it is the thing being
replaced. Reproducing its shape would carry that inconsistency into the extension meant to
own the convention.

`EXPLAIN` on the two forms, verified against the built binary:

```
table macro over query()          scalar returning LIST
  PROJECTION                        HASH_GROUP_BY   <- list(#0): FULL BARRIER
    FILTER (element_type=...)         PROJECTION
      READ_MARKDOWN_BLOCKS              PROJECTION struct_pack(...)
                                          READ_MARKDOWN_BLOCKS
```

The table form pushes the filter **down to the reader** and the `query()` wrapper vanishes
from the physical plan. The scalar form puts a blocking aggregate in the way: the entire
document is materialised into one value before anything downstream runs, and no predicate
can push below it. Fine for a README; not for the 400-page EPUB on panduck's roadmap.

It also makes the dispatcher the same shape as what it dispatches to — `panduck_read` →
`read_rtf_blocks` is table → table, with no `list()`/`unnest` round trip purely to change
container.

**Naming deviation, recorded deliberately.** By the convention above this would be spelled
`read_*`. `panduck_read` is kept as a branded entry point because it is format-agnostic
dispatch rather than a reader for one format. This is a deliberate choice, not an oversight
— do not "fix" it to `read_panduck_blocks` without revisiting the reasoning.

**Verified feasible:** a scalar macro can wrap a table macro
(`(SELECT list(b::duck_block) FROM panduck_read(p) b)` returns 173 blocks for README.md),
so the two functions are one implementation.

- [ ] **Step 1: Write the failing tests**

Append to `test/sql/reader_registry.test`:

```
query I
SELECT panduck_format_for('a.md');
----
markdown

query I
SELECT panduck_format_for('a.hpp');
----
code

query I
SELECT panduck_format_for('a.toml');
----
data

query I
SELECT panduck_format_for('a.nope') IS NULL;
----
true

query I
SELECT panduck_can_read('a.docx');
----
false

statement error
SELECT * FROM panduck_read('/etc/hostname');
----
unsupported extension

statement error
SELECT * FROM panduck_read('test/fixtures/sample.parquet');
----
is a data format

# the table form streams: a filter must push down to the reader, not sit above a
# materialised list. Regression guard for the shape, not just the result.
query I
SELECT count(*) > 0 FROM panduck_read('test/fixtures/heading.rtf') WHERE element_type = 'heading';
----
true

# the scalar wrapper returns the same document as a list
query I
SELECT len(panduck_read_blocks('test/fixtures/heading.rtf')) =
       (SELECT count(*) FROM panduck_read('test/fixtures/heading.rtf'));
----
true
```

Add a non-Pandoc JSON fixture and assert the sniff:

```bash
echo '{"name":"widget","count":3}' > test/fixtures/not_pandoc.json
```

```
statement error
SELECT * FROM panduck_read('test/fixtures/not_pandoc.json');
----
not a Pandoc AST
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test 2>&1 | grep -A5 reader_registry`
Expected: FAIL — none of these macros exist.

- [ ] **Step 3: Add introspection**

```sql
CREATE OR REPLACE MACRO panduck_format_for(path) AS (
    (SELECT (r).format
     FROM (SELECT unnest(panduck_reader_registry()) AS r)
     WHERE (r).ext = '.' || split_part(lower(path), '.', -1)
     LIMIT 1)
);

CREATE OR REPLACE MACRO panduck_supported_paths() AS (
    list_sort(list_distinct([(r).ext for r in panduck_reader_registry()]))
);

CREATE OR REPLACE MACRO panduck_can_read(path) AS (
    panduck_format_for(path) IS NOT NULL
);

CREATE OR REPLACE MACRO panduck_quote(s) AS ('''' || replace(coalesce(s, ''), '''', '''''') || '''');
```

- [ ] **Step 4: Add `panduck_read`**

```sql
-- Output schema is the seven flat duck_block columns -- kind, element_type, content,
-- level, encoding, attributes, element_order -- which DESCRIBE confirms is already
-- byte-identical between read_markdown_blocks and read_rtf_blocks. `SELECT *` from a
-- native reader IS the canonical shape; no projection is needed to reach it.
--
-- Each branch yields those columns directly. Do NOT route branches through a single
-- duck_block column and unpack once at the top: EXPLAIN shows the pack/unpack pair
-- does not optimise away, and the filter degrades to
-- struct_extract(struct_pack(<all seven>)) evaluated per row just to compare one
-- VARCHAR. That cost lands on the native table readers -- exactly the branches facing
-- large DOCX and EPUB.
--
--   flat pass-through              pack-to-struct then unpack
--     PROJECTION #2                  PROJECTION struct_extract(b,'content')
--     FILTER (element_type=...)      PROJECTION struct_pack(kind, ...)
--     READ_MARKDOWN_BLOCKS           FILTER (struct_extract(struct_pack(kind, ...)))
--                                    READ_MARKDOWN_BLOCKS
--
-- Note neither branch form below writes a POSITIONAL column list. Flat branches use
-- `SELECT *`, so ordering comes from the reader and there is nothing to transpose;
-- list branches address fields BY NAME. This matters because kind, element_type,
-- content and encoding are all VARCHAR -- transposing two would type-check and
-- silently corrupt.
CREATE OR REPLACE MACRO panduck_read(src, format := 'auto', pages := '') AS TABLE
    SELECT * FROM query(
                CASE coalesce(nullif(lower(format), 'auto'), panduck_format_for(src), '__none__')

                    WHEN 'markdown' THEN
                        CASE WHEN panduck_ensure_extension('markdown')
                            THEN 'SELECT * FROM read_markdown_blocks(' || panduck_quote(src) || ')'
                            ELSE error('panduck_read: ' || src || ' needs the markdown extension') END

                    WHEN 'html' THEN
                        CASE WHEN panduck_ensure_extension('webbed')
                            THEN 'SELECT ' || panduck_block_cols() || ' FROM (SELECT unnest(html_to_duck_blocks(html)) AS b FROM read_html_objects(' || panduck_quote(src) || '))'
                            ELSE error('panduck_read: ' || src || ' needs the webbed extension') END

                    WHEN 'pdf' THEN
                        CASE WHEN panduck_ensure_extension('pdf')
                            THEN CASE WHEN coalesce(pages, '') = ''
                                THEN 'SELECT ' || panduck_block_cols() || ' FROM (SELECT unnest(parse_markdown_to_duck_blocks(pdf_to_markdown(' || panduck_quote(src) || '))) AS b)'
                                ELSE 'SELECT unnest(parse_markdown_to_duck_blocks(string_agg(''## Page '' || page || chr(10) || chr(10) || coalesce(text, ''''), chr(10) || chr(10) ORDER BY page))) AS b FROM read_pdf(' || panduck_quote(src) ||
                                     ', first_page := ' || split_part(pages, '-', 1) ||
                                     ', last_page := ' || coalesce(nullif(split_part(pages, '-', 2), ''), split_part(pages, '-', 1)) || ')'
                                END
                            ELSE error('panduck_read: ' || src || ' needs the pdf extension') END

                    WHEN 'pandoc_ast' THEN
                        -- A .json file is only a document if it actually is a Pandoc AST.
                        CASE WHEN (SELECT content LIKE '%"pandoc-api-version"%' FROM read_text(src))
                            THEN 'SELECT ' || panduck_block_cols() || ' FROM (SELECT unnest(read_pandoc_ast(' || panduck_quote(src) || ')) AS b)'
                            ELSE error('panduck_read: ' || src || ' is not a Pandoc AST; it is data — read it directly')
                        END

                    WHEN 'rtf' THEN 'SELECT * FROM read_rtf_blocks(' || panduck_quote(src) || ')'

                    WHEN 'code' THEN
                        CASE WHEN panduck_ensure_extension('sitting_duck')
                            THEN 'SELECT ' || panduck_block_cols() || ' FROM (SELECT unnest(blocks) AS b FROM ast_to_blocks_list(' || panduck_quote(src) || '))'
                            ELSE error('panduck_read: ' || src || ' needs the sitting_duck extension') END

                    WHEN 'data' THEN
                        error('panduck_read: ' || src || ' is a data format, not a document; read it directly')

                    ELSE error('panduck_read: unsupported extension .' ||
                               split_part(lower(src), '.', -1) || ' for ' || src ||
                               '; pass format := to force a reader')
                END
            );

-- Scalar wrapper for the db_* utilities, which take LIST(duck_block). Same
-- implementation, different container: this is where the materialisation cost is
-- paid, explicitly and at the call site's choosing, rather than baked into the only
-- available surface. Fields are addressed BY NAME, never positionally.
CREATE OR REPLACE MACRO panduck_read_blocks(src, format := 'auto', pages := '') AS (
    (SELECT list(b::duck_block)
     FROM (SELECT {'kind': kind, 'element_type': element_type, 'content': content,
                   'level': level, 'encoding': encoding, 'attributes': attributes,
                   'element_order': element_order} AS b
           FROM panduck_read(src, format, pages)))
);
```

Note `rtf` needs no `panduck_ensure_extension` guard — if this macro is running, panduck is loaded.

**Two branch shapes, by what the underlying reader produces:**

```
native TABLE readers    'SELECT * FROM read_rtf_blocks(''p'')'
                        'SELECT * FROM read_markdown_blocks(''p'')'

scalar LIST producers   'SELECT b.kind, b.element_type, b.content, b.level,
                         b.encoding, b.attributes, b.element_order
                         FROM (SELECT unnest(html_to_duck_blocks(...)) AS b)'
```

The flat branches (`markdown`, `rtf`) pass through untouched. The list-producing branches
(`html`, `pdf`, `pandoc_ast`, `code`) unpack by name. Only the second group pays a
projection, and those readers already materialise a list before the dispatcher sees them.

- [ ] **Step 5: Build and test**

```bash
make && make format-fix && make format-check && make test
```
Expected: all eight assertions PASS.

- [ ] **Step 6: Verify no advertised extension is unroutable**

```bash
./build/release/duckdb -c "LOAD panduck; PRAGMA panduck_reader_macros;
SELECT ext FROM (SELECT unnest(panduck_supported_paths()) AS ext)
WHERE NOT panduck_can_read('x' || ext);"
```
Expected: zero rows.

- [ ] **Step 7: Commit**

```bash
git add src/reader_registry.cpp test/sql/reader_registry.test test/fixtures/not_pandoc.json
git commit -m "feat: add panduck_read dispatching on format with loud errors"
```

---

### Task A3: Move `doc_select` to panduck

CSS-selector queries need `read_ast(path)`, so they belong with the path surface. This is a straight port — the logic is correct today.

**Files:**
- Modify: `src/reader_registry.cpp`, `test/sql/reader_registry.test`

**Interfaces:**
- Produces: `panduck_select_blocks(src, css_selector)` → `LIST(duck_block)`.
- Uses sitting_duck for **all** file types including `.md` and `.html` — a CSS-selector query is an explicit request for the syntax-tree view, so it is the right reader even where a document reader owns the extension for `panduck_read`.

- [ ] **Step 1: Write the failing test**

```
query I
SELECT len(panduck_select_blocks('src/reader_registry.cpp', 'function_definition')) >= 0;
----
true
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test 2>&1 | grep -A5 reader_registry`
Expected: FAIL — macro does not exist.

- [ ] **Step 3: Port the macro**

Copy verbatim from `duck_block_utils/src/doc_macros.cpp`, renaming `db_quote` → `panduck_quote` and `db_ensure_extension` → `panduck_ensure_extension`:

```sql
CREATE OR REPLACE MACRO panduck_select_blocks(src, css_selector) AS (
    [
        (
            SELECT list(b::duck_block)
            FROM query(
                (CASE WHEN panduck_ensure_extension('sitting_duck') THEN ''
                      ELSE error('panduck_select_blocks: needs the sitting_duck extension') END) ||
                'WITH ast_table AS (SELECT * FROM read_ast(' || panduck_quote(src) || ', peek := ''full'')), ' ||
                '     sel_table AS (SELECT * FROM ast_select_from(''ast_table'', ' || panduck_quote(css_selector) || ')) ' ||
                'SELECT unnest(blocks) AS b FROM (SELECT list(block) AS blocks FROM ast_to_blocks_from(''sel_table''))'
            ) r(b)
        )
    ][1]
);
```

- [ ] **Step 4: Build, test, commit**

```bash
make && make format-fix && make format-check && make test
git add src/reader_registry.cpp test/sql/reader_registry.test
git commit -m "feat: move CSS-selector block queries into panduck_select_blocks"
```

---

### Task A4: panduck documentation

- [ ] **Step 1: Document the reader surface**

In panduck's `README.md`, add `panduck_read`, `panduck_can_read`, `panduck_supported_paths`, `panduck_select_blocks` and `panduck_reader_registry` to the SQL surface section.

- [ ] **Step 2: Document the collision rule and the data split**

Add a **Reader dispatch** subsection covering: the derived registry and why (two mappings in duck_block_utils drifted within four days), the exclusion rule with the collision table (`.md`/`.html`/`.json`/`.toml` claimed by both a native reader and sitting_duck), documents-vs-data, and `format := 'code'` as the escape hatch.

- [ ] **Step 3: Update the roadmap**

Phase 6 (`panduck_read(path)` — panduck takes ownership of dispatch) is now **done**, ahead of the reader phases. Note that it landed before Phases 2-5 because dispatch is derived and does not depend on panduck's own readers existing.

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "docs: document panduck_read and derived reader dispatch"
```

---

# Part B — duck_block_utils: strip dispatch, keep utilities

**Do not start Part B until Part A is merged and `panduck_read` works.**

### Task B1: `doc_render` — one output path, delegated

**Files:**
- Modify: `src/doc_macros.cpp`
- Test: `test/sql/doc_macros.test`

**Interfaces:**
- Produces: `doc_render(blocks, output_format := 'text')` → VARCHAR. Accepts `text`, `ansi`, `blocks`, `pandoc`, `md`, `html`; anything else raises. Consumed by B2.

- [ ] **Step 1: Write the failing tests**

```
# ============================================================================
# doc_render — delegated output
# ============================================================================

query I
SELECT doc_render(parse_markdown_to_duck_blocks('# T'), 'text') LIKE '%T%';
----
true

# md delegates to duckdb_markdown and returns MARKDOWN, not pandoc JSON
query I
SELECT doc_render(parse_markdown_to_duck_blocks('# Title'), 'md') LIKE '#%Title%';
----
true

query I
SELECT doc_render(parse_markdown_to_duck_blocks('# Title'), 'md') LIKE '%pandoc-api-version%';
----
false

query I
SELECT doc_render(parse_markdown_to_duck_blocks('# Title'), 'html') LIKE '%<h1%';
----
true

query I
SELECT doc_render(parse_markdown_to_duck_blocks('# Title'), 'pandoc') LIKE '%pandoc-api-version%';
----
true

statement error
SELECT doc_render(parse_markdown_to_duck_blocks('# T'), 'nonsense');
----
unsupported output_format
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test 2>&1 | grep -A5 doc_macros`
Expected: FAIL — `doc_render` does not exist.

- [ ] **Step 3: Add the macro**

```sql
-- Output rendering is delegated: format-specific writers live in the format
-- extensions. duck_blocks_to_md is duckdb_markdown's; duck_blocks_to_html is
-- webbed's. This extension owns only text, ansi, blocks and pandoc.
CREATE OR REPLACE MACRO doc_render(blocks, output_format := 'text') AS (
    CASE lower(output_format)
        WHEN 'text'   THEN db_blocks_to_text(blocks)
        WHEN 'ansi'   THEN db_blocks_render_ansi(blocks)
        WHEN 'blocks' THEN CASE WHEN db_ensure_extension('json')
                                THEN to_json(blocks)::VARCHAR
                                ELSE error('doc_render: output_format=blocks needs the json extension') END
        WHEN 'pandoc' THEN CASE WHEN db_ensure_extension('json')
                                THEN to_json(duck_blocks_to_pandoc_ast(blocks))::VARCHAR
                                ELSE error('doc_render: output_format=pandoc needs the json extension') END
        WHEN 'md'     THEN CASE WHEN db_ensure_extension('markdown')
                                THEN duck_blocks_to_md(blocks)
                                ELSE error('doc_render: output_format=md needs the markdown extension') END
        WHEN 'html'   THEN CASE WHEN db_ensure_extension('webbed')
                                THEN duck_blocks_to_html(blocks)
                                ELSE error('doc_render: output_format=html needs the webbed extension') END
        ELSE error('doc_render: unsupported output_format ''' || coalesce(output_format, 'NULL') ||
                   '''; expected one of text, ansi, blocks, pandoc, md, html')
    END
);
```

The `db_ensure_extension` calls now have a real `ELSE` naming the missing extension, replacing the `THEN '' ELSE '' END ||` idiom that discarded the result and surfaced later as "function not found".

- [ ] **Step 4: Build, test, commit**

```bash
make && make format-fix && make format-check && make test
git add src/doc_macros.cpp test/sql/doc_macros.test
git commit -m "feat(macros): add doc_render delegating md/html to markdown and webbed"
```

---

### Task B2: `doc_toc` / `doc_section` / `doc_search` take blocks

**Files:**
- Modify: `src/doc_macros.cpp`
- Test: `test/sql/doc_macros.test`

**Interfaces:**
- Consumes: `doc_render` (B1).
- Produces: `doc_toc(blocks)`, `doc_section(blocks, pattern, output_format := 'text')`, `doc_search(blocks, term, output_format := 'text')`. **No path forms.**

- [ ] **Step 1: Rewrite the existing tests to pass blocks**

The current `test/sql/doc_macros.test` calls `doc_toc('README.md')` etc. Replace each path argument with `parse_markdown_to_duck_blocks(...)` over an inline document, so the tests stop depending on README's contents:

```
query I
SELECT count(*) > 0 FROM doc_toc(parse_markdown_to_duck_blocks('# A' || chr(10) || chr(10) || '## B'));
----
true

query T
SELECT title FROM doc_toc(parse_markdown_to_duck_blocks('# Overview' || chr(10) || chr(10) || 'body')) WHERE title = 'Overview';
----
Overview

query I
SELECT doc_section(parse_markdown_to_duck_blocks('# A' || chr(10) || chr(10) || 'body a' || chr(10) || chr(10) || '# B' || chr(10) || chr(10) || 'body b'), 'A') LIKE '%body a%';
----
true

query I
SELECT count(*) > 0 FROM doc_search(parse_markdown_to_duck_blocks('# A' || chr(10) || chr(10) || 'findme'), 'findme');
----
true
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test 2>&1 | grep -A5 doc_macros`
Expected: FAIL — the macros interpolate their first argument into a `read_*` call.

- [ ] **Step 3: Rewrite `doc_toc`**

The current version builds a dynamic SQL string solely to thread a path into `query()`. Taking blocks removes that entirely:

```sql
CREATE OR REPLACE MACRO doc_toc(blocks) AS TABLE
    SELECT (toc).level AS level,
           (toc).title AS title,
           (toc).id AS id,
           (toc).indent AS indent,
           (toc).element_order AS element_order
    FROM (SELECT unnest(db_blocks_toc(blocks)) AS toc);
```

- [ ] **Step 4: Rewrite `doc_section`**

Two changes: the block source, and the duplicated output `CASE` becomes `doc_render`. **The slicing logic is load-bearing — copy it verbatim.** `top` selects innermost non-overlapping sections; getting it wrong silently returns the wrong slice rather than failing.

```sql
CREATE OR REPLACE MACRO doc_section(blocks, section_pattern, output_format := 'text') AS (
    [
        (
            SELECT doc_render(sliced_blocks, output_format)
            FROM (
                WITH doc AS (SELECT blocks AS b),
                     h AS (SELECT e.level AS lvl, e.title AS title, e.id AS id, e.element_order AS ord
                           FROM (SELECT unnest(db_blocks_headings(b)) AS e FROM doc)),
                     sec AS (SELECT h1.ord AS s, coalesce(min(h2.ord) - 1, 2147483647) AS e
                             FROM h h1 LEFT JOIN h h2 ON h2.ord > h1.ord AND h2.lvl <= h1.lvl
                             WHERE h1.title ILIKE '%' || section_pattern || '%' OR h1.id = section_pattern
                             GROUP BY h1.ord),
                     top AS (SELECT s, e FROM sec a
                             WHERE NOT EXISTS (SELECT 1 FROM sec b2
                                               WHERE b2.s <= a.s AND b2.e >= a.e AND (b2.s, b2.e) <> (a.s, a.e)))
                SELECT db_blocks_reorder(flatten(list(db_blocks_slice(doc.b, top.s, top.e) ORDER BY top.s))) AS sliced_blocks
                FROM doc, top
            )
        )
    ][1]
);
```

- [ ] **Step 5: Rewrite `doc_search`**

Currently one large `query()` string built solely to thread a path through. Taking blocks removes the string-building. **The preamble span in the `UNION ALL` is load-bearing** — it makes content before the first heading searchable.

```sql
CREATE OR REPLACE MACRO doc_search(blocks, query_term, output_format := 'text') AS TABLE
    WITH doc AS (SELECT blocks AS b),
         h AS (SELECT e.element_order AS ord, e.title AS title
               FROM (SELECT unnest(db_blocks_headings(b)) AS e FROM doc)),
         span AS (SELECT ord AS s, coalesce(lead(ord) OVER (ORDER BY ord) - 1, 2147483647) AS e, title FROM h
                  UNION ALL
                  SELECT 0 AS s, coalesce((SELECT min(ord) - 1 FROM h), 2147483647)::INT AS e, '(preamble)' AS title
                  WHERE coalesce((SELECT min(ord) - 1 FROM h), 2147483647)::INT >= 0),
         hit AS (SELECT span.s, span.e, span.title, db_blocks_slice(doc.b, span.s, span.e) AS sec_blocks
                 FROM doc, span
                 WHERE db_blocks_to_text(db_blocks_slice(doc.b, span.s, span.e)) ILIKE '%' || query_term || '%')
    SELECT hit.title AS section,
           hit.s AS start_order,
           doc_render(hit.sec_blocks, output_format) AS content
    FROM hit
    ORDER BY hit.s;
```

This also fixes a latent bug: the old inlined `CASE` silently fell back to `text` for `output_format` values of `blocks`, `md` or `pandoc`. Routing through `doc_render` makes all six work and makes an invalid one raise.

- [ ] **Step 6: Build, test, commit**

```bash
make && make format-fix && make format-check && make test
git add src/doc_macros.cpp test/sql/doc_macros.test
git commit -m "refactor(macros): doc_toc/doc_section/doc_search operate on blocks"
```

---

### Task B3: Delete the dispatch macros

**Files:**
- Modify: `src/doc_macros.cpp`
- Test: `test/sql/doc_macros.test`

**Interfaces:**
- **Removed:** `doc_to_blocks`, `doc_read`, `doc_default_extension_mappings`, `doc_supported_extensions`, `doc_is_supported`, `doc_select`, `doc_select_blocks`, `db_quote`.
- **Kept:** `doc_render`, `doc_toc`, `doc_section`, `doc_search`, `profile_table`, `profile_file`, and the `db_ensure_extension` C++ scalar (`doc_render` needs it).

- [ ] **Step 1: Write the failing tests**

```
# ============================================================================
# dispatch has moved to panduck — these must no longer exist here
# ============================================================================

statement error
SELECT doc_to_blocks('README.md');
----
Catalog Error

statement error
SELECT doc_is_supported('a.md');
----
Catalog Error

# but the utilities remain
query I
SELECT db_ensure_extension('json');
----
true

query I
SELECT count(*) FROM profile_table('SELECT 1 AS a');
----
1
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test 2>&1 | grep -A5 doc_macros`
Expected: FAIL — the macros still exist, so no error is raised.

- [ ] **Step 3: Delete the macros**

Remove from the `DBSQL` literal in `src/doc_macros.cpp`: `db_quote`, `doc_to_blocks`, `doc_read`, `doc_select_blocks`, `doc_select`, `doc_default_extension_mappings`, `doc_supported_extensions`, `doc_is_supported`.

Keep the `DocMacrosQuery` auto-load preamble for `json` and `markdown` (used by `doc_render`), but **drop the `sitting_duck` auto-load** — nothing here reads code any more.

- [ ] **Step 4: Build, test, commit**

```bash
make && make format-fix && make format-check && make test
git add src/doc_macros.cpp test/sql/doc_macros.test
git commit -m "refactor!: move reader dispatch to panduck; doc_* macros take blocks

BREAKING CHANGE: doc_to_blocks, doc_read, doc_select, doc_is_supported and
doc_supported_extensions are removed. Use panduck_read() and panduck_can_read().
doc_toc/doc_section/doc_search now take blocks, not paths."
```

---

### Task B4: Documentation

- [ ] **Step 1: Update the README**

The features list advertises "Pandoc AST conversion", "ANSI terminal rendering" and "Page composition & query tables". Rewrite the framing to: this extension defines the `duck_block` vocabulary, owns it through validation, and provides construction, query and rendering utilities over it. Format-specific reading and writing live in the format extensions; path→blocks dispatch lives in panduck.

- [ ] **Step 2: Document the migration**

Add a short section mapping each removed macro to its replacement:

| removed | replacement |
|---|---|
| `doc_to_blocks(path)` | `panduck_read_blocks(path)` — scalar, same `LIST(duck_block)` shape |
| | or `FROM panduck_read(path)` — table form, streams; prefer this for large documents |
| `doc_read(path, output_format := X)` | `doc_render(panduck_read_blocks(path), X)` |
| `doc_is_supported(path)` | `panduck_can_read(path)` |
| `doc_supported_extensions()` | `panduck_supported_paths()` |
| `doc_select_blocks(path, sel)` | `panduck_select_blocks(path, sel)` |
| `doc_toc(path)` | `doc_toc(panduck_read_blocks(path))` |

Note the pairing: `doc_to_blocks` was a scalar taking a path, which is the one shape the
portfolio otherwise avoids. It splits into a table function for the path (`panduck_read`,
matching `read_markdown_blocks`/`read_rtf_blocks`) and a scalar for the list
(`panduck_read_blocks`, matching `db_blocks_toc`/`duck_blocks_to_md`). Existing
`LIST`-shaped call sites port with a rename; new code that filters or limits should use the
table form and let the predicate push down.

- [ ] **Step 3: Commit**

```bash
git add README.md docs/api_reference.md
git commit -m "docs: reframe as vocabulary + utilities; document the panduck migration"
```

---

## Phase 2 Done When

- Both repos: `make test` passes.
- `panduck_read('README.md')` returns blocks; `panduck_read('x.hpp')` routes to sitting_duck; `.rtf` routes to `read_rtf_blocks`.
- Unroutable extensions, data formats and non-Pandoc `.json` all raise messages naming the file and the reason.
- No extension `panduck_supported_paths()` advertises is unroutable.
- `duck_block_utils` contains no path-taking macro and no extension→reader mapping.
- `doc_render(blocks, 'md')` returns markdown; `'html'` returns HTML.

## Next

Phase 3 relocates `pandoc_block_convert.cpp` + `pandoc_inline_convert.cpp` (2,120 lines) to panduck, guarded by the alignment harness built in Phase 1. Not planned yet — write it after Phase 1 lands, so the harness exists to prove the move is safe.
