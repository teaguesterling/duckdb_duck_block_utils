# Pandoc AST gaps and reader-dispatch ownership

**Date:** 2026-08-31
**Status:** approved, pending implementation plan
**Branch at design time:** `feat-doc-query-pipeline` @ `9165076`

## Problem

Two independent defects, verified against the built binary, that share one root cause:
**work is discarded without a trace, and one mapping is maintained in two places.**

### A. `pandoc_ast_to_blocks()` silently drops content

Four pandoc constructors are lost with no error, no warning, and no placeholder row.
Reported in `PANDOC_AST_GAPS.md`; all four reproduce.

| Constructor | Direction | Symptom |
|---|---|---|
| `Figure` | import | Block dropped entirely — image and caption both lost |
| `LineBlock` | import | Block dropped entirely |
| `DefinitionList` | import | Block dropped entirely |
| `Underline` | import + export | Text replaced by a placeholder, or lost outright |

Coverage is 11/15 block and 19/20 inline constructors of pandoc-types 1.23 (`Null` is a
legitimate skip). Root cause is the bare `else { return; }` ending the dispatch chain at
`src/pandoc_block_convert.cpp:306`, and the `"[" + pandoc_type + "]"` fallbacks at
`src/pandoc_inline_convert.cpp:240` (import) and `:541` (export).

`Figure` is the most damaging because pandoc 3.x wraps *every standalone captioned image*
in one, and because duckeye routes thirteen formats — `.docx .odt .epub .rst .org .tex
.ipynb .rtf .textile .man .mediawiki .1-.9` — through `pandoc(1)` into
`pandoc_ast_to_blocks()`. Markdown is the only format that bypasses this path, which is
why the gap survived testing.

Confirmed beyond the original report: nested figures are dropped too. `Div` is the one
container that recurses, and a `Figure` inside a `Div` vanishes while its sibling
paragraph survives. `BlockQuote` and lists fail differently — they store children as
opaque `encoding='json'`, so a nested `Figure` is preserved but never becomes a block.

Separately, `duck_blocks_to_pandoc_ast()` stamps `pandoc-api-version` `[1,20]`
(`pandoc_block_convert.cpp:1038`). Pandoc 3.x rejects everything below `[1,23]` outright,
so **every export is unreadable by the installed pandoc**. duckeye works around this in
shell (`duckeye:272-278`).

### B. `doc_macros.cpp` maintains one format mapping in two places

Commit `9165076` made `doc_supported_extensions()` dynamic off `ast_supported_languages()`,
but `doc_to_blocks` still dispatches on a hardcoded `LIKE '%.py' OR ...` chain. They
drifted within four days:

| Claim | Reality |
|---|---|
| `doc_is_supported('x.hpp')` → `true` | `doc_read('x.hpp')` → `Invalid Input Error: File is not a markdown file` |
| same for `.h .hh .hxx .cxx .c++ .h++ .toml .tf .tfvars .hcl .pyi .pyw .kts .ruby .bash .zsh .mjs .php3/4/5 .phtml .graphql .r .rlib` | all fall through to `read_markdown_blocks` |
| `doc_default_extension_mappings()` lists `panduck` → `.docx .odt .epub .tex .rst .org .wiki` | no panduck branch exists in `doc_to_blocks`; panduck ships **no readers at all** (its README: "Phase 1 scaffolding") |

Further defects in the same file, all verified:

- **`.json` is unconditionally treated as a pandoc AST.** A plain data file returns `NULL`
  silently. duckeye already solved this by sniffing for `"pandoc-api-version"` in the
  first 4KB (`duckeye:463-465`) — the coordinator is behind its own client.
- **`pages` is a dead parameter**, threaded through `doc_read` → `doc_to_blocks` and never
  read in the body.
- **`output_format:='md'` returns pandoc AST JSON**, byte-identical to `'pandoc'`.
- **No `html` output format exists at all.**
- **`CASE WHEN db_ensure_extension('x') THEN '' ELSE '' END ||`** discards the result, so a
  failed load surfaces downstream as "function not found" rather than naming the extension.

