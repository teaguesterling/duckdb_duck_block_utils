# Spec 1.3: the body predicate

**Status:** implemented on `feat/spec-1.3-body-predicate`, awaiting Teague's review of the PR.
**Reported by:** duckeye (measurement), markdown (analysis), 2026-09-11.

## The divergence

Two conformant producers, one consumer, opposite results:

| source | element | reaches `-t text`? |
|---|---|---|
| `.docx` via panduck | `kind='value'` metadata tree | no |
| markdown frontmatter | `kind='block'`, `element_type='metadata'`, `role='frontmatter'` | yes: "title: Secret Title" printed above the first heading |

duckeye has no filter and no special case: it skips `value` because values are not body and
renders `block` because blocks are. markdown emits exactly what the two-homes rule says a
verbatim blob is. Both are right by the spec as written.

The reference tools have the same leak. Measured 2026-09-11 on a three-element list
(frontmatter blob, heading, paragraph):

```
duck_blocks_to_text        -> "title: Secret\n\nH\n\nBody."
duck_blocks_to_match_text  -> "title: Secret H Body."
```

`raw` and `hr` were already skipped; `value` was already skipped; only the blob leaked.

## Why it is a spec gap

The header's `KIND_VALUE` comment says content walkers "filter on KIND_BLOCK and ignore
these automatically". True, and it invites the converse: that a kind filter yields the
body. It does not. The blob is a block because it has a source position and a level (the
positional claims `role='frontmatter'` and `role='tailmatter'` are only meaningful on a
block), and it is metadata because that is what it is. "Body" was never defined, so each
consumer defined it, and the definitions differed on the one element that is both.

## Options considered

1. **Define body once; fix the reference tools.** Chosen.
2. **Fix `to_text` only, tell consumers in prose.** Leaves every consumer writing its own
   filter, which is how this diverged.
3. **Producers emit `value` rows alongside the blob** so kind filters work. Puts one fact in
   two homes and reinterprets a blob the spec says to keep verbatim.
4. **The blob is body; docx is the outlier.** Contradicts markdown#57 (frontmatter is not
   prose on the renderer path) and the header's own statement of intent.

## The rule

```
body(kind, element_type) := kind IN ('block', 'inline') AND element_type <> 'metadata'
```

Carried three ways so they cannot disagree:

- header: `constexpr bool IsBody(const char *kind, const char *element_type)`, beside
  `ImplicitParentOf`; vendored copies get the rule with the constants.
- SQL: `duck_block_is_body(kind, element_type) -> BOOLEAN`, NULL in, NULL out; a Value loop
  for the same DuckDB-main reason as `duck_block_implicit_parent`.
- `duck_blocks_to_text` / `to_match_text`: `metadata` joins `hr` and `raw` in the skip.

`raw` **is** body: document content in its source format. That it has no text rendering is
the renderer's decision and stays in `to_text`, not in the predicate. `hr` likewise.

## Versioning

Additive: one predicate, one function, one rule the prose implied. 1.2 -> 1.3. It changes
`to_text`'s output for documents with a blob; that is a fix to the reference tool and is
recorded in the header's history so a consumer that copied the old behaviour knows why.

## Tests

`test/sql/body_v13.test`: the predicate over a NON-constant vector (a constant NULL is
folded away before the function runs); the measured leak list now renders "H Body." with
the blob still present in the list; tailmatter and a `value` row give the same answer; a
paragraph whose text looks like YAML still renders (the fix keys on the element, not the
content); a `role='document'` blob alone renders nothing.

## Consumers

duckeye's `emits` / `no_leak` guard pair flips on its own when 1.3 is served. markdown
changes nothing. A consumer with its own body filter replaces it with the predicate.
