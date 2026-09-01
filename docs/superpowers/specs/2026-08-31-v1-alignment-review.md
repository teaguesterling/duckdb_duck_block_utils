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

## External release naming

The v1.0 tag ships a spec whose own header reads `**Version:** 0.4.0` — so the doc version
and the release tag have never matched in this repo. The extension releases are `v1.0.0`
through `v1.6.1`; `SPEC_VERSION` is a separate axis that reached 3.0 today.

"3.0" is the internal code name. The external name is undecided and is Teague's.
Whatever it is, the two axes should be named distinctly enough that a consumer asserting
`duck_block_spec_version()` cannot confuse it with the extension release it is installed
from — that confusion is what the SPEC_VERSION contract in the header exists to prevent.