## Architecture decision: who owns reader dispatch

The registry is in the wrong repo. A library that defines a vocabulary should be a leaf
dependency; instead `duck_block_utils` knows about every reader extension that exists
(`markdown`, `webbed`, `pdf`, `sitting_duck`, `panduck`). That inversion is what let the
two mappings drift.

Two concerns were conflated under "the dispatcher", and only one is panduck-shaped:

1. **Path → blocks routing** — which extension reads `.docx`. This is pandoc's identity
   and belongs in `panduck`.
2. **The `doc_*` query surface** — `doc_toc`, `doc_section`, `doc_search`, `doc_render`.
   These do not care where blocks came from. They only became path-shaped because
   `doc_to_blocks` sat in the same file.

**Decision:** split by that line. `duck_block_utils` macros operate on **blocks, not
paths**, and stop knowing that formats exist. Path→blocks routing eventually moves to
`panduck` as `panduck_read(path)`.

**Decision:** the fix for drift is not a better registry — it is **readers describing
themselves**. `sitting_duck` already exposes `ast_supported_languages()`. When every
reader exposes an equivalent, dispatch is *derived* rather than maintained and cannot
drift by construction.

**Decision (superseded — see below):** ~~derive now, relocate later.~~

**Decision, revised 2026-08-31 after review:** build dispatch **in panduck directly**. Do not
build a derived registry here and relocate it; that writes ~270 lines of macros in this repo
solely to delete them. `doc_to_blocks`, `doc_reader_registry`, `doc_format_for`,
`doc_supported_extensions`, `doc_is_supported`, `doc_select*` and `db_quote` are **removed**
from this extension.

**Accepted cost:** reading a file by path now requires `LOAD panduck`, including a `.md`
file. This was the objection that originally motivated "relocate later"; it is accepted
deliberately, because the alternative is maintaining the registry in two places during the
transition — the exact defect this work exists to remove. duckeye should add `panduck` to
`DUCKEYE_BASE`.

### What this extension is, after this work

Teague's framing: *"duck_block_utils is just that: utilities — and it owns the spec through a
validation utility."* Applying it to the current 9,009 lines:

| | lines | disposition |
|---|---|---|
| builders, inline_builders, assembly | 3,306 | **stays** — construction utilities |
| extraction, manipulation, type_functions | 1,297 | **stays** — query utilities |
| validation, block_types, pragma_aliases | 539 | **stays** — spec ownership |
| `render_ansi`, `render_macros` | 1,392 | **stays** — see below |
| `doc_macros` (dispatch) | 268 | **moves to panduck** now |
| `pandoc_block_convert`, `pandoc_inline_convert` | 2,120 | **moves to panduck** after the converter fixes land |

**ANSI rendering stays.** It was considered for relocation and rejected on principle: it is
the one output that is not a *format*. `duck_blocks_to_md` and `duck_blocks_to_html` know
about markdown and HTML and correctly belong to those extensions; `render_ansi` knows only
the `duck_block` vocabulary, making it a utility over the spec in the same category as
`db_blocks_to_text`. Moving it would also mean `LOAD panduck` to render a markdown file. If
it ever moves, the destination is a dedicated renderer extension, not panduck.

**The Pandoc converters move, but only after their gaps are fixed.** They are format-specific
by the same logic that relocates dispatch, and panduck already owns the pandoc model
(`panduck_pandoc_ast_map()`, `panduck_pandoc_api_version()`, the alignment harness). But
moving them first would relocate four known bugs and lose the regression net. Sequence:
fix here (Phase 1) → alignment harness green → relocate as a separate test-guarded move
(Phase 3), with the harness proving nothing broke.

**Decision:** source code stays behind the same dispatcher as documents. duckeye already
treats them uniformly and "one function, any file" is the useful promise.

