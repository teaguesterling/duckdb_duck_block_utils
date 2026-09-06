# Extractor reshape (spec 6.5): blocks in, blocks out, with projection siblings

**Status: RULED by Teague 2026-09-05 (first-hand: lands in the current PR), design
details owned here.** Written before code. Consumers consulted: panduck (doc_toc binds
duck_blocks_toc's struct), duckeye (-S/-s compute spans from heading element_order,
-t reads toc_rows), markdown and webbed (no call sites on the reshaped functions).

## What changes and why

Retrieval functions that hand back a bespoke projection are terminal: their output
cannot be sliced, rendered or re-emitted. The extractors already moved
(`f047b3e`, `0eb9e47`, `e2341be`: get_section, sections_like, get_pages return
`LIST(duck_block)`; page_rows carries blocks; `output_format` is gone). This finishes
the surface with one rule: **the base name returns blocks; a suffix names what the
ORIGINAL returned.**

| Function | Base name now returns | Sibling, original behaviour |
|---|---|---|
| `duck_blocks_get_section(blocks, pattern)` | `LIST(duck_block)` (already) | `duck_blocks_get_section_text(...)` → VARCHAR |
| `duck_blocks_get_pages(blocks, first, last)` | `LIST(duck_block)` (already) | `duck_blocks_get_pages_text(...)` → VARCHAR |
| `duck_blocks_sections_like(blocks, term)` | TABLE(section, start_order, blocks) (already) | `duck_blocks_sections_like_text(...)` → TABLE(section, start_order, content VARCHAR) |
| `duck_blocks_headings(blocks)` | `LIST(duck_block)` **new** | `duck_blocks_headings_structs(blocks)` → today's STRUCT(level, title, id, element_order)[] |
| `duck_blocks_toc(blocks)` | `LIST(duck_block)` **new** | `duck_blocks_toc_structs(blocks)` → today's STRUCT(level, title, id, indent, element_order)[] |
| `duck_blocks_code_blocks(blocks)` | `LIST(duck_block)` **new** | `duck_blocks_code_blocks_structs(blocks)` → today's STRUCT(language, content, element_order)[] |
| `duck_blocks_links(blocks)` | `LIST(duck_block)` **new** | `duck_blocks_links_structs(blocks)` → today's STRUCT(href, text, title, element_order)[] |

`duck_blocks_toc_rows` and `duck_blocks_page_rows` are unchanged in shape (toc_rows is
an index, not an extract: ruled 2026-09-04) and are rebuilt on the `_structs` forms.
Every internal macro that read `duck_blocks_headings(b).title` moves to `_structs`.

**The `_text` siblings are the original behaviour by construction, not by copy.** The
pre-reshape `'text'` branch was literally `duck_blocks_to_text(sliced)`, so
`duck_blocks_get_section_text(b, p)` is defined as
`duck_blocks_to_text(duck_blocks_get_section(b, p))` and is byte-identical to what the
old default produced. Same for pages and sections_like.

**The `_structs` siblings are today's functions under a new name, byte-for-byte.** They
are the ERGONOMIC form (href, title, language as columns) and are permanent, not a
compatibility shim: a shipped consumer (panduck's `doc_toc`) projects all five toc
columns, and duckeye's -S/-s/-t read the projections.

## The blocks forms are CONSTRUCTIONS with one lossless exception

Teague: *"flattening when there's nested inline elements and similar polish"*; *"the
element_order should allow recovery of the original version from the ergonomic"*.

- **`duck_blocks_headings`** builds one block per heading: `kind='block'`,
  `element_type='heading'`, `content` = the heading's flattened text (the same
  content-or-inline-children rule the projection uses today), `level` = the source
  block's `level`, `encoding='text'`, `attributes` = the source's attributes with
  `heading_level` guaranteed present and **`outline`** added (below),
  `element_order` = the source heading's, unrenumbered. Inline children are NOT
  carried: the flattened `content` replaces them, which is the "polish" and what
  makes the form usable standalone.
