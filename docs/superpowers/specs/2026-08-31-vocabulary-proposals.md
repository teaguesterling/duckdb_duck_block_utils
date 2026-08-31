# duck_block vocabulary proposals

**Date:** 2026-08-31
**Status:** proposals for review — none implemented
**Context:** raised while completing Phase 1 of
`2026-08-31-pandoc-gaps-and-reader-dispatch-design.md`

Four questions came up during the converter work. Each is answered below with what was
measured, not what seemed likely. Two are worth doing, one is a bug in another repo rather
than a vocabulary gap, and one is worth doing but only in a weaker form than proposed.

---

## 1. `kind = 'value'` — RECOMMENDED

**The defect it fixes is real and reproduced.** All document metadata is destroyed on
round-trip today:

```
input frontmatter:   title, tags: [one, two], author: {name, affil}, draft: true
pandoc Meta:         {"title":{"t":"MetaInlines",...},"tags":{"t":"MetaList",...},
                      "author":{"t":"MetaMap",...},"draft":{"t":"MetaBool","c":true}}
after round-trip:    {}
```

`duck_blocks_to_pandoc_ast(pandoc_ast_to_blocks(doc)).meta` returns `{}`. Title, tags,
author and draft are all gone. This is a fifth member of the same family as the four gaps in
`PANDOC_AST_GAPS.md`, and arguably the most damaging: metadata is what drives titles,
authorship, dates and citations in any real pipeline.

**Why the current model cannot hold it.** Pandoc's `Meta` is a recursive `MetaValue` tree —
`MetaMap`, `MetaList`, `MetaInlines`, `MetaBlocks`, `MetaBool`, `MetaString`. A
`LIST(duck_block)` has nowhere to put a tree of non-prose values. `duck_blocks_to_pandoc_ast`
accepts `meta` as `MAP(VARCHAR, VARCHAR)`, which cannot express nesting, lists, or booleans.

**Proposed shape**, reusing the `level`-nesting mechanism `figure`/`caption` already use:

| MetaValue | kind | element_type | carries |
|---|---|---|---|
| `MetaString` | `value` | `string` | `content` |
| `MetaBool` | `value` | `bool` | `content` = `'true'`/`'false'` |
| `MetaList` | `value` | `list` | children at `level+1` |
| `MetaMap` | `value` | `map` | children at `level+1`, each with `attributes['key']` |
| `MetaInlines` | `value` | `inlines` | normal `kind='inline'` children |
| `MetaBlocks` | `value` | `blocks` | normal `kind='block'` children |

**Why this is safe to add.** It is **additive, not breaking**. `RenderDocument` already does
`if (kind != BlockTypes::KIND_BLOCK) { continue; }`, so every existing consumer that iterates
blocks ignores `value` elements automatically. Nothing has to change to tolerate them; only
code that *wants* metadata has to learn them.

**The one real cost:** `blocks[1]` is no longer guaranteed to be the first content block if
value rows are interleaved. Mitigation is to append metadata at the **end** of the list, and
to say plainly in the spec that consumers must filter on `kind` rather than index blindly —
which is already true for inlines and is merely less obvious today.

**Open question worth deciding before implementing:** whether `value` should also become the
home for tabular data (table cells, query results). I would say **no** — that pulls
duck_blocks toward being a general data model, which DuckDB already is, and the documents-vs-
data split from the dispatch work argues for keeping the boundary. Restrict `value` to
document metadata.

---

## 2. `page` — RECOMMENDED, as a marker

**Not currently expressible, and there is already a workaround in the wild proving the need.**
duckeye's PDF path synthesises `## Page N` **markdown headings** (`duckeye:900-907`) to mark
page boundaries.

That is actively harmful rather than merely ugly: it injects *physical* pagination into the
*semantic* heading structure. `doc_toc` then lists "Page 1, Page 2, …" as though they were
sections, and `doc_section` will happily slice on a page boundary.

Formats that need it: PDF (pages are physical), DOCX (explicit page breaks), and EPUB spine
boundaries at a stretch.