**Note:** this reverses panduck's roadmap Phase 5, which currently reads "`read_rst_blocks()`,
and the `doc_to_blocks()` hook in `duck_block_utils`". Panduck's plan of record assumes the
registry lives here with panduck plugging in. That must be communicated.

## Design

### 1. Converter: no constructor leaves without a trace

New block-type constants in `src/include/block_types.hpp`:
`TYPE_FIGURE = "figure"`, `TYPE_CAPTION = "caption"`, `TYPE_LINEBLOCK = "lineblock"`,
`TYPE_DEFLIST = "deflist"`, `TYPE_UNKNOWN = "pandoc:unknown"`. New inline constant
`INLINE_UNKNOWN = "pandoc:unknown"`.

**`Figure` — structural, with structured captions.** Pandoc's shape is
`[Attr, Caption, [Block]]` where `Caption = [ShortCaption?, [Block]]`. A figure therefore
carries *two* block lists — caption blocks and content blocks — which a flat `duck_block`
list must keep distinguishable.

**Add `TYPE_CAPTION = "caption"`** and nest it with the existing `level` mechanism, exactly
as `Div` already nests children. No new field on `duck_block`, and no per-child attribute
marker (which would be fragile — every descendant would have to carry it).

```
block  | figure    |                    level=N      attrs from Attr
block  | paragraph |                    level=N+1    <- content blocks
inline | image     | A caption          level=N+2
block  | caption   |                    level=N+1    <- caption container
block  | paragraph | A **bold** caption level=N+2    <- caption blocks, fully structured
inline | bold      | bold               level=N+3
```

- emit the `figure` block with `content` empty (like `Div`) and `Attr` via `StorePandocAttr`
- recurse `c[2]` content blocks at `level+1`
- emit a `caption` block at `level+1`, then recurse the caption's `[Block]` list beneath it,
  so caption formatting — bold, links, code — survives as real inline children
- `ShortCaption`, when present, is stored as `attrs['short_caption']` on the `caption` block
- export splits children on the `caption` child and rebuilds
  `Figure(attr, Caption(short?, caption_blocks), content_blocks)`

**Caption goes last**, after the content blocks. Import/export are symmetric either way, so
this is chosen for consumers: a renderer that walks the flat list without figure-awareness
then emits the image before its caption, which is the correct visual order and how the
markdown source reads. One-line change if that proves wrong.

*Note:* pandoc duplicates short captions — `![cap](img.png)` yields both an `Image` alt of
"cap" and a caption block reading "cap". The model represents both faithfully; de-duplicating
is a renderer concern, not a conversion one.

*Generalizes:* pandoc's `Table` also carries a `Caption`. Table stays opaque JSON in this
work (its caption is preserved inside that JSON), but `caption` is deliberately defined as a
general container so Table can adopt it later without another vocabulary change.

*Verified against pandoc 3.1.3*: `![A **bold** caption with a [link](http://x)](img.png)`
produces `Caption = [null, [Plain [Str, Space, Strong[...], ..., Link[...]]]]`. Under the
flattened design this degraded to `A bold caption with a link`, losing both the emphasis and
the URL — which is what makes the structured form worth the extra block type.

**Renderer consequence.** `figure` and `caption` both carry empty `content`.
`render_ansi.cpp:1125` does `if (lines.empty()) continue;`, so neither emits a stray blank
line — but neither is *visible* either, and the caption's child paragraph then renders as
undistinguished body text. A reader cannot tell a caption from the prose around it.

So `src/render_ansi.cpp` needs two cases: `figure` as a transparent container (children flow
through unchanged), and `caption` styled distinctly — dimmed or italic — so it reads as a
caption. This is required converter work, not a follow-up: the whole reason `Figure` was
rated High severity is that duckeye renders thirteen pandoc-routed formats to a terminal.

**`LineBlock`** — `lineblock` block; lines joined with `\n` in `content`. Rich runs emit
inline children with `linebreak` between lines, mirroring how `Para` already splits
text-only from rich runs.

