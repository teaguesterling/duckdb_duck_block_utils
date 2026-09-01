# Vendorable utilities

Files here are **meant to be copied into your own repository**. They carry no
dependency on the `duck_block_utils` extension, because most consumers cannot load
it: DuckDB matches extension ABI by **exact version string**, so an extension that
vendors its own DuckDB submodule pinned off the release tag (`v1.5.5-dev154`) is
refused `duck_block_utils` by every route — `INSTALL`, `LOAD '<path>'`, and
publishing would not change it. Two of the three consuming extensions are in that
position today.

That is not a packaging inconvenience. It means every conformance check this repo
built was invisible to the consumers who most needed it — and invisible from here,
because this repo can always load itself.

| file | what it is | needs |
|---|---|---|
| `duck_block_conformance.sql` | validity, error detail, and the declared kind/type lists, as DuckDB macros | DuckDB. Nothing else. |
| `duck_block_normalize.hpp` | the spec 6.1 content rule as a transform — collapses a lone `plain` into its container, to a fixpoint | `duck_block_vocabulary.hpp` + DuckDB's `Value` API |
| `../src/include/duck_block_vocabulary.hpp` | the element_type / kind / attribute names as C++ constants | `<cstdint>`. Nothing else. |

`duck_block_normalize.hpp` is **the implementation this extension itself calls** — not
a copy kept in step. `src/normalize.cpp` includes it and `vendor/` is on the build's
include path, so there is exactly one version of the rule and it cannot drift from the
one you vendored. Where a duplicate was unavoidable (`duck_block_conformance.sql`
reimplements the validator in SQL) it is compared on every `make check`; here it was
avoidable, so there is no duplicate to compare.

Pass your own `duck_block` LogicalType as the second argument — the header does not
define the type, and the rebuilt structs must match the vectors you put them in.

**Why a transform and not just a check.** Every hand-rolled copy of this rule in the
wild is a copy of the version that stalled on a chain of `plain` elements, because
that is what the spec described until 2026-09-01. None of them will get the fix. A
producer that vendors this converges instead of diverging further — which is the whole
argument, since the rule having no distributable home is why panduck's readers and
this extension ended up implementing it in two layers.

`duck_block_vocabulary.hpp` deliberately stays at `src/include/` — four extensions
already vendor it from that path, and moving it to buy tidiness would break their
pull tooling for no gain. Its own "VENDORING THIS FILE" block is the authority on how
to take it.

## Every file here is COMPARED against the extension, not merely maintained

Each of these duplicates rules the extension also implements. A duplicate that
nothing checks is the defect that made an image's alt text invisible in this repo for
months: the reader wrote one fact twice, the exporter read one copy, and every round
trip through the pair looked perfect **because the pair shared its assumptions**.
Maintaining both carefully is not a defence.

So `make check` runs `test/check_conformance_macro.py`, which fails if the vendorable
SQL and the extension disagree about:

- the verdict, over 13 documents including real reader output
- the **error detail** — `{element_order, field, message}`, compared as a set
- the declared **kind** list, against `duck_block_kind_names()`
- the declared **element_type** list, against `duck_block_type_names()`

Each comparison is verified by perturbation rather than by passing. The kind-list
perturbation is the historical defect itself: reverting to two kinds — which is what
this repo's spec published until 2026-09-01 — fails with *"a kind missing here makes
the macro reject conforming data outright."*

## Using it

```sql
.read vendor/duck_block_conformance.sql     -- or paste it; it is plain SQL

CREATE TEMP TABLE d AS SELECT my_reader('doc.epub') AS blk;   -- MATERIALISE FIRST
SELECT duck_blocks_are_valid(blk) FROM d;                     -- gate: true/false
SELECT * FROM duck_blocks_errors((SELECT blk FROM d));        -- why it failed
SELECT duck_blocks_undeclared_types((SELECT blk FROM d));     -- type names outside the vocabulary
```

**Materialise first, always.** Do not try to work out whether your producer is
exempt from that — the file explains why the exemption is not knowable, and the
error you get names subqueries you never wrote.

**Run a control before trusting a silence.** A green run from a check with a blind
spot is worth nothing, and this file had one until 2026-09-01: it accepted any
string as an `element_type`. Assert that a deliberately broken document FAILS,
before concluding that yours passes.

## Copy or extract — do not re-derive

If you need a transform or check that is not here, ask rather than writing your own.
Three producers independently invented three different answers for document metadata
— two dropped it, one minted a type outside the vocabulary — and the shared rule
having no distributable home is why. Additions belong here.
