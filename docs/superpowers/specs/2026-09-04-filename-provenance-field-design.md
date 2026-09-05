# Optional trailing `filename` field on duck_block — design

**Status: RULED 2026-09-04 by Teague ("filename is go", confirmed first-hand, all six rulings) and IMPLEMENTED the same day on `fix/duckdb-v2-compat`.** Originally: Requested 2026-09-04 by the panduck and
webbed sessions. Nothing below is implemented. Six decisions are the spec owner's
and are marked RULING.

## Problem

Readers have no provenance. `read_html_blocks('*.html')` returns rows from several
files with no way to say which file a row came from. The fix every reader wants is a
`filename` column on its output. The documented consumer idiom folds the whole row
into the struct:

```sql
SELECT duck_blocks_toc((SELECT list(b) FROM read_html_blocks('*.html', filename := true) b));
```

so an 8th column makes an 8-field struct, and every C++ function here binds the
exact 7-field type. Measured on this build (spec 6.3, DuckDB v1.5.5):

| Input | Result today |
|---|---|
| 7-field struct | works |
| 8-field struct, `filename` trailing | Binder Error, no candidate matches |
| 8-field struct with explicit `::duck_block[]` | works, `filename` dropped |
| 8-field struct, `filename` in the middle, explicit cast | works, `filename` dropped |
| `to_duck_block(8-field)` | works (ANY-typed, reads by index) |

DuckDB's named STRUCT-to-STRUCT cast already matches by name and skips extra source
fields. Only the **implicit** cost rule refuses a child-count mismatch
(`cast_rules.cpp`, `source_children.size() != target_children.size()` returns -1).
That single rule is the whole break.

## Where the siblings stand (measured 2026-09-04, local checkouts)

| Repo | State | Column name |
|---|---|---|
| duckdb_webbed `main` | `read_html_blocks(..., filename := true)` shipped | `filename` |
| duckdb_markdown `main` (v1.7.x) | `read_markdown_blocks(..., include_filepath := true)` shipped | `file_path` |
| duckdb_panduck `feat/duck-block-filename` | held unmerged | `filename` |
| duck_block_utils | `duck_block_ext` type registered, never emitted, not in the spec | `file_path` |

The siblings have already diverged. Whatever is ruled, one of them moves.

## RULING 1 — name

Recommendation: **`filename`**.

- DuckDB core names the column `filename` on `read_csv`, `read_parquet`, `read_json`.
- webbed main and panduck's branch already use it; only markdown uses `file_path`.
- Teague's relayed words to panduck were "filename should be an acceptable column".
- The `file` name used by the pdf community extension is the outlier, not the convention.

**Counter-argument, raised by panduck 2026-09-04 and withdrawn by panduck the same day
after the index check below.** The
vocabulary header already reserves this concept as `file_path` at index 8 (behind
`source_format` at 7), and markdown ships `file_path`; so `file_path` would match a
published name and need no removal. Checked against the layout: an 8-field trailing
shape puts the new field at **index 7** whatever it is called. `file_path` there
changes `FILE_PATH_IDX` from 8 to 7, a silent VALUE change, the drift the header's own
comments call the dangerous one. Keeping index 8 means the widened shape is the full
9-field ext type and every reader emits `source_format` as well, which nobody asked
for. So no name avoids touching the published constants; the honest comparison is
`filename` plus a loud removal against `file_path` plus a silent value change. The
name question is then only DuckDB core's convention against the dormant header's,
and core wins.

Consequence: duckdb_markdown renames its opt-in column `file_path` to `filename`
(its flag already accepts `filename := true` as a spelling). The dormant
`duck_block_ext` type here is a separate cleanup, not bundled with this change.

## RULING 2 — position

Recommendation: **trailing-optional, and exactly one widened shape is legal**:
the canonical seven fields, then `filename VARCHAR`. Nothing else, nowhere else.

- webbed reads struct children by index 0..6; panduck writes columns by index. A
  trailing field keeps every existing body correct with no logic change.
- Keying acceptance on one exact type keeps an unnamed 9th field, or `filename` in
  position 3, a loud binder error rather than a silently tolerated drift.

## RULING 3 — acceptance before emission

Recommendation: a **rollout rule, not normative spec text**. The normative text says:

- Consumers MUST accept both the 7-field and the 8-field shape.
- Producers MAY emit the 8-field shape; they SHOULD do so behind an opt-in flag
  named `filename`, matching DuckDB core.