**`DefinitionList`** — `deflist` block, `encoding='json'`. Follows the existing
`BulletList`/`OrderedList` precedent rather than the `Figure` treatment: its
`[([Inline],[[Block]])]` shape has no flat rendering, and the codebase already keeps lists
as JSON.

**Unknown blocks** — replace `else { return; }` with a `pandoc:unknown` block,
`encoding='json'`, `content` = the original constructor JSON, `attrs['pandoc_type'] = t`.
Document length is preserved and export reconstitutes it verbatim.

**`Underline`** — import beside `Strong`/`Emph`; export added to the styled group at
`pandoc_inline_convert.cpp:343-345`.

**Unknown inlines** — the `"[" + pandoc_type + "]"` placeholder eats the words. Replace
with an `inline|pandoc:unknown` carrying `attrs['pandoc_type']`, then flatten its children
as `Span` does — text survives *and* the gap stays visible. The text-flattening paths
(`ExtractInlinesTextVal`, `RenderInlinesToTextVal`) get the same treatment; that is the
"Case A" silent loss where no marker is left behind at all.

**api-version** — default `[1,23,1]`, plus an `api_version :=` argument on
`duck_blocks_to_pandoc_ast`. Verified: pandoc 3.1.3 accepts `[1,23]` and `[1,23,1]`,
rejects `[1,20]` and `[1,22]`. This deletes duckeye's `pandoc_apiver()` workaround.

### 2. Macros operate on blocks, not paths

Canonical signatures take `LIST(duck_block)`:

- `doc_render(blocks, output_format)`
- `doc_toc(blocks)`
- `doc_section(blocks, pattern, output_format)`
- `doc_search(blocks, term, output_format)`

**Revised 2026-08-31:** with dispatch moving to panduck outright, there is no path form left
in this extension and therefore **no compatibility seam**. The earlier design had a
`doc_as_blocks(x)` helper using `typeof()` to accept either; that is dropped. These macros
take blocks, full stop.

`doc_read` is **removed**. Its path-taking convenience belongs with the reader — panduck
exposes `panduck_read(path, output_format := ...)`. What remains here is `doc_render`, which
takes blocks.

`doc_select` / `doc_select_blocks` are **removed** and move to panduck with dispatch. They
drive `read_ast(path)` + `ast_select_from(...)`, which need a file; there is no block-list
form of that operation, so they cannot be converted to block-taking macros and stay here.

**Breaking change:** `doc_toc('README.md')` stops working. The replacement is
`doc_toc(panduck_read('README.md'))`, or panduck's own path-taking wrapper.

`profile_table` and `profile_file` are untouched by this work.

*Risk:* if `typeof()` proves unreliable under some binder path, fall back to distinct names
(`doc_toc_file` vs `doc_toc`) rather than reintroducing per-macro coupling.

### 3. Output rendering delegated to the format extensions

One `doc_render(blocks, output_format)` replaces the `CASE` currently duplicated across
`doc_read`, `doc_section` and `doc_select`:

| `output_format` | delegate |
|---|---|
| `md` | `duck_blocks_to_md` — markdown extension |
| `html` | `duck_blocks_to_html` — webbed |
| `ansi` | `db_blocks_render_ansi` — this extension |
| `text` | `db_blocks_to_text` — this extension |
| `blocks` | `to_json` |
| `pandoc` | `duck_blocks_to_pandoc_ast` |
| anything else | `error()` naming the valid set |

Both delegates are verified present and working. This repo does not gain a markdown or
HTML writer; format-specific output is the format extension's job.

### 4. Derived dispatch — built in panduck, not here

**Everything in this section is implemented in `~/Projects/duckdb_panduck`, not in this
repo.** It is retained here because it is the design of record and this spec is what the
panduck work argues from. In panduck the entry point is `panduck_read(path, format := 'auto',
pages := '')`; the macro names below (`doc_reader_registry`, `doc_format_for`, …) become
`panduck_`-prefixed equivalents.

