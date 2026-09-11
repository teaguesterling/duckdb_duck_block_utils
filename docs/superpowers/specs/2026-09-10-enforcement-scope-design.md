# Enforcement scope (issue #29): fragments, list-level validation, one shape for all producers, a conformance corpus

**Status: APPROVED by Teague 2026-09-10 (all four sections) and IMPLEMENTED on `feat/spec-6.6-enforcement-scope` the same day; see the plan in `docs/superpowers/plans/2026-09-10-spec-6.6-enforcement-scope.md`.** Originally: Rulings received 2026-09-10,
first-hand: (1) fragments are valid, and the spec should define optional implicit default
parents per type so a fixer can wrap mechanically; (2) list-level validation, with a fixer
that coerces to valid when deterministic; (3) the one-shape rule re-scoped to all producers,
after a pros-and-cons review with sound rationale, breaking allowed; (4) a shared conformance
corpus, owned here. Nothing implemented.

Issue #29 (duckeye) is right that every validation predicate is per block and every
conformance rule is scoped to this repo, while the divergences consumers hit are per list and
between repos. Three measured examples: markdown starts element_order at 1 (markdown#59,
a plain bug); a tight list item has two shapes in the field; an orphan fragment is valid,
rendered by `to_text`, and silently dropped by `to_pandoc_ast`.

---

## A. Fragments are valid; implicit default parents

**Rule (normative).** Any `LIST(duck_block)` that passes per-block validation is a legal
input to every consumer function, whether or not it is a whole document. A function that
receives a fragment MUST either handle it or wrap it into its implicit parent; it MUST NOT
drop content. `duck_blocks_validate` reporting valid and a writer returning nothing for
the same input is the contradiction this rule removes.

**Implicit default parents, declared in the vocabulary header** so a fixer, a writer and a
sibling's vendored copy all read one table:

| element_type (kind) | requires an ancestor of | implicit parent if absent | level of the wrapper |
|---|---|---|---|
| `list_item` (block) | `list` | `list` with `attributes['list_type']='bullet'` | one less than the items; a run of consecutive orphan items at one level forms ONE list |
| `list_item` with `role` in (`term`, `definition`) | `deflist` | `deflist` | same |
| `caption` (block) | `figure` or `table` | `figure` | same |
| any `kind='inline'` element | a block that carries inlines | `plain` | one less than the run; one wrapper per consecutive inline run |
| everything else | none | none (legal at top level) | — |

Declared as `IMPLICIT_PARENT_OF(type) -> type-or-empty` in `duck_block_vocabulary.hpp`, so
`check_consumer_alignment` sees it and `check_constants_are_used` keeps it honest.

**Why `plain` for an inline run and not `paragraph`:** `plain` is the wrapper the vocabulary
already reserves for "a text run that is not a paragraph" (spec: beside block siblings and at
the top level). Wrapping into `paragraph` would assert a paragraph the source never had, and
export it as `Para`; `plain` exports as `Plain`, which is what pandoc does with bare inlines.

**What changes:** `duck_blocks_to_pandoc_ast` applies the table (an orphan `list_item` run
becomes `BulletList`, an orphan inline run becomes `Plain`). `duck_blocks_to_text` already
renders fragments and does not change. Writers in sibling extensions apply the same table
from their vendored header.

---

**Not a replacement for real ancestors (duckeye, after review):** an implicit parent
carries no attributes, so a selector that keeps the real `list` (with `list_type`,
`start`) around its matches must go on doing so; repair is a no-op on it. Possible
follow-up, not taken: a hint form of the wrapper that takes the real parent when a
caller has one, so a consumer can delete its own wrapping honestly.

## B. List-level validation, and a fixer

**Rules over the list**, added to `duck_blocks_validate` (which already carries one list-level
rule: a level may not jump by more than one between adjacent elements), each with a
`field` of `list` so a consumer can separate them from per-block errors:

| rule | error text | deterministic fix |
|---|---|---|
| L1 `element_order` is dense from 0 in list order | `element_order starts at N; must start at 0` / `gap after N` | renumber in list order (this is `duck_blocks_reorder` with the sort being a no-op) |
| L2 the first element is at level 1 | `first element is at level N; top level is 1` | rebase all levels by the same offset |
| L3 no level jump greater than +1 (existing) | existing text | collapse the jump: every element in the jumped-over subtree moves up by the gap |
| L4 an element whose type requires an ancestor has one (table in A) | `list_item at N has no list ancestor` | wrap per the implicit-parent table |
| L5 a `kind='inline'` element is preceded, at its level minus one, by a block that carries inlines | `inline at N has no block parent` | wrap per the table (`plain`) |

