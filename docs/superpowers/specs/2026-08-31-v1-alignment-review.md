# Staying close to v1.0: what drifted, what was approved, what to pull back

**Date:** 2026-08-31
**Prompted by:** Teague — "look back at v1.0 for guidance. we should be staying close to v1 where we can."
**Sources:** `git show v1.0.0:docs/duck_blocks_spec.md` (267 lines, internally versioned `0.4.0`)
against `main` @ b7ae268 (625 lines, SPEC_VERSION 3.0).

## v1.0 contradicted itself on `level`, and the ruling picks v1's own type definition

Three statements in one document:

| v1.0 line | says |
|---|---|
| 19 — the **type definition** | `level INTEGER -- Structural nesting depth (NOT heading level)` |
| 181 — block validation | "`level` is NULL for headings" |
| 190 — inline validation | "`level` >= 1" |
| 145-148 — level semantics | per-type: inlines from 1, blockquote = quote nesting, headings NULL, "other blocks: typically NULL" |

So v1.0 simultaneously required inlines to carry a level ≥ 1 and headings to carry NULL,
while its type definition said the field is structural nesting depth full stop.

**3.0 is closer to v1's type definition than v1's own validation rules were.** Teague's
"this was always the rule, even in spec v1" matches line 19 exactly. Lines 145-148 and 181
are the looseness that produced a year of drift — they are where "typically NULL" came
from, and they are what four extensions implemented.

Nothing to pull back here. 3.0 is the v1-faithful reading.

## Checked against the LATEST v1 tag, not just v1.0.0

Teague: "make sure to look at the most recent tag, to make sure there were no bugfixes
since v1.0.0." Done. Ten tags, v1.0.0 (2026-01-02) through v1.6.1 (2026-08-30), 47 commits.

**The spec document is BYTE-IDENTICAL across the whole v1 line.** `git diff v1.0.0 v1.6.1 --
docs/duck_blocks_spec.md` is empty. So there were no spec bugfixes to miss, and the review
above covers the shipped v1.6.1 contract, not a superseded draft.

The CODE moved, though, and three of those commits land in exactly what changed this
session. All verified still holding at `87a3254`:

| commit | fixed | still holds |
|---|---|---|
| `80545cd` | `li('text')` — list item with simple string content — converting to Pandoc; previously only items with inline children converted and string content was LOST | ✅ `[{"t":"BulletList","c":[[{"t":"Plain","c":[{"t":"Str","c":"simple text"}]}]]}]` |
| `d848846` | OrderedList format, and **checking both `list_type` and `ordered`** | ✅ both spellings export to OrderedList |
| `e24a885` | nested level preservation at arbitrary depth — overloads were flattening every child to level 2 | ✅ `paragraph@1 > bold@2 > italic@3 > strikethrough@4` |
| `921729c` | blockquote conversion using inline children | ✅ |
| `8145df6` | table conversion preserving full content | ✅ round trip byte-identical |
| `111b42b` | nested list conversion | ✅ |
| `ddb02eb` | rich inline content through lists | ✅ |
| `12898dd` | `LIST(LIST(duck_block))` overloads | ✅ both registered |
| `0263b0e` (v1.5.0, newest) | headings/toc/to_text falling back to inline children; Code and Math surviving as structured children; Link/Image alt inlines not lost | ✅ all three |

**Two of these are evidence FOR the restorations rather than against them.**

`80545cd` explicitly fixed a list item carrying its text in `content` so that it exports —
which means v1's content rule was load-bearing in shipped code, not merely documented. The
2.0 broadening that replaced it was the anomaly, and panduck reports the same independently:
their fix for the tight/loose ruling was DELETING a branch added this morning, because their
reader already implemented v1's rule before any of today's churn.

`d848846` established reading BOTH `list_type` and `ordered` in v1.1.0, back in January. So
the dual name is v1-era and long-tolerated; declaring `ordered` canonical formalises what
the exporter already did rather than inventing a rule.

`e24a885` is the level one, and it points the same way as the type definition: the v1 line
was actively fixing bugs where levels FAILED to preserve relative depth. A codebase spending
commits on depth preservation is not one that believed depth was optional.

## The heading fallback: a deliberate reversal of v1, and it should be recorded as one

v1.0 lines 55-57:

> **Producers** MUST set `attributes['heading_level']` for heading elements.
> **Consumers** SHOULD check `attributes['heading_level']` first, then fall back to `level`
> field for backward compatibility with older data.

duckdb_markdown's writer does exactly this. It was not improvising — it was following the
spec.

That fallback was safe only while headings carried NULL in `level`: a number there could
only be a rank. Under 3.0 `level` is always structural depth, so the fallback reads a
heading inside two containers as `h3`. b7ae268 now warns and the spec says do not do it.

**This is the one place we deliberately depart from explicit v1 guidance.** It is
necessary, but it should be stated as a reversal rather than presented as though v1 agreed.