- `filename` is the path as the reader received it (glob-expanded), the same meaning
  as DuckDB's own column. It is NOT copied into `attributes`.
- A consumer that transforms a block list MAY return the 7-field shape. Every
  `LIST(duck_block)`-returning function in this extension does. Provenance lives on
  the reader's rows and survives `GROUP BY filename`; it does not survive
  `duck_blocks_merge`.

The ordering (land acceptance everywhere, then emit) goes in the `SPEC_VERSION`
change note, because it only matters during the rollout.

`SPEC_VERSION` 6.3 → **6.4**: additive, nothing renamed or removed.

## RULING 4 — is `duck_block` a catalog type or a shape?

Raised by webbed 2026-09-04: webbed registers no catalog type named `duck_block`;
its functions bind an anonymous, structurally identical STRUCT. Verified here:

- This extension's functions ALSO bind the anonymous struct. `DuckBlockType()` sets
  no alias, and `typeof(duck_block_paragraph('x'))` prints the bare STRUCT.
- `duck_block` is only a system-catalog name, registered with
  `ExtensionLoader::RegisterType`, which uses ERROR_ON_CONFLICT. A second extension
  registering the same name would fail to load. (Cross-loading webbed could not be
  measured here; panduck measured the other direction on 2026-09-04: a `duck_block[]`
  value binds to panduck's anonymous STRUCT[] parameter, and the explicit 8-to-7 cast
  drops `filename` against that anonymous signature too.)
- `ExtraTypeInfo::Equals` ignores an alias when only one side has it, so the named
  type and the anonymous struct are the same type to the binder and the cast.

Recommendation: **structural identity is the conformance requirement; the catalog
name is a convenience that only duck_block_utils registers.** Spec text: "A
duck_block is the STRUCT shape below. Extensions MUST NOT register a catalog type
named `duck_block`; duck_block_utils registers it so `::duck_block` and
`::duck_block[]` work when it is loaded." That names what webbed already does and
prevents the load-time collision.

## RULING 5 — is the acceptance mechanism normative?

Recommendation: **mechanism-agnostic.** "Consumers MUST accept both shapes" is the
requirement; a registered cast and per-function overloads are both conformant.
This repo uses the cast for the one-place property. A consumer with no registered
`duck_block` name registers its cast on the anonymous struct types, which is what
`RegisterCastFunction` keys on anyway.

## RULING 6 — the vocabulary header already claims index 7

`duck_block_vocabulary.hpp`, which every sibling vendors, publishes
`SOURCE_FORMAT_IDX = 7` and `FILE_PATH_IDX = 8` for the never-emitted
`duck_block_ext` type. No sibling references either constant (grepped webbed,
markdown, panduck on 2026-09-04), but a `filename` at index 7 contradicts a
published constant, and panduck has already minted a private
`FIELD_FILENAME = "filename"` in its own header because the vocabulary offered
nothing to vendor.

Is `SOURCE_FORMAT_IDX` dead or the reason `duck_block_ext` exists? Dead: no function
produces the ext type, `db_blocks_set_source` is documented in the README and does not
exist in `src/`, and the spec never mentions either. It does not foreclose a future
`source_format`: optional trailing fields append in **adoption order**, so a later
field takes index 8. Reserving slots ahead of adoption is what produced this conflict.

Recommendation: **retire the two ext offsets and publish the new field from the
vocabulary header**: `FILENAME_IDX = 7` and `FIELD_FILENAME = "filename"`,
alongside `DuckBlockExtType` and the `duck_block_ext` catalog type going away.
Removing a constant is breaking by the spec's own rule, so the 6.4 note says so.

Retiring `duck_block_ext` also removes a **registered catalog type**, which is a
different blast radius (panduck, 2026-09-04): the constants break rebuilds of three
repos we control, while `CAST(x AS duck_block_ext)` in a user's own SQL breaks at
LOAD and no grep of ours can see it. Recommendation: follow this repo's precedent
for the Pandoc converter (commit 193a7c0, one release of warning before removal).
Retire the two offsets in 6.4, keep the catalog type registered and documented as
deprecated for one release, drop it in 6.5. The 6.4 note carries the type as its own
line, separate from the constants.
The consumer-alignment check reports the removal as EXTRA on every sibling until
they re-vendor, which is the intended signal, not noise.

## Mechanism in this extension

**Register two implicit casts** in `BlockTypes::Register`:

