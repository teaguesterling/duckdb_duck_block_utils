# Pandoc AST conversion gaps in `duck_block_utils`

> **For review by whoever owns this repo.**
> Found on 2026-08-31 while building `panduck`'s Pandoc AST alignment test. Every claim
> below was **reproduced against the built binary**, not inferred from reading code.
>
> **Nothing in this repo has been modified.** This file is untracked and dropped in as a
> report only — delete it freely. The repo was on branch `feat-doc-query-pipeline` with a
> clean tree at HEAD `9165076` when this was written; no commit, stage, or edit was made.

---

## TL;DR

`pandoc_ast_to_blocks()` silently discards content. Four pandoc constructors are affected;
all four lose data with no error, no warning, and no placeholder row.

| # | Constructor | Direction | Symptom | Severity |
|---|---|---|---|---|
| 1 | `Figure` | import | Block dropped entirely — **image and caption both lost** | **High** |
| 2 | `LineBlock` | import | Block dropped entirely | Medium |
| 3 | `DefinitionList` | import | Block dropped entirely | Medium |
| 4 | `Underline` | import **and** export | Text replaced by a placeholder, or lost outright | Medium |

`Figure` is rated High because pandoc 3.x wraps *every standalone captioned image* in a
`Figure`. `![caption](img.png)` on its own line — the most common way to put a figure in a
document — is silently deleted by the importer.

---

## Environment

```
duckdb_duck_block_utils  branch feat-doc-query-pipeline, HEAD 9165076
built binary             build/release/duckdb  (built 2026-08-31 10:54)
pandoc                   3.1.3
pandoc-api-version       [1, 23, 1]
```

---

## Finding 1 — three block constructors are dropped without a trace

### Reproduction

```sh
cd ~/Projects/duckdb_duck_block_utils

cat > /tmp/gaps.md <<'EOF'
| A line block first line
| and its second line

Term one
:   Definition of term one.

An [underlined phrase]{.underline} inside a paragraph.

![A figure with a caption](img.png)
EOF

pandoc -f markdown -t json /tmp/gaps.md > /tmp/gaps.json
python3 -c "import json;print([b['t'] for b in json.load(open('/tmp/gaps.json'))['blocks']])"
# ['LineBlock', 'DefinitionList', 'Para', 'Figure']

./build/release/duckdb -c "
SELECT b.kind, b.element_type, coalesce(b.content,'<NULL>') AS content
FROM (SELECT unnest(pandoc_ast_to_blocks(
        (SELECT content FROM read_text('/tmp/gaps.json')))) AS b) t;"
```

### Observed

Four pandoc blocks in, **one** duck_block out:

```
┌─────────┬──────────────┬─────────────────────────┐
│  kind   │ element_type │         content         │
├─────────┼──────────────┼─────────────────────────┤
│ block   │ paragraph    │ An  inside a paragraph. │
└─────────┴──────────────┴─────────────────────────┘
```

`LineBlock`, `DefinitionList` and `Figure` produced nothing at all. (Note also the
`An  inside` — the underlined text is missing; that is Finding 2.)

### Per-constructor confirmation

Each was then tested in isolation, so the result is not an artifact of them appearing
together:

```
LineBlock        pandoc emits ['LineBlock']       -> duck_blocks produced: 0
DefinitionList   pandoc emits ['DefinitionList']  -> duck_blocks produced: 0
Figure           pandoc emits ['Figure']          -> duck_blocks produced: 0

controls:
Para             pandoc emits ['Para']            -> duck_blocks produced: 1
Header           pandoc emits ['Header']          -> duck_blocks produced: 1
```

### Why `Figure` is the worst of the three

The same image survives or vanishes depending only on whether it sits alone in a block:

```
# standalone -> pandoc emits Figure -> 0 duck_blocks. Image AND caption gone.
![The caption](photo.png)

# mid-sentence -> pandoc emits Para/Image -> survives correctly:
See ![The caption](photo.png) here.
#   paragraph |
#   text      | See
#   space     |
#   image     | The caption      <-- fine
#   space     |
#   text      | here.
```

Any document converted through pandoc 3.x loses its figures, while inline images are
unaffected — which makes this easy to miss in testing.

### Root cause

`src/pandoc_block_convert.cpp:306-308` — the dispatch chain ends in a bare `else` that
returns without emitting anything or recording that it did so:

```cpp
	} else if (strcmp(pandoc_type, "Div") == 0) {
		...
	} else {
		return;          // <-- any unrecognised block silently disappears
	}
```

The constructors are handled nowhere else. A search across the whole source tree returns
**zero** hits for each:

```sh
$ for c in LineBlock DefinitionList Figure Underline; do
    echo "$c: $(grep -rn "\"$c\"" src/ | wc -l) hits"; done
LineBlock: 0 hits
DefinitionList: 0 hits
Figure: 0 hits
Underline: 0 hits
```

### Docs disagree with the code

`docs/pandoc_ast_spec.md:422-424` documents these three as *mapped*:

| Pandoc Type | element_type | level | encoding | Notes |
|---|---|---|---|---|
| `LineBlock` | `pandoc:lineblock` | NULL | json | |
| `DefinitionList` | `pandoc:deflist` | NULL | json | |
| `Figure` | `pandoc:figure` | NULL | json | Pandoc 3.0+ |