- **`duck_blocks_toc`** = the headings blocks plus `attributes['indent']` (heading
  level minus the document's minimum heading level, as the projection computes it).
  It is deliberately a superset of `duck_blocks_headings` rather than a pretence of a
  different thing; the projection sibling is where `indent` is a column.
- **`duck_blocks_code_blocks`** returns the `code` blocks as they are (kind `block`,
  `element_type='code'`), `element_order` preserved. Implemented as a filter; if a
  polish (e.g. normalising `language`) is ever wanted it becomes a construction here,
  and the `_structs` projection is unaffected either way.
- **`duck_blocks_links`** is the lossless exception: it returns the source elements
  that carry a URL — inline `link` elements and block/inline `image` elements — **as
  they are**, `element_order` preserved. Constructing a synthetic `link` for an image
  would lose the fact that it was an image; the projection sibling is where `href`
  unifies `href` and `src`.

Heading `id`: stays where the source put it, `attributes['id']`; the projection
reads it from there today and continues to.

## `attributes['outline']` — a computed attribute, deliberately

`ATTR_OUTLINE = "outline"`, value like `"1.2.1"`, set on every block the headings and
toc forms return. Never NULL, never padded. Rule: **outline positions are positions in
the outline, not the heading's own digit.**

Maintain a stack of `(heading_level, counter)`. For a heading at level L: pop entries
with level > L, remembering the last popped counter `c` (or 0 if none popped); if the
top has level == L, increment it; otherwise push `(L, c + 1)`. The outline is the
counters joined with `.`.

```
h1 A        -> 1
h3 B        -> 1.1        (skipped level: B is A's first child, whatever its digit)
h2 C        -> 1.2        (shallower than B, deeper than A: A's second child)
h2 D        -> 1.3
h3 E        -> 1.3.1
h1 F        -> 2
```

This is the first attribute a duck_block_utils function COMPUTES rather than copies
from a source. Recorded as precedent: computed attributes are legitimate on the output
of a construction, must be named in the vocabulary, and must never be emitted by a
reader as if sourced.

## The recovery contract (spec text, normative)

`element_order` is dense from 0 over the list a reader EMITS, including synthetic
elements the reader inserts (page_break markers). That list is the source document for
every consumer. Functions that **project or construct from** a document — headings,
toc, code_blocks, links, their `_structs` siblings, slice, get_section, get_pages,
sections_like — carry `element_order` through **unrenumbered, with gaps**; it is the
join key back to the document and between the blocks and `_structs` forms of the same
call. Functions that **build a standalone document** — assemble, merge, reorder —
renumber, and say so. duckeye computes section spans from heading `element_order` and
slices with them: a renumbering there would not fail to bind, it would return the
wrong span silently, which is why this is asserted on a sparse document rather than
documented.

## Not changed

`duck_blocks_page_rows` on a markerless document returns ZERO rows (duckeye: "no page
information" and "one page" are different facts). Its end sentinel stays INT_MAX. No
propagation utility is built (duckeye does not read page numbers off content blocks;
panduck drops the per-block attribute, timing coordinated with duckeye).

## SPEC_VERSION 6.4 → 6.5 (public name still duck_blocks v1.1)

Return types changed on get_section / sections_like / get_pages (VARCHAR →
LIST(duck_block)); `output_format` removed; page_rows gains `blocks`; headings / toc /
code_blocks / links return blocks; eight siblings added; `ATTR_OUTLINE` added; the
recovery contract stated; `level` never NULL and `filename` boolean-only restated.
Nothing in the STRUCT shape changes.

## Tests (TDD order)

1. `duck_block_spec_version()` = 6.5.
2. `_structs` siblings equal today's output byte-for-byte on the existing extraction
   fixtures (the existing assertions move to the `_structs` names unchanged).
3. `_text` siblings equal `duck_blocks_to_text` over the blocks form.
4. Blocks forms: typeof is the 7-field list; `element_order` equals the source's on a
   document with deliberately sparse orders (0, 5, 17, 40); headings `content` is the
   flattened title including a heading with inline children; toc adds `indent`;
   code_blocks keeps `language`; links returns the image element as an image.
5. `outline` on the h1/h3/h2/h2/h3/h1 sequence above.
6. `doc_toc`-shaped query over `duck_blocks_toc_structs` returns the five columns.
7. Pins: page_rows zero rows on markerless input; INT_MAX sentinel; #21, #22, #23.
8. Discriminating negative: `duck_blocks_toc(b)` fed to a projection consumer
   (`(t).title`) fails to bind — the loud failure, kept on purpose.

## Added during implementation (ruled by Teague 2026-09-05)

- **`duck_blocks_to_match_text(blocks)`** = `duck_blocks_to_text(blocks, ' ')`. Tiiny's
  proposal: the two to_text jobs (render with a blank line, match with a space) get two
  names so a wrong choice reads wrong on sight; sections_like's predicate moves onto it
  and no bare separator literal remains in src. The `_text` siblings use the render form.
- **`duck_blocks_get_section` on a correlated table column** threw `INTERNAL Error:
  inequal types (INTEGER != BIGINT)`, on the published release too. Bisected to the
  `NOT EXISTS` anti-join that kept outermost sections; replaced with a window over
  sections ordered by start, which agrees with the anti-join on every value input and
  decorrelates. Tested on a bare column.
- **RawInline "split"** (duckeye): `Text with <b>raw</b> inline.` decodes to
  raw / text / raw. Measured: that is pandoc's own AST (`RawInline html "<b>"`, `Str
  raw`, `RawInline html "</b>"`), and each raw element carries `attributes['format'] =
  'html'`. The decode is faithful; nothing to fix.