This repo's only obligation is to **delete** `doc_to_blocks`, `doc_reader_registry`,
`doc_format_for`, `doc_supported_extensions`, `doc_is_supported`,
`doc_default_extension_mappings`, `doc_select`, `doc_select_blocks`, `doc_read` and
`db_quote`, keeping `doc_render`, `doc_toc`, `doc_section`, `doc_search` on blocks.
`db_ensure_extension` (C++) stays — `doc_render` needs it to delegate md/html.

`doc_to_blocks(path, format, pages)` dispatches on a *format name* from a small closed set
(`markdown`, `html`, `pdf`, `pandoc_ast`, `code`) rather than on extension patterns.

- `doc_reader_registry()` assembles `(extension, format, extension_name)` rows from
  self-describing readers — `ast_supported_languages()` today,
  `panduck_supported_extensions()` when it lands — plus a small static core for
  `markdown`/`webbed`/`pdf`/`json`, which do not self-describe yet.
- `doc_supported_extensions()` = extensions drawn from that registry.
- `doc_is_supported(p)` = `doc_format_for(p, 'auto') IS NOT NULL`. **One source of truth,
  structurally** — the two cannot disagree because both read the same registry.

#### Collisions — sitting_duck is the fallback, never the choice for a known format

Deriving the registry fixes *which extensions are supported*. It does not, on its own,
settle *which reader wins* when two claim the same extension — and they do. Verified
against the installed build:

| extension | claimants |
|---|---|
| `.md`, `.markdown` | `markdown` **and** `sitting_duck` |
| `.html`, `.htm` | `webbed` **and** `sitting_duck` |
| `.json` | pandoc-AST / `json` **and** `sitting_duck` |
| `.toml` | `toml` **and** `sitting_duck` |
| `.css` | `sitting_duck` only — no conflict |

Both claimants are real: `read_ast('README.md')` yields 3331 tree-sitter nodes while
`read_markdown_blocks('README.md')` yields 173 document blocks. They answer different
questions about the same file. The existing hardcoded chain gets this right only *by
accident* of ordering, so a naive union would regress `README.md` to sitting_duck.

**Rule: a format with a native DuckDB reader never goes to sitting_duck.** The registry
filters sitting_duck's contribution down to extensions that no document or data reader
claims — `.py`, `.rs`, `.go`, `.css` and so on. This is categorical exclusion, not a
precedence ranking: there is no `rank` column and no per-extension arbitration, because
after filtering there is nothing left to arbitrate.

`format := 'code'` bypasses the registry entirely and forces sitting_duck, which is how you
ask for the tree-sitter reading of a `.md`, `.html` or `.json` file.

#### Documents vs data

`doc_to_blocks` returns `LIST(duck_block)` and is therefore a **document** entry point.
`.toml`, `.yaml`, `.csv`, `.parquet`, `.xlsx` and non-AST `.json` are data: they have native
readers, they are not documents, and they have no sensible block representation. They are
excluded from sitting_duck by the rule above *and* are not documents, so `doc_to_blocks`
rejects them with a message pointing at `profile_file()` and direct reads.

This mirrors duckeye's existing `is_data_file()` split (`duckeye:468-480`), which already
routes yaml/toml/csv/parquet/xlsx/zip/git to a table renderer and sniffs `.json` to decide.
Adopting the same taxonomy keeps the two dispatchers agreeing rather than diverging.

Resulting document set: `.md`/`.markdown` → markdown; `.html`/`.htm`/`.xml` → webbed;
`.pdf` → pdf; `.json` → pandoc-AST **iff** it sniffs as one, else a data error; `.rtf` →
panduck's `read_rtf_blocks` (its one `implemented` format today), with the remaining panduck
formats joining as they become `implemented`; everything else sitting_duck claims → code.

#### The `doc_select` exception