**The fixer: `duck_blocks_repair(blocks) -> LIST(duck_block)`.** Applies L1..L5's fixes in
the order L4, L5, L3, L2, L1 (structure first, numbering last, because wrapping inserts
elements that need numbers). Idempotent: a second pass finds nothing. It never touches
`content`, `attributes` or `element_type` of an existing element, and it never removes one;
that is what makes every fix deterministic. What it cannot fix it leaves, and
`duck_blocks_validate` still reports it: a per-block error (bad kind, empty element_type,
unknown encoding) is not a repair. Name chosen over `fix`/`coerce` because it says what it
does to a document and reads next to `normalize` (content rule) and `reorder` (numbering).

Not merged into `duck_blocks_normalize`: normalize applies the content rule and is vendored
header-only by producers that cannot load this extension; repair needs the implicit-parent
table and produces new elements. They compose: `repair(normalize(b))`.

---

## C. One shape per element_type, for every producer — the review

**Re-scope (normative):** "every producer of duck_blocks", not "every producer in this repo".
The rule's justification is unchanged: a consumer query over `list_item.content` must mean
one thing whatever produced the blocks. duckeye dispatches entries of one ZIM archive to
three different readers, so this is a within-one-file property, not a cross-file one.

**The instance: the tight list item.** This was presented in #29 as under-determined. It is
not. The spec's Pandoc mapping table (lines ~974-992) and the 6.0 change note already rule it:

| shape | pandoc | meaning |
|---|---|---|
| `list_item` with `content` | `Plain` | tight item |
| `list_item` > `paragraph` child | `Para` | loose item |

Measured 2026-09-10: pandoc emits `Plain` for `- alpha\n- beta` and `Para` for
`- alpha\n\n- beta`; this repo's exporter reproduces each from its shape; **markdown 75e9d0b
emits `list_item(NULL) > paragraph` for BOTH inputs**, so tight and loose lists leave the
markdown reader byte-identical. panduck's docx reader emits content on the item for a tight
item and is conformant. The level diagram in the spec shows a `list_item > paragraph`, which
is the loose form and also legal; it did not decide anything.