No code produces `pandoc:lineblock`, `pandoc:deflist` or `pandoc:figure`. Either the docs
or the code should move; a reader trusting the spec would reasonably assume these
round-trip.

---

## Finding 2 — `Underline` loses its text in both directions

`Underline` is the odd one out: the vocabulary supports it *everywhere except* the Pandoc
converters.

```
src/include/block_types.hpp:75            INLINE_UNDERLINE = "underline"   <-- defined
src/render_ansi.cpp:733                   styles it for terminal output    <-- consumed
src/inline_builders.cpp:194,542,796,980   db_underline() builds it         <-- produced
src/pandoc_inline_convert.cpp             never matches "Underline"        <-- MISSING
```

So this looks like an oversight in the converters rather than a deliberate omission.

### Import: two different failure shapes

The damage depends on which path the inline run takes.

**Case A — run is otherwise text-only (flattening path):** the text is lost outright.

```
input:  An [underlined phrase]{.underline} here.
output: block | paragraph | "An  here."     <-- "underlined phrase" gone, no placeholder
```

**Case B — run also contains a rich inline (structured path):** the text is replaced by a
literal placeholder.

```
input:  An [underlined phrase]{.underline} with `code` here.
output: inline | text | An
        inline | text | [Underline]         <-- placeholder; original words gone
        inline | text | with
        inline | code | code
        inline | text | here.
```

Case B comes from the fallback at `src/pandoc_inline_convert.cpp:240-241`:

```cpp
		} else {
			inline_type = BlockTypes::INLINE_TEXT;
			content_str = "[" + string(pandoc_type) + "]";
		}
```

Case A is arguably the more serious of the two, because there is no `[Underline]` marker
left to notice.

### Export: also a placeholder

The reverse direction is broken symmetrically — `db_underline()` output does not survive
`duck_blocks_to_pandoc_ast()`:

```sql
SELECT duck_blocks_to_pandoc_ast(
  db_paragraph([db_text('An '), db_underline('underlined'), db_text(' phrase.')])
).blocks;
```

```json
[{"t":"Para","c":[{"t":"Str","c":"An "},
                  {"t":"Str","c":"[underline]"},   <-- should be {"t":"Underline",...}
                  {"t":"Str","c":" phrase."}]}]
```

The word `underlined` is gone and the constructor is wrong. So a document cannot
round-trip underline in either direction, even though `db_underline()` and the ANSI
renderer both handle it.

---

## Suggested direction (not prescriptive — you know this code better)

1. **Make the drop non-silent.** The highest-value change is probably not mapping any
   particular constructor, but ensuring `pandoc_block_convert.cpp:306` cannot discard a
   block without a trace. An `unknown`/`pandoc:raw` passthrough block carrying the original
   `t` would preserve document length and make future gaps visible instead of invisible.
2. **Wire `Underline` into both converters.** `INLINE_UNDERLINE` already exists and is
   already rendered; this looks like two missing `strcmp` branches — import in
   `pandoc_inline_convert.cpp` alongside `Strong`/`Emph`, and export next to them in the
   reverse path.
3. **`Figure` first** among the block constructors, on impact. Its content is a caption plus
   nested blocks, so it may map reasonably onto the existing `Div`-style recursion.
4. **Reconcile `docs/pandoc_ast_spec.md:422-424`** with whatever is decided, so the spec
   stops advertising three mappings that do not exist.
5. **Consider a regression test** driven by real pandoc. `panduck` has one that may be worth
   copying — `duckdb_panduck/test/pandoc/check_pandoc_alignment.py` runs pandoc over a
   fixture exercising 34 of the 35 pandoc-types 1.23 constructors and fails when the
   handled set drifts. It is standalone Python and skips cleanly when pandoc is absent.

---

## What was **not** checked

Being explicit so this isn't over-trusted:

- Only the `markdown` reader was used to generate ASTs. Other pandoc readers (docx, latex,
  html) may emit constructors this fixture never produced.
- `Null` was not exercised — no pandoc reader appears to emit it.
- The export direction was checked **only** for `Underline`. `duck_blocks_to_pandoc_ast()`
  may have further gaps that were not looked for.
- Nested occurrences were not tested — e.g. a `Figure` inside a `BlockQuote`, or a
  `DefinitionList` inside a list item. The dropping `else` returns before recursing, so
  nested cases are likely affected too, but that was not confirmed.
- No fix was attempted, and no file in this repo was modified.

---

## Provenance

These gaps surfaced from `panduck`'s alignment harness, which asserts that panduck's
Pandoc AST mapping matches what a real pandoc binary emits. All four constructors are
recorded there as `status='planned'` and are visible in SQL:

```sql
SELECT pandoc_type, element_type, status
FROM panduck_pandoc_ast_map() WHERE status = 'planned';
--  DefinitionList  pandoc:deflist     planned
--  Figure          pandoc:figure      planned
--  LineBlock       pandoc:lineblock   planned
--  Underline       underline          planned
```

The harness ratchets on that set: if `duck_block_utils` fixes one, or a future pandoc adds
a constructor, `duckdb_panduck/test/pandoc/check_pandoc_alignment.py` fails until the
mapping is updated — so a fix here will surface there rather than drifting apart.

Repo: https://github.com/teaguesterling/duckdb_panduck