## Real departures from v1, and which were approved

| | v1.0 | now | status |
|---|---|---|---|
| `list` representation | `encoding: json`, items array in content | `list` → `list_item` → blocks | **approved** this session ("emit real children") |
| list orderedness attribute | `ordered` | both `ordered` and `list_type` | **drift** — see below |
| container `content` | populated **iff** single text child | block containers never carry content | **over-broad** — see below |
| `kind` values | block, inline | block, inline, value | **approved** ("kind could be block\|inline\|value?") |
| block types | 10 | 19 | mostly **approved** (page_break, section, figure, caption, generic); `list_item`, `div`, `deflist`, `lineblock` predate this session |
| heading rank fallback | consumers SHOULD | consumers MUST NOT | **deliberate reversal**, above |

### Drift worth correcting: `list_type` vs `ordered`

v1.0's list attribute is `ordered`. `list_type` arrived with the Pandoc reader and nothing
ever declared which is canonical, so today both are emitted and consumers read either.
That is two names for one fact — the same class of defect as two shapes for one
element_type, which 2.0 exists to remove.

**Recommendation:** declare `ordered` canonical per v1, keep `list_type` as a documented
alias that producers may emit and consumers must tolerate. Costs nothing, removes an
unnecessary divergence from v1.

### Over-broad: "a container carries no content of its own"

Spec 2.0 says block containers never carry content. v1.0 says something narrower and, on
review, better:

> **`content` is populated if and only if the container has a single text child.**

That rule is already deterministic — one shape per situation, which is what 2.0 was
actually trying to achieve. It covers inline and block containers with one sentence and no
per-type table.

The problem 2.0 solved was `list` carrying a JSON items array while the Pandoc reader
emitted children — genuinely two representations of one thing. v1's content rule was never
the cause of that; the `encoding: json` list was.

**So 2.0's container rule went further than the defect required.** Under v1's rule,
`duck_block_blockquote('x')` → blockquote with `content='x'` is correct, and a blockquote
whose child is a paragraph correctly has empty content. Both are the same rule, not two
shapes.

**RESOLVED — Teague ruled: restore v1's rule.** Landed in the same session. A string
argument is a single text child and lands in `content`; a list of children leaves content
empty and nests at level+1. Blocks and inlines now share one rule again. The real 2.0 win —
`list` being structural rather than a JSON items array — is unaffected and stands.

The containment lint had to move with it: it was flagging every conforming v1-shaped
container, because it was written while 2.0 said containers never carry content. A lint
encoding a superseded rule reports correct data as broken, which is the third instance of
that shape today.

## Why this is the strongest explanation of the whole episode

duckdb_markdown's framing, and it is better than any per-defect account: a single file
contradicted itself three ways on `level`, and **every implementation that diverged was
conforming to one of the three**. panduck derived its model from lines 145-148 and read them
correctly. webbed tracked line 19 and was right for a reason it could not have articulated.
duck_block_utils normalised to NULL from the block table. Nobody was careless — the spec
licensed all of it.

That is why the fix is not "be more careful" but "make the checker able to object": the
validator never inspected `level` at all, so no amount of care would have surfaced the
disagreement.

## Open: tight vs loose list items

Discovered while ruling on panduck's question. Pandoc encodes the tight/loose list
distinction as `Plain` vs `Para` inside a list item; **this reader maps both to
`paragraph`**, so the distinction is lost. webbed loses it independently by a different
mechanism — `ListItemsToJson` flattens every item to its text, so the `<p>` vanishes before
anything downstream sees it.

Two readers, two mechanisms, same loss, neither aware. That is the argument for putting the
answer in the vocabulary rather than in either reader: a per-reader fix has each inventing
its own representation, which is the divergence 2.0 and 3.0 exist to kill.

**Recommendation: an ATTRIBUTE, not a new element_type.** The parallel to `heading_level`
is the weaker half of the argument — precedent alone would equally justify minting a type.
The stronger half is panduck's: an attribute is invisible to a consumer that does not look
for it, so a renderer ignoring it produces slightly wrong SPACING rather than wrong content.
That is the correct failure mode for this distinction specifically, and it would not be for
one where ignoring it changes meaning.

Not acted on — minting vocabulary immediately before an external release rename is Teague's
call, not mine.

## External release naming

The v1.0 tag ships a spec whose own header reads `**Version:** 0.4.0`, and it still reads
that at v1.6.1 — so the doc version and the release tag have never matched in this repo.
The extension releases are `v1.0.0` through `v1.6.1`; `SPEC_VERSION` is a separate axis
that reached 3.0 today.

"3.0" is the internal code name. The external name is undecided and is Teague's.
Whatever it is, the two axes should be named distinctly enough that a consumer asserting
`duck_block_spec_version()` cannot confuse it with the extension release it is installed
from — that confusion is what the SPEC_VERSION contract in the header exists to prevent.