`doc_select` / `doc_select_blocks` stay on sitting_duck for **all** file types, including
`.md` and `.html`. A CSS-selector query is an explicit request for a syntax-tree view, so
sitting_duck is the right reader even where a document reader owns the extension for
`doc_to_blocks`. `webbed` does expose `html_extract_text(html, selector)`, but it returns
text rather than blocks, so `ast_to_blocks_from` remains the only path producing blocks
from a selection. Not worth special-casing; documented instead.
- **Unroutable → `error('unsupported extension: .xyz')`.** Only `.md/.markdown/.txt` and
  extensionless files fall through to markdown; `format:=` still forces a reader.
  *This is a behavior change* — callers relying on the markdown fallback will now fail loudly.
- **`.json` is sniffed inside the branch**, not in `doc_format_for` (which must stay I/O-free
  for `doc_is_supported`): no `"pandoc-api-version"` → clear error rather than silent `NULL`.
- **`pages` wired** to `read_pdf(first_page:=, last_page:=)`, parsing `N-M` and `N`, as duckeye does.
- **`db_ensure_extension` failures become loud**: `CASE WHEN db_ensure_extension('webbed')
  THEN '' ELSE error('...') END`.
- **panduck removed** from `doc_default_extension_mappings()` until it self-describes.

### 5. panduck coordination — done, uncommitted in the panduck tree

`panduck_supported_extensions()` is implemented, tested and documented in
`~/Projects/duckdb_panduck` (working tree, not committed). Schema:

| column | type | meaning |
|---|---|---|
| `format` | VARCHAR | pandoc's own `--from` name (`docx`, `latex`, `mediawiki`…) |
| `extensions` | VARCHAR[] | lowercase, **no leading dot**, matching `ast_supported_languages()` |
| `reader` | VARCHAR | the panduck table function that reads it — **NULL** until one exists |
| `status` | VARCHAR | `implemented` / `planned` |
| `notes` | VARCHAR | |

Eight rows — docx, odt, epub, latex, rst, org, mediawiki and **rtf**. The first seven are
`planned` with `reader = NULL`; `rtf` is `implemented` with `reader = 'read_rtf_blocks'`.

**Updated 2026-08-31, later the same day:** a concurrent session landed `read_rtf_blocks()`,
panduck's first native reader, after this spec was first written. The original text claimed
panduck shipped no readers and that filtering on `status='implemented'` would "correctly
yield zero panduck rows." That is no longer true — it yields `rtf`.

Two consequences, both of which *validate* the design rather than disturb it:

- **`.rtf` becomes routable through `doc_to_blocks` with no change to this repo.** The
  derived registry picks it up because panduck now describes it. This is precisely the
  property the design was chosen for. duckeye currently sends `.rtf` through `pandoc(1)`;
  it can go native.
- **The "derive now, relocate later" sequencing still holds**, but on updated reasoning.
  It is no longer "panduck ships no readers" — it is that panduck implements one format of
  eight, so requiring `LOAD panduck` to read a `.md` file remains a bad trade. Revisit the
  relocation when panduck covers the bulk of its roadmap, not on the strength of `rtf`
  alone.

The same session also strengthened panduck's own test: rather than asserting a *count* of
implemented readers, it asserts that every row claiming `implemented` names a function
present in `duckdb_functions()`. That is the right invariant — a count restates the
registry instead of checking it, and goes stale on every new reader. This repo should adopt
the same shape for its own registry test (see §6).

**Delegation resolved: panduck claims nothing it does not read itself.** No rows for
markdown or HTML, and no `delegated_to` column. The reasoning is that the table is a
*self-description*, not a routing table — a `delegated_to='markdown'` row would be panduck
holding second-hand knowledge about another extension's formats, which is structurally the
same defect as `doc_default_extension_mappings()` claiming `.docx` for a panduck reader
that never existed. It would relocate the inversion rather than remove it. Absence is
already unambiguous: `planned` means "not yet, but mine"; no row means "not mine."
Delegation is a dispatcher concern, and the dispatcher is derived from N registries each of
which describes only itself.