**Root cause on markdown's side (markdown#60, measured on d428f7e):** the vocabulary added
`plain` at 4.0 precisely to stop this collapse ("this reader had been collapsing onto
`paragraph`, losing the tight vs loose list distinction"); markdown vendored the header
through 6.5 and `TYPE_PLAIN` is referenced by no source file there; its reader's element
types are blockquote, heading, list, list_item, paragraph. cmark-gfm exports
`cmark_node_get_list_tight()`, so the parser is not discarding the bit; the reader does not
read it. So this is a producer that never implemented a spec version it vendors, which is
#29's cross-repo enforcement gap in its purest form, and the corpus (D) is the instrument
that would have shown it.

**Pros of enforcing the ruled shape on markdown:**
- Fidelity: tight/loose is a real property of the source, it round-trips through pandoc, and
  this repo fixed exactly this collapse in its own converter at 6.0. A reader that loses it
  cannot be made to recover it downstream.
- Convergence: one shape for a tight item across markdown, webbed (already content-on-item
  for `<li>text</li>`, per the spec's own example) and panduck. #29's example 2 disappears.
- No new rule: the ruling already exists; this only widens who it binds.

**Cons, and who pays:**
- markdown's reader changes emitted shape for every tight list, which is most lists in most
  markdown. Consumers that walk to a child paragraph for the text (duckeye `-Q li` over
  markdown sources) see `content` instead; the text is still there, in a different place.
- markdown's own tests and any golden files move.
- Loose lists are unaffected, so a consumer cannot assume one shape per source; it must
  handle both, which the spec always required.

**Mitigation:** the `_structs`/`_text` pattern does not apply (this is a reader, not an API),
but `duck_blocks_to_text` and the heading/section extractors already read both shapes, so a
consumer that goes through them sees no change. A release note on markdown naming the shape
and the reason, and a corpus entry (D) that pins it, are the guard.

**Rationale, stated for the record:** the shape is not a preference between two readings;
it is the encoding of tight versus loose, which pandoc, the spec and this repo's exporter
agree on. Breaking markdown's current output is fixing a fidelity loss, the same class as
the 6.0 fix here.

**Second instance to record, not to rule now:** the inline-wrapper gap the spec already lists.

---

## D. A shared conformance corpus, owned here

**Shape:** `test/conformance/corpus/<construct>/source.<fmt>` for each construct the spec
names (heading, paragraph, tight list, loose list, nested list, code, blockquote, table,
figure+caption, page_break, metadata/frontmatter, inline run, link, image, raw) in each
source format a fleet producer reads (md, html, docx where a fixture exists), plus
`expected.blocks.parquet`: the canonical sequence for that construct, written by hand or
by the exporter and reviewed, with `element_order` dense from 0.

**What "agree" means:** for each producer that can read the source format, the emitted
sequence must match `expected` on `kind, element_type, level, content, and the attribute
keys the spec names for that type`, in order, after `duck_blocks_repair`. Attribute keys a
producer adds beyond the spec's are ignored (they are the ATTR_ proposal's business). A
mismatch names the construct, the producer and the first differing element.

**Runner:** `test/check_conformance_corpus.py`, same shape as `check_consumer_alignment.py`:
skips (never OK) when a producer is not installed; runs each producer through the installed
DuckDB CLI with a clean `extension_directory` so it measures what users get; distinguishes
"regression" (matched before, mismatches now) from "expected upgrade" (expected changed in
the same commit) the way duckeye's drift check does; carries a manifest of the spec version
each expected file was written against, the way panduck's `test/fixtures/parsed/` does.

**Attribution (duckeye, 2026-09-10):** a disagreement reported without saying WHICH
producer moved produces a confident wrong attribution by elimination. The runner records
the installed version of every producer it ran, compares each against the version the
manifest recorded when `expected` was written, and reports a mismatch as "producer X moved
from v to v'" or "expected changed in <commit>", never as a bare difference. duckeye had to
add panduck to its probe for exactly this reason: an absent producer got a sibling blamed.

**Cheap closed-set assertions (Tiiny via the zim session):** the corpus runner also asserts
that every producer's emitted `kind` values are a subset of `duck_block_kind_names()` and
its `element_type` values a subset of `duck_block_type_names()`, per producer, so a new
kind or type fails loudly at the producer that introduced it rather than as a silently
skipped element in a consumer's allowlist. Tiiny's golden test already does this on the
consumer side; the corpus makes it a producer-side check.

**What the corpus is not (Tiiny via the zim session):** a test. It runs after a producer
builds and after a consumer's corpus builds; an eleven-hour build finishes before any test
tells you it was wrong. So the corpus is the second line: it catches the producer that
introduced a new kind at the right place, and it does not remove a consumer's obligation to
filter with an allowlist that fails in the cheaper direction on its own. The spec's
consumer text says so; the corpus's documentation must not read as replacing it.

**Absorbed:** panduck's parsed fixtures and manifest become corpus entries (with panduck's
agreement); duckeye's drift check logic, including the version-attribution above, becomes
the regression/upgrade split in the runner.
Producers run the runner in their own CI against their own build, as they run the vendored
vocabulary check today.

---

## Sequencing and version

1. Spec text for A, B (rules), C (re-scope + the tight-item ruling made explicit in the
   Content Rules section, not only the Pandoc table), D (what agree means). SPEC_VERSION
   6.5 → **6.6**: additive for consumers; producers gain obligations (L1, C), which the note
   states as such with markdown#59 and the tight-item change named.
2. Header: `IMPLICIT_PARENT_OF` table.
3. `duck_blocks_validate` list rules L1, L2, L4, L5 (L3 exists), test-first.
4. `duck_blocks_repair`, test-first, including idempotence and "never removes content".
5. `duck_blocks_to_pandoc_ast` applies the implicit-parent table (the #29 example 3 fix),
   test-first: the fragment that returned `[]` returns `Plain` / `BulletList`.
6. Corpus: the constructs this repo can produce from its own builders first, then the
   markdown/webbed/panduck sources as their sessions supply them.

## Tests

- L1..L5 each with a positive, a negative, and the repaired result re-validating clean.
- `repair` idempotent; `repair` never changes `content`/`attributes`/`element_type`; a
  per-block error survives `repair` and is still reported.
- `to_pandoc_ast` on an orphan inline run yields one `Plain`; on an orphan `list_item` run
  yields one `BulletList` with one item per orphan; pandoc 3.1.3 accepts both.
- Corpus runner: a deliberately wrong expected file fails with the construct and element
  named (a negative control on the runner, not only on the producers).