**Marker, not container.** A zero-content boundary element like `hr`, rather than a container
owning its blocks. `element_order` already gives grouping ("blocks between marker N and N+1"),
and a marker avoids re-nesting an entire document one level deeper. `attributes['page_number']`
carries the number.

**Pandoc has no page constructor**, so this exports as `Div` with a class — which is the
pandoc-idiomatic spelling anyway. That is a fair argument that `div` + `attributes['role']`
would suffice without a new type. The counter-argument, and why I still recommend the type: a
TOC builder and a section slicer must *ignore* pages, and they cannot know to do that if pages
are indistinguishable from every other div.

---

## 3. Sections (`<section>`, `<main>`, `<aside>`, `<article>`) — NO NEW TYPE

This is **a bug in `duckdb_webbed`, not a gap in the vocabulary.** Measured:

| element | pandoc | webbed |
|---|---|---|
| `<main>` | `Div kv=[role=main]` | flattened |
| `<section id="s1">` | `Div id='s1' classes=['section']` | flattened |
| `<aside class="note">` | flattened | flattened |
| `<article>` | flattened | flattened |

Pandoc preserves `main` and `section` as Divs carrying `id`/`class`/`role`. **webbed flattens
all four and discards `id="s1"` and `class="note"` entirely**, so an aside emerges
byte-identical to body text.

`div` already exists and is exactly what pandoc uses, so it round-trips for free. Adding
`TYPE_SECTION` would fragment the vocabulary without improving fidelity.

**Recommendation:** file this against `duckdb_webbed` — emit `div` with the tag name, `id` and
`class` preserved. Same defect shape as `PANDOC_AST_GAPS.md`, one repo over. Optionally
preserve `<aside>`/`<article>` too, where pandoc drops them; pandoc is a reference, not ground
truth.

---

## 4. Version frontmatter — RECOMMENDED, but OPTIONAL not REQUIRED

**Current state:** no version concept exists anywhere in the vocabulary or spec. Validation
does **not** check `element_type` against a known set — it only special-cases `heading` — so
today's three new types (`figure`, `caption`, `generic`) did not break validation. That is
good news for compatibility and bad news for detection: validation will not catch a version
mismatch either.

**Why it matters now:** five separately-versioned extensions emit duck_blocks
(`duck_block_utils`, `markdown`, `webbed`, `sitting_duck`, `panduck`). An older renderer
meeting a `figure` block renders nothing — silent degradation, the exact class of failure
this work has been removing.

**Why optional, not required:** making a header row mandatory shifts every `blocks[1]`, breaks
every existing test, and forces every consumer to skip it — a cost paid on every list to
protect a boundary most lists never cross.

**Why in-band at all, rather than just a function:** a `duck_block_spec_version()` scalar
reports the *producer's* version at runtime, but duck_blocks get **persisted** —
`test/fixtures/sample_document.parquet` is already in the repo. A parquet written today and
read in two years has no version and no function can recover it. Only an in-band marker
solves that.

**Recommendation: both.**
- `duck_block_spec_version()` scalar for runtime negotiation.
- An optional `value`/`version` element (see §1) for anything persisted or exchanged across
  extension boundaries. Using `kind='value'` rather than a `metadata` *block* keeps it out of
  the block stream entirely, which removes the indexing objection above.

**The consistency argument is the strongest one.** `duck_blocks_to_pandoc_ast` already emits
`pandoc-api-version`, pandoc *rejects* mismatches loudly, and Phase 1 Task 8 existed precisely
because ours had gone stale and made every export unreadable. Demanding a version handshake
from pandoc while offering none for our own vocabulary is hard to defend.

---

## Suggested order

1. **`kind='value'` + metadata round-trip** — fixes a reproduced data-loss bug, and subsumes
   the version marker.
2. **`duck_block_spec_version()`** — trivial, and useful immediately.
3. **webbed section bug** — separate repo, no coordination needed with this one.
4. **`page`** — needs the PDF reader to emit it to be worth anything, so it pairs naturally
   with dispatch work.