**What this repo must do to consume it:**

1. **Filter on `status = 'implemented'`.** Today that yields exactly one panduck row —
   `rtf` → `read_rtf_blocks`. A consumer that ignores `status` routes `.docx` to a reader
   that does not exist. The `reader IS NULL ⟺ status <> 'implemented'` invariant is a second
   structural safeguard — forgetting the filter builds a `CASE` branch naming NULL rather
   than a plausible-looking function name.
2. **Prepend the dot** — `'.' || ext`, exactly as already done for `ast_supported_languages()`.
   No new normalisation path.
3. **Remove panduck from `doc_default_extension_mappings()`** immediately; all seven
   extensions are now self-reported.

panduck's README records the roadmap reversal: Phase 5 drops the `doc_to_blocks()` hook
line, and a new Phase 6 is `panduck_read(path)` — panduck taking ownership of dispatch.

### 6. Verification

Copy panduck's `check_pandoc_alignment.py` and fixtures into `test/pandoc/` rather than
depending on the panduck repo. Panduck is also a *consumer* of this extension, so a
test-time dependency would create a cycle. The harness runs real pandoc and skips cleanly
when it is absent.

SQL tests to add:

- one per newly-mapped constructor (`Figure`, `LineBlock`, `DefinitionList`, `Underline`)
- nested `Figure` inside a `Div`
- **`Figure` with a formatted caption** (`![a **bold** caption](img.png)`) round-trips with
  the emphasis intact — the regression test for structured captions
- a `Figure` with no caption emits no `caption` child, and export omits it correctly
- the ANSI renderer emits the image before its caption for a figure
- unknown-constructor passthrough preserves document length
- `Underline` round-trips in both directions
- `[1,23,1]` export accepted by real pandoc
- routing: `.hpp` resolves, non-AST `.json` errors clearly, unknown extension errors
- `doc_is_supported(p)` agrees with `doc_to_blocks(p)` for every extension in the registry
  (the drift regression test)
- **collision exclusion**: `.md` routes to `markdown`, `.html` to `webbed`, `.toml` to the
  data error — even though `sitting_duck` claims all three; `format := 'code'` still forces
  the tree-sitter reading of each
- **the resolved registry maps each extension to exactly one reader** — asserted over the
  whole registry, so a future ambiguous claim fails loudly instead of resolving arbitrarily
- `doc_select('x.md', ...)` still reaches sitting_duck (the documented exception)
- data formats are rejected by `doc_to_blocks` with a message naming `profile_file()`
- **every reader the resolved registry names actually exists** — asserted against
  `duckdb_functions()`, not against a count. A count restates the registry instead of
  checking it and goes stale on every new reader; panduck's own test was rewritten to this
  shape after a count assertion went stale within an hour of being written.
- panduck rows with `status <> 'implemented'` are excluded, and `.rtf` (its one
  `implemented` format) routes to `read_rtf_blocks`
- `doc_render` md/html delegation

Update `docs/pandoc_ast_spec.md:422-424`, which currently advertises `pandoc:lineblock`,
`pandoc:deflist` and `pandoc:figure` as mapped when no code produces them, and which the
`figure` decision above supersedes.

## Sequencing

§1 is independent of §2-4 and is pure gain — no behavior is removed. §2-4 restructure a
public macro surface and include one hard behavior change (loud errors on unroutable
extensions). Land the converter first.

## Out of scope

- A native markdown or HTML writer in this repo — delegated to the format extensions.
- Relocating dispatch into panduck — deferred until panduck ships a reader.
- Migrating `Table` from opaque JSON onto the new `caption` block — the type is defined to
  allow it, but Table conversion is otherwise untouched here.
- Structured-data formats (`.parquet`, `.csv`, `.zip`) — those stay duckeye's concern.