- `STRUCT(7 fields, filename VARCHAR)` → `duck_block`
- `LIST(that struct)` → `LIST(duck_block)`

each with a bind callback that returns `DefaultCasts::GetDefaultCastFunction` for the
pair, and a small positive implicit cost. The binder's overload resolution calls
`CastFunctionSet::ImplicitCastCost`, which checks registered casts before the default
rules, so the registered entry supplies the cost the default rule refuses. The
default cast itself is DuckDB's name-based struct cast, which drops `filename`.

Why this and not the alternatives:

- **Per-function overloads**: ~60 registrations to duplicate, and the 34 functions
  that return `LIST(duck_block)` would need a second return type or a strip step.
  **Measured by panduck 2026-09-04: overloads break NULL handling.** Two arities make
  an untyped `NULL` argument ambiguous ("Could not choose a best candidate function"),
  which removed panduck's documented empty-document-on-NULL behaviour. Its suite caught
  it; the overloads were reverted. Every consumer that accepts NULL would regress the
  same way.
- **`ANY` + bind per function**: same fan-out, plus every executor must be checked
  for a `Value::LIST(DuckBlockType(), ...)` rebuild that would throw on 8-field values.
- **`attributes['filename']`**: no shape change, but not a column, and repeated
  per block as a map entry rather than a dictionary-encoded column.
- **A `kind='value'` provenance element** (like the version marker): cannot be
  grouped by, and is lost the moment two files' rows are interleaved.

Cost is two registrations and one test file. SQL macros (`doc_toc`, `doc_render`)
route into the C++ functions and are covered for free.

## Verification

- 7-field input to every function family: unchanged (existing suite).
- 8-field input to a list-taking function (`duck_blocks_toc`), a struct-taking
  function, and the `list(b)` idiom from a CTE with a `filename` column: bind and
  return the same rows as the 7-field input.
- `duck_blocks_stamp` on 8-field input returns the 7-field shape.
- **Discriminating negatives**: `filename` in a non-trailing position, an extra
  field with any other name, and a 9-field struct must still fail to bind.
- No overload becomes ambiguous: run the builder functions that already have
  VARCHAR overloads with 8-field input.
- `RegisterCastFunction` with a bind callback exists on v1.5.5. The DuckDB `main`
  checkout on this machine is v1.5.4-based, so the v2.0 signature is verified by the
  CI canary, not locally.

## Verified against a sibling (panduck, 2026-09-04, same process, DuckDB v1.5.5)

Both extensions loaded in one process against this build, `duck_block_spec_version()`
reporting 6.4, panduck unchanged:

| Call | 7-field | 8-field |
|---|---|---|
| `panduck_blocks_to_pandoc_ast` | 3189 bytes | 3189 bytes, byte-identical |
| `panduck_blocks_to_pandoc_blocks` | | 2853 bytes |
| `duck_blocks_toc` | | 3 entries |
| `doc_toc` / `doc_section` | | 3 / 41 |
| `panduck_blocks_to_pandoc_ast(NULL)` | `[]` | unchanged |

Three claims this settles rather than argues: the registered casts reach a sibling's
ANONYMOUS struct signatures, so one place does cover everyone; an untyped NULL stays
unambiguous, which panduck's overload prototype had broken; and the 8-field input is
IGNORED, not merely tolerated, since the output is byte-identical to the 7-field call.
panduck will re-vendor the header only against a committed sha, which is the
vendoring discipline working as intended.

## Docs touched

- `docs/duck_blocks_spec.md`: a "Provenance: `filename`" subsection under Type
  Definition, the three normative sentences above, changelog entry.
- `src/include/duck_block_vocabulary.hpp`: `SPEC_VERSION` 6.4 with the change note;
  add `FILENAME_IDX`, `FIELD_FILENAME`; remove `SOURCE_FORMAT_IDX`, `FILE_PATH_IDX`.
- `README.md` line 357: the example uses `include_filepath := true` and a
  `file_path` column; both become `filename`.
- `docs/api_reference.md`, `docs/design.md`: mark `duck_block_ext` as never emitted
  and superseded by this field.

## Rollout

1. This repo lands acceptance and bumps `SPEC_VERSION`.
2. webbed: no change needed for the column; adopt the flag name if it differs.
3. markdown: rename `file_path` → `filename` on its own schedule; it is a visible
   change for anyone who opted in.
4. panduck merges `feat/duck-block-filename`.
