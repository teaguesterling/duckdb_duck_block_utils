# Duck Blocks Canonical Specification

**Status:** Canonical.

> This document deliberately does **not** carry a version number, and does not name
> spec versions in its prose. The shipped version is `duck_block_spec_version()` — one
> value, produced by the build, which cannot go stale the way a number written here
> did: this header read `0.4.0` for eight months while the shipped value moved five
> majors, so anyone reading it to decide what to implement was reading a number no code
> had produced in months.
>
> The change history lives beside `SPEC_VERSION` in `src/include/duck_block_vocabulary.hpp`,
> where it is next to the constant it describes.

This document defines the canonical representation for duck_blocks - structured document elements for DuckDB. Extensions that produce or consume duck_blocks (markdown, webbed, etc.) MUST conform to this specification.

## Type Definition

### duck_block (Unified Type)

Block-level elements, inline elements and metadata values all use the same unified
type, distinguished by the `kind` field. `value` models a document's metadata tree
(Pandoc's `MetaValue`); those elements are appended AFTER the blocks, so consumers
must filter on `kind` rather than index blindly. The authoritative list is
`duck_block_kind_names()`.

```sql
STRUCT(
    kind          VARCHAR,                      -- 'block', 'inline' or 'value'
    element_type  VARCHAR,                      -- Element type identifier
    content       VARCHAR,                      -- Text content (see Content Rules)
    level         INTEGER,                      -- Structural nesting depth (NOT heading level)
    encoding      VARCHAR,                      -- Content encoding hint
    attributes    MAP(VARCHAR, VARCHAR),        -- Key-value metadata (includes heading_level)
    element_order INTEGER                       -- Position in sequence
)
```

### Kind Values

| Kind | Description |
|------|-------------|
| `block` | Block-level elements (heading, paragraph, code, list, etc.) |
| `inline` | Inline elements (text, bold, italic, link, etc.) |
| `value` | Non-prose data attached to a document — currently its metadata |

`kind` is the discriminator, so a `element_type` string may be reused across kinds:
`code`, `image`, `raw` and `generic` all exist as both a block and an inline, and
`list` exists as both a block and a value. Always test `kind` and `element_type`
together.

**Consumers must filter on `kind`, not index blindly.** A block list may carry
`value` elements (document metadata, a version marker) after its content, so
`blocks[1]` is not guaranteed to be the first content block. Walking with an
**allowlist** — `kind = 'block'` — survives a future kind; a blocklist such as
"skip inline, treat the rest as a block" does not.

Introspect the live vocabulary rather than mirroring this table:

```sql
SELECT duck_block_kind_names();        -- ['block', 'inline', 'value']
SELECT duck_block_encoding_names();    -- every legal `encoding` value
SELECT duck_block_type_names();        -- every element_type name
SELECT duck_block_spec_version(); -- the spec version this build implements
```

Sibling extensions should consume the vocabulary as a **vendored copy**, not a
submodule — see the "VENDORING THIS FILE" block in the header itself for why, and
for the drift check a copy requires:

```cpp
#include "duck_block_vocabulary.hpp"   // link-free; constants only
duckdb::DuckBlockVocabulary::TYPE_FIGURE;
```

`src/include/duck_block_vocabulary.hpp` is a **published interface**. It is header-only
and deliberately declares nothing it does not define, so including it costs no linking —
`block_types.hpp` holds the type constructors and `Register()`, which do need
`block_types.cpp`. `test/check_vocabulary_header.py` fails if that property is ever lost,
because the breakage would appear at a *consumer's* link step and nothing in this repo
would otherwise notice.

Renaming or removing a constant is **breaking** for every consumer: bump
`SPEC_VERSION` and say so.

However the header arrives, **assert agreement at test time**. The introspection
functions above catch a copy that was never re-synced, which a compile cannot —
and note that constants catch a RENAME but never a changed VALUE, so a check is
not optional. `SPEC_VERSION` is the only signal a name-and-value comparison emits
for a pure SHAPE change like 2.0, so give it its own arm rather than lumping it in
with the type names.

## Block Types (kind='block')

**`level` is the same rule for every type**, and the column below says it per row
only because the table predates the rule being stated: a TOP-LEVEL element carries
NULL, and a child carries its parent's effective depth + 1, where NULL reads as
depth 1. So a top-level container is 1, its children 2, its grandchildren 3. A
container and a leaf sitting side by side at top level both carry 1 — depth is not
a property of being a container.

Earlier revisions documented a NULL at top level. That convention was never
approved and has been removed: NULL and 1 both resolved to depth 1, so a child at
1 under a NULL parent was indistinguishable from its parent's sibling, and
consumers expressing containment through `level` closed the container before the
child rendered. That was a live defect in the portfolio. `duck_blocks_validate()`
now rejects a NULL level outright.

| Type | Description | level Usage | encoding Values | Key Attributes |
|------|-------------|-------------|-----------------|----------------|
| `heading` | Section heading | depth (top level 1) | `text` | `heading_level` (1-6) |
| `paragraph` | Text paragraph | depth (top level 1) | `text`, `markdown` | |
| `plain` | Block-level text run with NO paragraph semantics | depth (top level 1) | `text` | |
| `code` | Code block | NULL | `text` | `language` |
| `blockquote` | Quoted content | depth (top level 1) | `text` if it carries content, else — | |
| `list` | List container | depth (top level 1) | — (never carries content; its text lives in its items) | `list_type` (canonical): `bullet`, `ordered`, `definition`. `ordered` (legacy alias). For ordered: `start`, `number_style`, `number_delim` |
| `list_item` | List item | parent `list` + 1 | `text` if it carries content, else — | |
| `deflist` | Definition list | NULL | `json` | |
| `lineblock` | Preserved line breaks | NULL | `text` (lines joined with `\n`) | |
| `table` | Table | NULL | `json` | |
| `hr` | Horizontal rule | NULL | `text` | |
| `page_break` | **Physical** page boundary — a marker, not a container | NULL | `text` | `page_number` |
| `metadata` | A verbatim metadata blob — *not* the `kind='value'` tree; see "two homes" below | depth (top level 1) | `yaml` | `role` |
| `image` | Block-level image | depth (top level 1) | `text` | `src`, `alt`, `title` |
| `raw` | Raw content in a *named* format | NULL | format name | `format` |
| `div` | Generic container | depth (top level 1) | `text` if it carries content, else — | `id`, `class` |
| `section` | **Semantic** sectioning container | depth (top level 1) | `text` if it carries content, else — | `role`, `id`, `class` |
| `figure` | Figure: content plus a caption | depth (top level 1) | `text` if it carries content, else — | `id`, `class` |
| `caption` | Caption belonging to the container before it | parent + 1 | `text` if it carries content, else — | `short_caption` |
| `generic` | Structurally valid, type not in this vocabulary | NULL | `json` | `source_type` |

### Containers nest by `level`

`div`, `section`, `figure`, `caption`, `blockquote`, `list` and `list_item` hold their
children by position: children follow them in document order at `level + 1`, and the
container ends at the first element back at its own level. This is the same mechanism
throughout — there is no separate child-list field.

**A container MAY carry `content`, and the rule for when is the same one that governs
every other element** — see the content rule under "`plain` vs `paragraph`" below.
`content` is populated if and only if the element has a single text child, so
`<li>text</li>` is `list_item` with `content='text'` and no children, while
`<li><p>text</p></li>` is `list_item` with a `paragraph` child.

`list` is the one exception, and it follows from the rule rather than qualifying it: a
list's children are `list_item`s, never text, so a `list` can never *have* a single
text child and therefore never carries `content`. Text on a `list` is malformed and is
dropped on export.

**THE RULE IS UNIVERSAL. It is not settled per type.** It holds for `figure`, `section`,
`div`, `blockquote`, `caption` and `list_item` alike, and it is decided by what sits
BESIDE the run — never by which `element_type` is above it. `duck_blocks_normalize` and
`duck_blocks_lint` are both type-blind by construction for exactly this reason, and
`vendor/duck_block_conformance.sql`'s `duck_blocks_warnings` flags a lone `plain` under
any of them.

> Asked by the webbed session, who measured that `<figure>Some text</figure>` emitting
> `figure > plain('Some text')` and `figure(content='Some text')` BOTH pass validity, and
> asked whether the rule they had been given for `list_item` was universal or per-type.
> It is universal, so the first shape is valid-but-not-canonical.
>
> They could not have known: validity accepts both, and the advisory rule that expresses
> the preference lived only in the extension — which they cannot load, because DuckDB
> matches extension ABI by exact version string and they vendor DuckDB off-tag. Two
> conformant producers could differ on the same document with nothing objecting. The
> advisory rules are in the vendorable file now for that reason.

> This paragraph once said containers "carry no content of their own", which
> **directly contradicted** the content rule stated later in this same document. Both
> statements were published, and an implementer conforming to either one was following
> the spec. That is precisely how the `level` disagreement happened — v1.0 said three
> different things about `level` in one file, and every extension that diverged was
> conforming to one of them. A specification that contradicts itself does not produce
> careless implementations; it produces careful implementations that disagree.

### `section` versus `div` versus `generic`

These are three different statements and should not be used interchangeably:

- **`div`** — a container with no semantics. HTML's own spec calls `div` "an element
  of last resort".
- **`section`** — a container that *means* something structurally. Which kind lives in
  `attributes['role']`: `section`, `article`, `aside`, `nav`, `header`, `footer`,
  `main`. One type plus a role attribute, following `heading`+`heading_level` rather
  than minting a type per variant.
- **`generic`** — "this is structurally valid but its type is not in this vocabulary."
  The original name is preserved in `attributes['source_type']` and the verbatim
  source in `content`. It is a **backstop against silent loss**, not a mapping: a
  reader should ledger its `generic` output so that "still generic" fails once the
  construct is mapped properly, rather than becoming where things go to be forgotten.

  `generic` suits a **closed** vocabulary where every constructor is semantic (Pandoc).
  For an **open** one where most elements are presentational (HTML tags, RTF control
  words), emitting `generic` for everything unrecognised floods the output and buries
  real gaps — there, scope it to constructs that carry document meaning.

### `page_break` is physical, not semantic

`page_break` marks a pagination boundary — a PDF page break, a DOCX explicit page break.
It is a **marker**: it carries no content and owns no children. Named `page_break`
rather than `page` because `page` is already taken twice in this extension — the
`duck_blocks_page` composer and the `page()` alias for assembling a document — and
because a marker is the boundary *between* pages, exactly as `hr` is a rule rather
than a section. `element_order`
already groups "blocks between marker N and N+1", so making it a container would
re-nest whole documents one level deeper for nothing.

**Consumers that walk semantic structure must ignore it.** A table of contents and a
section slicer care about headings, not about where the paper ran out. This type
exists because the alternative — a reader synthesising `## Page N` headings — puts
physical pagination into the heading structure, where `duck_blocks_toc_rows` then lists pages as
though they were sections.

Pandoc has no page constructor, so it exports as `Div` with class `page` carrying
`page_number`.

**The page axis is queried separately from the heading axis**, which is what makes
ignoring it in the outline affordable rather than lossy:

```
duck_blocks_page_rows(blocks)               -> page_number, start_order, end_order, block_count
duck_blocks_get_pages(blocks, first, last)  -> the blocks of that page range
```

A reader asking for the outline never sees pages; a reader asking for pages gets them.
That separation is the whole reason a synthesised `## Page N` heading is the wrong
answer — it is a page forced through the only surface that existed.

Semantics, because `page_number` was ambiguous until it was written down:

- **A `page_break` marks the START of the page it names.** Its
  `attributes['page_number']` labels the page BEGINNING there, not the one ending.
  The break for page N+1 is what ends page N — which is the question a heading
  structurally cannot answer, and the reason the type exists.
- **`page_number` is optional**; without it a break takes its ordinal position.
- **Content before the first break belongs to no page.** It is listed with a NULL
  `page_number` so it is never invisible, and it is not selectable by number.
- **A document with no `page_break` has no pages, and reports zero rows** rather than
  one implicit page. "This document has no page information" is a different fact from
  "this document is one page", and inventing the second would be structure the source
  never carried.

**`duck_blocks_get_pages` does NOT replace reader-level page selection, and a consumer
that swaps one for the other has introduced a performance regression disguised as a
simplification.** It slices blocks already materialised, so getting two pages of a
400-page document this way reads all 400 and discards 398. A reader that can push a
page range down — `read_pdf(src, first_page := 1, last_page := 2)` — must keep doing
so. The two answer the same question by different mechanisms: pushdown is for *reading
less*, `get_pages` is for addressing pages inside blocks you already hold. Both are
real; neither is redundant. Raised by duckeye against exactly this mistake in my own
description of the macro.

The same rule holds for `duck_blocks_get_section` versus a reader-level section flag,
but only the page case can actually lose anything — and the difference says when
pushdown is even available. **A section is defined by structure that exists only AFTER
parsing, so there is nothing to push down. A page range is knowable from the path
alone, so pushdown is available and declining it costs real work.** duckeye's
distinction. So: push a selection down wherever the selector is knowable before
parsing; where it is not, the block-level macro is not a compromise, it is the only
thing there is.

### `figure` and `caption`

```
block  | figure    | level N     attrs from the source
block  | paragraph | level N+1   <- content
inline | image     | level N+2
block  | caption   | level N+1   <- caption container, sibling of the content
block  | paragraph | level N+2   <- caption's own blocks, fully structured
inline | bold      | level N+3
```

**Caption position is the emitter's choice, not a property of `caption`.** A figure
emits content-then-caption because an image's caption belongs below it; a
`<details>`/`<summary>` label belongs above its body, and OOXML puts table captions
above and figure captions below in the same document. The caption scope runs from the
marker to the next element at its own level, so either order is well-defined.

`caption` is deliberately general rather than figure-specific, so tables and
disclosure widgets can use it without a new type.

**A figure's content is ANY block sequence. Do not assume the paragraph wrapper.**
The shape above is what the Pandoc reader emits, because pandoc's `Figure` holds
`Plain[Image]` and the paragraph is that `Plain`. An HTML reader emits a block-level
`image` at level N+1 directly, because `<figure><img>` has no intervening paragraph:

```
block  | figure    | level N
block  | image     | level N+1   <- also legal: no paragraph wrapper
block  | caption   | level N+1
```

Both are valid figure content and the validator accepts both, because the rule is
about *nesting*, not about which block types may appear. A consumer walking figures
must handle either — reaching for `children[0].content` on the assumption of a
paragraph will find an image from one reader and a paragraph from another.

Recorded so neither shape gets read as canonical: reported by the webbed session
2026-08-31, comparing its HTML reader against this repo's Pandoc reader.

**No caption means no `caption` block at all.** A figure whose caption is empty emits
`figure` then its content and nothing else. Do not assume the container is present.

### Heading Level Attribute

For heading elements, the semantic heading level (h1-h6) is stored in `attributes['heading_level']` as a string, NOT in the `level` field. This separates:

- **`level` field**: Structural nesting depth (used by blockquotes, inlines)
- **`heading_level` attribute**: Semantic heading level (1-6 for h1-h6)

**Producers** MUST set `attributes['heading_level']` for heading elements.

**Consumers** SHOULD check `attributes['heading_level']` first, then fall back to `level` field for backward compatibility with older data.

## Inline Types (kind='inline')

### Leaf Types (have content, no children)

| Type | Description | content | Key Attributes |
|------|-------------|---------|----------------|
| `text` | Plain text | The text | - |
| `space` | Word separator | Empty | - |
| `softbreak` | Soft line break | Empty | - |
| `linebreak` | Hard line break | Empty | - |
| `code` | Inline code | The code | - |
| `math` | Math expression | The LaTeX | `display`: inline/block |
| `raw` | Raw format | The content | `format`: html/latex/etc |

### Container Types (may have children)

| Type | Description | content Rule | Key Attributes |
|------|-------------|--------------|----------------|
| `bold` | Strong emphasis | See below | - |
| `italic` | Emphasis | See below | - |
| `strikethrough` | Struck text | See below | - |
| `superscript` | Superscript | See below | - |
| `subscript` | Subscript | See below | - |
| `smallcaps` | Small capitals | See below | - |
| `underline` | Underlined text | See below | - |
| `underline` | Underlined | See below | - |
| `link` | Hyperlink | See below | `href`, `title` |
| `image` | Inline image | Alt text | `src`, `alt`, `title` |
| `quoted` | Quoted text | See below | `quote_type`: single/double |
| `span` | Generic container | See below | `id`, `class` |
| `cite` | Citation | Key | `key`, `prefix`, `suffix` |
| `note` | Footnote | Content | - |
| `generic` | Type not in this vocabulary | Empty; children follow | `source_type` |


## Value Types (kind='value')

Non-prose data attached to a document. These model Pandoc's recursive `MetaValue`
tree, which a flat list of blocks and inlines has nowhere else to put — before this
existed, all document metadata (title, author, tags, draft flags) was silently
dropped on conversion.

| Type | Description | Carries | Key Attributes |
|------|-------------|---------|----------------|
| `string` | Scalar string | `content` | `key` |
| `bool` | Boolean | `content` = `'true'`/`'false'` | `key` |
| `list` | Ordered sequence | children at `level + 1` | `key` |
| `map` | Key/value mapping | children at `level + 1`, each with its own `key` | `key` |
| `inlines` | A run of formatted text | `kind='inline'` children | `key` |
| `blocks` | Block content | `kind='block'` children | `key` |
| `version` | duck_block spec marker | `content` = the version | *(none — see below)* |
| `generic` | MetaValue not in this vocabulary | verbatim `json` | `key`, `source_type` |

`attributes['key']` is the name under which a value sits in its parent map. Elements
in a `list` have no key. Nesting uses `level`, exactly as block containers do.

### `attributes['role']` on `metadata` — which source construct it was

`metadata` carries a verbatim blob. `role` says what kind of blob, so the TYPE answers
"which of the two metadata homes is this" and the ROLE answers "what did it come
from" — genuinely different questions that were being asked of one field.

| role | source | has a document body? |
|---|---|---|
| `frontmatter` | a frontmatter block at the head of a document | yes — the blob precedes it |
| `document` | the blob **is** the whole document — a `.toml` or `.yaml` file read as metadata, with no body at all | no |
| *(absent)* | unspecified; a consumer treats it as an opaque blob | unknown |

`frontmatter` and `document` differ on a structural fact, not on file extension: whether
there is a body the metadata belongs to. A `.yaml` file read whole is `document` even
though a frontmatter block is also YAML.

**Keep the blob VERBATIM. Do not parse it to decide the role, and do not parse it at
all.** Teague's ruling, and the reason is isolation rather than taste: panduck's `.toml`
branch parsed the file to build a value tree, which meant it *could not read a `.toml`
file at all* unless a third-party TOML extension was installed — a hard dependency
acquired to do work this vocabulary does not want done. Unparsed, it is a file read and
nothing else.

A producer MAY lift specific well-defined fields into `attributes` without parsing the
whole document. That is a narrow allowance, not an invitation: a partial parse wearing
an attribute is still a parse, and the moment it needs a real parser it has crossed the
same line.

**`encoding` says how to read the blob**, and TOML is a declared encoding for exactly
this reason — a verbatim `.toml` file emitted as `text` discards the one fact a consumer
needs in order to parse it, which is the opposite of what a verbatim blob is for. Use
`attributes['source_type']` only for something `encoding` cannot say.

Proposed by Teague; the reasoning is this document's own, four sections up: *"One type
plus a role attribute, following `heading`+`heading_level` rather than minting a type
per variant."* `frontmatter` as its own `element_type` is precisely minting a type per
variant, and `role` is already the general discriminator here — `section` uses it for
seven sectioning kinds, `list_item` for `term`/`definition`.

It is **additive**: `metadata` stays declared, so a consumer that has never heard of
`frontmatter` reads the block correctly today and the role is refinement it may ignore.
A rename would have cost a migration across four extensions and discarded the
provenance the divergent name was carrying.

**Adding a role is the spec owner's call, not an inference from the principle being
general.** The principle argues that roles are the right MECHANISM; it does not license
a producer to mint values. If you have a verbatim blob that is not frontmatter — a
LaTeX preamble, an RTF `\info` group kept whole — ask for the value rather than
inventing one, for the same reason `generic` exists: an unrecognised name should be
visible as a gap, not silently private. `duck_blocks_lint` does not yet check role
values, so nothing will object if you do.

### There are TWO homes for document metadata. Pick by SHAPE, not by preference

| you have | use | example |
|---|---|---|
| discrete FIELDS — title, author, date | `kind='value'`, this section | docx `core.xml`, EPUB Dublin Core, odt `meta.xml`, RTF `\info`, LaTeX `\title`, HTML `<head>`, Pandoc `Meta` |
| a verbatim BLOB you must not reinterpret | `kind='block'`, `element_type='metadata'`, `encoding='yaml'` | a markdown file's YAML frontmatter, kept as written |

They are not interchangeable and neither is a fallback for the other. The first is
structured and queryable; the second is a preserved artifact whose internal syntax
this vocabulary does not model.

> This is stated because the spec never said it, and the resulting confusion is
> documented: three producers each chose differently — one dropped metadata, one
> dropped it too, one invented a `frontmatter` type outside the vocabulary — and
> **the author of this document then relayed "metadata at level 1, `kind='value'`"
> to two of them**, which takes the element_type from one row and the kind from the
> other and names a shape nothing emits. Ask which of the two shapes you have.

### A realistic document, both together

Metadata and blocks coexist in almost every real document, and the combination is
where mistakes hide — this exact shape exposed an exporter defect in which a
document's title REPLACED its body, because every fixture had metadata or blocks and
none had both:

```
block  paragraph  L1                  content="body text"
value  inlines    L1  key=title       (inline text at L2 carries "Report")
value  list       L1  key=author
value  inlines    L2                  (inline text at L3 carries "A")
value  inlines    L2                  (inline text at L3 carries "B")
value  string     L1  key=date        content="2026-09-01"
value  bool       L1  key=draft       content="true"
```

A consumer walking blocks MUST stop its inline run at a `kind='value'` element, not
merely at the next block — the inlines under a `value` belong to that value.

Metadata is appended **after** a document's blocks. **This is a contract**, not the
convenience this paragraph once called it — that wording predates anything
depending on it, and two producers have now asked where value elements go relative to
blocks, which is a question a convenience cannot answer.

`blocks[1]` therefore still points at the first content block. But do not index
blindly: filter on `kind`, per the rule above.

**A consumer walking blocks MUST end an inline run at any NON-INLINE element, not
merely at the next block.** The inlines beneath a `kind='value'` element belong to that
value. This is stated because getting it wrong is not a subtle failure: this repo's own
exporter stopped at `kind='block'`, walked past a `value`, and emitted a document whose
**body had been replaced by its title**. It needs a document carrying both metadata and
blocks, which is the normal case and was in none of its fixtures.

**An element that is not nested in anything carries level 1, including a metadata
blob.** `level` is not a claim of depth *inside* something — it is the absence of
nesting. A frontmatter blob sits at the top level of its document exactly as the first
paragraph does, and an `hr` and a top-level `kind='value'` field carry 1 for the same
reason. There is no level 0: nothing is shallower than the top, and
`duck_blocks_validate()` rejects it.

> The rule stands on the definition of the field and needs no further argument: `level`
> is depth in a depth-first ordering, the top of that ordering is 1, and 0 names a place
> the ordering does not have. A carve-out would have had this document bless a number
> with no meaning in the rule that defines the field.
>
> **A NOTE ON HOW THIS WAS DECIDED, because the reasoning published here was wrong
> before it was corrected.** The ruling originally argued that the producer's `level`
> column was heading RANK and so a different measurement colliding with this one. That
> claim was about a function I never read — duckdb_markdown measured it and their
> `read_markdown_blocks` column was already pure depth, with rank already in
> `attributes['heading_level']`. The 0-and-1-through-6 scale belongs to a *different*
> function of theirs. They described one thing, this document reasoned about another,
> and neither of us checked which until after the ruling shipped.
>
> Kept visible rather than quietly deleted: a specification that reaches a correct rule
> through a false premise is one bad premise away from a wrong rule, and the premise was
> a present-tense claim about another repository's code that nobody had measured.

**A document with NO blocks at all is conformant.** A `.toml` or `.yaml` file read as a
document is entirely metadata; a bare `kind='value'` tree with nothing of `kind='block'`
in it is valid, lints clean, and is what `pandoc_ast_to_blocks` itself emits for a
document with metadata and an empty body. Asked by the panduck session, who noted that
"nothing objects" was reasoning they had twice had to retract that day — correctly, so
this is stated rather than left to be inferred from silence.

### Version marker

`version` records which duck_block spec a persisted or exchanged list was written
against. It is **optional** — requiring it would shift every index and break every
consumer to protect a boundary most lists never cross.

```sql
SELECT duck_blocks_stamp(blocks);   -- append a marker
SELECT duck_blocks_version(blocks); -- read it back; NULL when unstamped
SELECT duck_block_spec_version();   -- what this build implements
```

It deliberately carries **no** `attributes['key']`, which is what keeps it out of a
document's own metadata on export. Use it when blocks are written to storage or
crossing an extension boundary; a runtime check against `duck_block_spec_version()` is
enough within a single session.

## Content Rules for Container Types

**`content` is populated if and only if the container has a single text child.**

This enables both ergonomic simple cases and full structured representation.

### Simple Case: String Argument → Single Struct

When a container builder receives a simple string, it returns a single `duck_block` struct
with the text in the `content` field:

```sql
duck_block_link('Click here', 'https://example.com')

-- Returns duck_block struct:
{kind: 'inline', element_type: 'link', content: 'Click here', level: 1,
 encoding: 'text', attributes: {href: 'https://example.com'}, element_order: 0}
```

### Complex Case: List Argument → LIST with Nested Children

When a container builder receives a list of inlines, it returns a `LIST(duck_block)`
with the container as first element (empty content) and children at level+1:

```sql
duck_block_link([duck_block_bold('Important'), duck_block_text(' link')], 'https://example.com')

-- Returns LIST(duck_block):
[{kind: 'inline', element_type: 'link', content: '', level: 1,
  encoding: 'text', attributes: {href: 'https://example.com'}, element_order: 0},
 {kind: 'inline', element_type: 'bold', content: 'Important', level: 2,
  encoding: 'text', attributes: {}, element_order: 1},
 {kind: 'inline', element_type: 'text', content: ' link', level: 2,
  encoding: 'text', attributes: {}, element_order: 2}]
```

### Normalization

A single-element list containing only a text inline SHOULD be normalized to the simple case:

```sql
-- These are equivalent:
duck_block_bold('text')
duck_block_bold([duck_block_text('text')])

-- Both produce:
{kind: 'inline', element_type: 'bold', content: 'text', level: 1,
 encoding: 'text', attributes: {}, element_order: 0}
```

### Level Field Semantics

**Every element carries an explicit `level`. There are no NULLs, and there are no
per-type exceptions.**

`level` is depth in a DEPTH-FIRST ordering of the document tree. Top level is 1;
a child is its parent's level + 1; siblings share a level. Together, `level` and
adjacency describe the entire tree — that is the whole reason the field exists, and
why it cannot be optional. An element without a level cannot be placed.

```
heading      1
paragraph    1
  text       2
  bold       2
    text     3
blockquote   1
  paragraph  2
    text     3
list         1
  list_item  2
    paragraph 3
```

Inlines are children of their block like anything else — a `bold` inside a
top-level paragraph is at 2, not 1. Blocks and inlines share ONE scale. They used
to be described as separate scales, which is what let a container and its child
resolve to the same depth and collapse.

**`level` is never semantic.** It does not encode quote nesting, list nesting, or
heading rank. A semantic level lives in an attribute beside it: a top-level `h2`
is `level` 1 with `attributes['heading_level']` = 2, and the two numbers are
independent. A consumer needing quote or list nesting depth counts containers by
walking the structure rather than reading it off this number.

**`list_type` is the canonical attribute for what kind of list this is.**
`attributes['ordered']` = 'true'/'false' is a legacy alias, kept because the earliest spec
documented it and the exporter has read both since v1.1.0.

`list_type` wins because a boolean cannot grow. Ordered and bullet are two of a set
that already wants more members — definition lists, navigation lists — and
`ordered='false'` says only what a list is *not*. Current values are `bullet` and
`ordered`; the set is open.

Producers SHOULD emit both names. Consumers MUST tolerate either and should prefer
`list_type`. A consumer reading only `ordered` sees nothing meaningful the first
time a third list kind appears.

**Do NOT fall back to `level` when a semantic attribute is missing.** A heading
without `attributes['heading_level']` has no rank information at all — reading its
`level` gives you its depth, so a heading inside two containers renders as `h3`
whatever it actually is. That is a live hazard, not a hypothetical: it was a fair
reading of an earlier revision, which said headings carried NULL there, and an
explicit level turns it into silent corruption. Default to 1 and treat the block as malformed;
`duck_blocks_lint()` warns on it. Both producers here always emit the attribute.

`duck_blocks_validate()` enforces this: a NULL level, a level below 1, or a level
that jumps by more than one from the previous element are all errors. The jump is
an error because depth-first ordering descends one at a time, so a jump means the
element's parent is not in the list and the tree cannot be reconstructed.

### Element Order Field Semantics

- Sequential ordering within the element list
- Used to reconstruct document order
- Starts at 0 for each element list

## Content Encoding

The `encoding` field indicates how to interpret `content`. The authoritative list is
`duck_block_encoding_names()` — this table restates it, and `make check` fails if the
two disagree.

| Encoding | Description |
|----------|-------------|
| `text` | Plain text, no markup |
| `markdown` | Markdown-formatted text |
| `yaml` | YAML content |
| `toml` | TOML content |
| `json` | JSON content |
| `html` | HTML content |
| `xml` | XML content |
| `latex` | LaTeX content |

### `encoding='json'` does not say WHOSE json

This is the most under-specified part of the format and it has already produced the
same four defects in three independent implementations. Read this before writing a
consumer.

Some block types store their structure as JSON in `content` rather than as child
elements, because their shape has no flat text rendering. **Decoding them is the
consumer's obligation.** A consumer that renders `content` directly puts raw AST on
the screen; one that ignores it renders nothing at all.

Which types carry it depends on the producer, and **the two producers in this repo
emit incompatible schemas for the same `element_type`**:

| | `duck_block_list(['a','b'])` | `pandoc_ast_to_blocks(...)` |
|---|---|---|
| **content** | `["a","b"]` | `[[{"t":"Plain","c":[...]}],...]` |
| attributes | `ordered=false` | `list_type=bullet` |

Both are `element_type='list'`, `encoding='json'`. A decoder written against one
silently mis-renders the other — the builder shape renders correctly today and the
Pandoc shape renders as empty bullets, from the same renderer, same document, one
variable changed. So `encoding='json'` is a statement about lexical form only. It
is **not** a schema, and it is not sufficient to decode against.

### ONE shape per element_type

**Every producer in this repo emits the same shape for a given BLOCK
`element_type`.** Before 2.0 it emitted three for `list` alone, and a consumer's
decoder silently depended on which producer made the block. That is the defect
this rule exists to prevent, and it is a promise consumers can rely on rather
than a convention.

**Known gap: INLINE wrappers are not yet covered by this rule.** `duck_block_bold('y')`
produces `bold` carrying `y` in `content`, where the Pandoc reader produces `bold`
with an empty `content` and a `text` child one level deeper. Both round-trip and
render correctly today because every path in this repo reads either, but it is the
same divergence this section exists to eliminate, and a consumer that tests
`content IS NULL` to identify a wrapper will get different answers from different
producers. Do not rely on either form for inlines yet; walk children if present
and fall back to `content`. Blocks are settled; inlines are scheduled.

Relatedly, "no content of its own" is spelled two ways, and the split is not
internal to this repo -- it runs between shipped extensions. Measured 2026-09-01:

```
duckdb_webbed        a container's content is NULL     (list, list_item)
duck_block_utils     a container's content is ''       (blockquote, the value tree)
```

**Consumers MUST treat NULL and `''` as the same absence. Producers SHOULD emit
NULL.** The portable test is `coalesce(content, '') <> ''`, which is what the
`list_not_structural` advisory rule uses.

The consumer half of this was always stated. What was missing is the producer
preference and the fact that the two spellings are how two READERS differ, not how
two builders in one codebase differ -- so a consumer writing `WHERE content IS NULL`
selects webbed's containers and not this repo's, and no check catches it, because
every check either coalesces or compares within one implementation.

The cost of ruling this way rather than making the spellings mean different things:
an INTENTIONALLY empty value cannot be distinguished from a container carrying no
content. For metadata nothing is lost -- a field present and blank is carried by the
element existing at all, with its key. If a body case ever needs the distinction, the
fix is a real one and not a re-spelling.

**`content` is populated IF AND ONLY IF the container has a single text child.**
This is spec v1.0's rule and it covers inline and block containers alike:

```
duck_block_blockquote('x')          blockquote content='x'      <- single text child
duck_block_blockquote([p1, p2])     blockquote content=NULL
                                      paragraph, paragraph      <- children at level+1
duck_block_bold('y')                bold content='y'
duck_block_bold([text, italic])     bold content=NULL + children
```

One rule, two inputs, two representations — not two shapes for one input. The rule
briefly replaced this for BLOCK containers with "a container never carries
content", which was broader than the defect required: what actually needed fixing
was `list` storing a JSON items array, not the content rule. It also left blocks
and inlines on two different rules, since inline containers never stopped
following v1. Restored in 3.0.

### Definition lists are a LIST KIND

A definition list is `list` with `attributes['list_type'] = 'definition'`. Terms and
definitions are `list_item`s distinguished by `attributes['role']`:

```
list         list_type='definition'
  list_item  role='term'        content="the term"
  list_item  role='definition'  content="the definition"
```

Zero new types. This is the extensibility `list_type` was made canonical FOR — a
boolean cannot say "definition" — and it means every consumer that already walks
lists handles definition lists with no new code.

Before 5.0 `deflist` carried the Pandoc tuple as opaque JSON, which meant it
RENDERED ITS OWN AST to the screen and its serialisation polluted search. That was
worse than `table`, which merely rendered nothing. The `deflist` type remains in the
vocabulary and is still accepted on export so stored data keeps converting, but
nothing emits it.

### `plain` vs `paragraph`

`plain` is a block-level run of text that is **not** a paragraph. It is Pandoc's
`Plain` constructor, and in HTML it is text not wrapped in a `<p>`.

**`plain` appears only where the text has nowhere else to live.**
The container's `content` is the first home for a single text child — that is v1's
content rule and it has never changed — so `plain` is what you use when that home is
taken or does not exist. There are exactly two such places:

```
<section>Lead<h2>H</h2></section>    section > plain("Lead") + heading
                                     the run has a block SIBLING, so the
                                     container's content cannot hold it

Plain at the top of a document       plain
                                     the document root has no content field
```

Everywhere else, a lone text run is the container's own content:

```
<li>tight item</li>          list_item  content="tight item"
<li><p>loose item</p></li>   list_item > paragraph
<td>cell</td>                cell       content="cell"
<dd>definition</dd>          list_item  role='definition'  content="definition"
<div>bare text</div>         div        content="bare text"
<blockquote>q</blockquote>   blockquote content="q"
```

An earlier revision shipped the first column as `list_item > plain(...)`, which gave a
container with a single text child **two** legal shapes depending on which producer
built it. Collapsing that back to one is the whole point of the content rule.

**Tight vs loose survives this, and needs no attribute.** The two shapes above are
exactly Pandoc's two constructors:

| duck_block shape | Pandoc | markdown |
|---|---|---|
| `list_item` with `content` | `Plain` | tight item |
| `list_item` > `paragraph` child | `Para` | loose item |

This was measured on the exporter before the rule changed, not assumed: it already
emitted `Plain` for a content-carrying item and `Para` for a paragraph child, so
narrowing `plain` cost nothing on the export side.

**The rule is about the RUN, not about its container.** Decide by what sits beside
the run, never by which `element_type` is above it. A nested list makes this visible
in both directions at once:

```
- outer          list                        the outer item's run has a `list`
  - inner          list_item                 sibling, so it keeps its `plain`
                     plain      "outer"
                     list                    the inner item's run is alone,
                       list_item  "inner"    so it becomes content
```

A consumer that special-cased `list_item` would get one of those two wrong.

**Why a type and not an attribute.** Pandoc has had this distinction all along and
this reader collapsed both constructors onto `paragraph`, which is how tight-vs-loose
list items were being lost — and lost independently in webbed, by a different
mechanism, with neither reader aware. The gap was a constructor we failed to
represent, not a variation we failed to annotate.

**Implementers: this rule CANNOT be applied while streaming. Do not try.**

Whether a text run becomes its container's `content` or stays a `plain` depends on
what FOLLOWS it. A rule whose input includes the element's later siblings is not
merely awkward for a reader that emits as it walks — it is unavailable to it. A
producer applying this rule inline is necessarily guessing, and the guess will be
right for its own test documents, which is what makes the resulting bug ship.

Raised by the panduck session, whose EPUB and LaTeX readers both stream, and
sharpened by duckdb_markdown to the form above. It applies to any streaming reader
of any format. It does NOT apply to a reader that walks a complete tree — webbed
parses HTML into a full DOM, so a node's siblings are known before anything is
emitted, and it needs nothing here. The distinguishing property is whether sibling
information is available at decision time, not how the container is tracked.

You do not have to solve it. Emit the naive shape — always a `plain` child — and
call `duck_blocks_normalize(blocks)` on the finished vector, which applies this
rule and returns the 6.1 shape. It is idempotent, so it is safe to call on input
that may already have been normalised upstream. That is exactly what this repo's
own Pandoc reader does: it emits `plain` during the walk and collapses afterwards.

The pass is deliberately type-blind — it asks only about levels and adjacency,
never about the parent's `element_type` — so a container type added later is
covered without being listed. If you do implement it yourself, keep that property:
enumerating the container types you happen to know is what lost `table`, `deflist`
and `lineblock` inside every container, in code that was correct when written.

**Migrating from 5.0.** A consumer that reads a container's `content` needs no
change — that has been required since v1. A consumer that walks for a `plain` child
must also read `content`, and one that WRITES `list_item > plain` should write the
content instead; the old shape still converts, but it is no longer what readers emit.

**Leaf types are unchanged by 2.0, and keep their either/or.** `paragraph`,
`heading`, `code` and `raw` carry text in `content` **when that text is a single
plain run**. A leaf with rich inline content instead has empty `content` and
`inline` children at `level + 1`:

```
paragraph  content="just text"        <- plain: content populated, no children
paragraph  content=""                 <- rich: content empty, children carry it
  text   "plain"
  bold
    text "x"
```

This is the 1.x rule and 2.0 did not touch it. A reader emitting ordinary prose
needs no migration — only `list`, `list_item` and `blockquote` changed shape.

```
list        attrs list_type, and for ordered: start, number_style, number_delim
  list_item                          <- level+1, no content
    paragraph  "the item's words"    <- level+2
blockquote
  paragraph  "the quoted words"      <- level+1
```

All of these produce exactly that shape:

```sql
duck_block_list(['a'])            duck_block_list_block(['a'])
duck_block_list(false, ['a'])     duck_block_list_block([duck_block_list_item('a')])
pandoc_ast_to_blocks(...)         duck_block_list_item('a')   -- item + paragraph
```

**Migrating from 1.x.** `list` no longer carries `encoding='json'` with its items
packed into `content`; `list_item` and `blockquote` no longer carry their text in
`content`. A reader that walks children needs no change. A reader that parsed the
JSON, or read `list_item.content`, must read the child elements instead. Both old
forms are still ACCEPTED on export, so stored 1.x block lists keep converting —
but nothing produces them any more.

**Only `table` carries JSON, and it carries the NATIVE schema.**

**`attributes['pandoc_ast']` is a PRESERVATION SLOT, not a requirement.** Asked by the
panduck session, whose readers parse XHTML and LaTeX directly and never hold a Pandoc
tuple to preserve.

| you have | do |
|---|---|
| a source tuple (you started from Pandoc JSON) | emit `{headers, rows}` **and** keep the tuple verbatim in `attributes['pandoc_ast']` |
| no source tuple (you parsed HTML, LaTeX, XML…) | emit `{headers, rows}` and **omit** the attribute |

Absence means *no tuple existed*, and a producer that HAS one and drops it is
non-conformant. **Do not synthesise a tuple you never parsed** — panduck raised that
option and rejected it themselves, correctly: a constructed tuple puts a fabricated
artifact in the slot reserved for the authentic one, and any lossiness in the
construction is then preserved as though it were faithful.

Nothing can enforce this. A consumer cannot tell "no tuple existed" from "the producer
forgot", because a dropped tuple leaves no trace — so it is a rule rather than a check,
and it is stated here precisely because no instrument will catch a violation.

**THE PROJECTION IS LOSSY, and for a producer with no tuple the loss is permanent.**
`{headers, rows}` does not express `colspan`, `rowspan`, per-column alignment, or
multiple table bodies. Where a tuple is preserved, the truth survives beside the
projection. Where there is none — panduck's EPUB `<td colspan="2">` — the span is
simply gone, with nothing holding it.

That is real fidelity lost by conforming, and it is named here rather than left to be
discovered: a producer in that position should say so plainly rather than let
"conformant" imply more than it does. panduck's framing, and they are right that the
honest move is to document it in the reader rather than let the label carry it.

| Type | content | also |
|------|---------|------|
| `table` | `{"headers": [...], "rows": [[...]]}` | `attributes['pandoc_ast']` holds the full Pandoc tuple |

`table` previously emitted the raw Pandoc tuple, which nothing understood: tables
rendered as NOTHING and `duck_blocks_to_text` returned the raw AST, so cell text
was unsearchable while `AlignDefault` matched every search. That was the same
two-schemas-under-one-element_type defect `list` had — the native schema was
already understood by this extension's renderer and `render_macros.cpp` (measured),
and — as reported by those sessions on 2026-08-31, not verified here — by
duckdb_markdown's writer and webbed's decoder. The reader emitted the other one.

The native projection is lossy — it flattens colspan, rowspan, alignment and
multiple bodies — which is exactly why `attributes['pandoc_ast']` keeps the tuple
verbatim. The renderable form lives in `content`, the faithful form beside it, and
export prefers the tuple so round trips stay byte-identical. Nothing is lost.

`deflist` is no longer emitted at all — see below.

Everything else is `text`, **including `heading` and `paragraph`** — a consumer
writing a defensive JSON branch for those is guarding nothing.

Note the separate native table schema `{"headers": [...], "rows": [[...]]}`, which
the ANSI renderer and `render_macros.cpp` understand. The Pandoc reader does not
emit it, which is why a Pandoc table renders as nothing today.

The set is release-dependent: `deflist` and `lineblock` produce no blocks at all
from the shipped v1.6.1 binary, so a consumer cannot determine this set by
experiment without knowing which build it tested. Assert against the running build
rather than assuming.

*Measured 2026-08-31 by the duckeye and duckdb_markdown sessions against both the
shipped binary and main, after three implementations hit the same four defects.*

## Validation Rules

A duck_block is **canonical** if:

### Block Validation (kind='block')

1. `kind` is `'block'`
2. `element_type` is a recognized block type (or custom type with `x-` prefix)
3. `element_order` is non-negative integer
4. `level` is the element's structural depth; the heading's semantic rank is in `attributes['heading_level']`
5. For headings: `attributes['heading_level']` is '1'-'6'
6. `encoding` matches content format
7. `attributes` keys are valid identifiers

### Inline Validation (kind='inline')

1. `kind` is `'inline'`
2. `element_type` is a recognized inline type
3. `level` >= 1
4. `element_order` >= 0
5. For container types: content is empty OR no nested children exist
6. For leaf types: content contains the actual content
7. Whitespace types (`space`, `softbreak`, `linebreak`): content is empty

## Validation Macro

Extensions can use these macros to validate duck_blocks without depending on
duck_block_utils.

> **COPY `vendor/duck_block_conformance.sql`, not the snippet below.** That file
> is the maintained one: `test/check_conformance_macro.py` runs it and the real
> `duck_blocks_validate()` over the same corpus on every `make check` and FAILS on any
> disagreement, including cases taken from real reader output rather than hand-built.
>
> The snippet here is an excerpt for reading. It cannot be checked, and an unchecked
> second copy of a rule is how this document came to publish a validator that
> **rejected conforming metadata as invalid** — it enumerated two kinds when three
> exist. Two copies of one rule, maintained by one party, cannot detect their own
> disagreement; comparing them can.
>
> **This matters more than it sounds.** An extension that vendors its own DuckDB
> submodule pinned off the release tag cannot load duck_block_utils by ANY route —
> DuckDB matches extension ABI by exact version string, and publishing does not change
> it. For that class of consumer these macros are the *only* conformance available.
> The webbed session's metadata blocks carried a NULL `level` for three major spec
> versions with nothing in a position to object, because the check that would have
> caught it was one they structurally could not run.

Excerpt:

```sql
-- Validate a single duck_block
CREATE OR REPLACE MACRO duck_block_is_valid(elem) AS (
    elem.kind IN ('block', 'inline', 'value')  -- omitting 'value' rejects all metadata
    AND elem.element_type IS NOT NULL
    AND elem.level >= 1        -- explicit structural depth, never NULL
    AND elem.element_order >= 0
    AND (
        elem.kind != 'block'
        OR elem.element_type != 'heading'
        OR (
            -- A heading carries BOTH: `level` is its structural depth in the tree,
            -- heading_level its semantic rank. They are independent numbers.
            elem.level >= 1
            AND (elem.attributes['heading_level'] IS NULL
                 OR elem.attributes['heading_level']::INTEGER BETWEEN 1 AND 6)
        )
    )
    AND (
        elem.kind != 'inline'
        OR elem.level >= 1
    )
);

-- Check if content field usage is canonical for container inline
CREATE OR REPLACE MACRO duck_block_content_is_canonical(elem, has_nested_children) AS (
    CASE
        -- Blocks always use content
        WHEN elem.kind = 'block' THEN true
        -- Inline leaf types: always use content
        WHEN elem.element_type IN ('text', 'code', 'math', 'raw', 'cite', 'note') THEN true
        -- Whitespace: content must be empty
        WHEN elem.element_type IN ('space', 'softbreak', 'linebreak') THEN
            elem.content = '' OR elem.content IS NULL
        -- Container types: content XOR nested children
        ELSE (length(coalesce(elem.content, '')) > 0) != has_nested_children
    END
);
```

## Compatibility Notes

### Pandoc AST Mapping

Duck_blocks representation is designed to be convertible to/from Pandoc AST:

- Pandoc's nested inlines map to our flat representation with levels
- Container types (Strong, Emph, Link, etc.) become level markers
- Round-trip conversion should preserve structure
- The `kind` field maps to Pandoc's block/inline distinction

### Vendorable utilities (SUGGESTED, not required)

`vendor/` holds files meant to be **copied into your own repository**. Using them is
suggested, never required — conformance is defined by this document, not by any
implementation of it.

| file | gives you |
|---|---|
| `vendor/duck_block_conformance.sql` | `duck_blocks_are_valid`, `duck_blocks_errors` (with `{element_order, field, message}` detail), `duck_blocks_warnings` (advisory rules), `duck_blocks_undeclared_types`, and the declared kind / type / encoding lists — pure DuckDB SQL |
| `vendor/duck_block_normalize.hpp` | the content rule as a transform: collapse a lone `plain` into its container, to a fixpoint |
| `src/include/duck_block_vocabulary.hpp` | the names as C++ constants |

**Why this exists, and it is not convenience.** DuckDB matches extension ABI by exact
version string, so an extension whose DuckDB submodule is pinned off the release tag
**cannot load `duck_block_utils` by any route** — and publishing does not change that.
Two of the three consuming extensions are in that position. Without these files a
producer must either take a dependency it cannot load, or reimplement the rules by
hand; the second is how three producers arrived at three different answers for
document metadata, and how the content rule came to exist in two layers with two
different bugs.

**Copy or extract what is prepared rather than re-deriving it.** If you need a rule
that is not here, ask for it to be added — a shared rule with no distributable home is
the condition that produced every divergence this specification has had to correct.

**Every file there is compared against the extension, not merely maintained.** A
duplicate that nothing checks is how an image's alt text stayed invisible in this repo
for months. `make check` fails if the vendorable SQL and the extension disagree on a
verdict, on error detail, or on either declared list; `duck_block_normalize.hpp` is
the implementation the extension itself calls, so it has no duplicate to disagree
with.

### Extension Responsibilities

Extensions that produce duck_blocks:
- MUST generate valid structures per this spec
- **MUST NOT DISCARD DOCUMENT METADATA.** If the source format carries a title,
  author, date or equivalent, emit it as `kind='value'` (see "Value Types" below)
  rather than dropping it or placing it in the body.

  This is stated because, as measured and reported by those sessions on 2026-09-01,
  two of the three consuming extensions **dropped every document's title and author**
  — webbed never visited `<head>`, and none of panduck's five readers extracted
  metadata. Both were migrating when this was written, so treat it as the situation
  that motivated the rule rather than as current fact. This document is a plausible
  cause of it:
  until 2026-09-01 it said producers MUST set `kind` to `'block'` or `'inline'`,
  which leaves a conforming producer with **nowhere to put a title**. A producer that
  followed that sentence had two options, both wrong: put it in the body as prose, or
  drop it.

  A discard is the worse of the two. A leak is visible and correctable; a discarded
  title is unrecoverable and indistinguishable from a document that never had one.
- MUST set `kind` to one of `'block'`, `'inline'` or `'value'` — the authoritative
  list is `duck_block_kind_names()`. **This said "either `'block'` or `'inline'`"
  until 2026-09-01**, which is the instruction that produces the metadata leak: a
  producer following it has nowhere to put a document's own title, and puts it in the
  body as prose. `value` has existed for as long as document metadata has. The conformance macro above had
  the same omission, and it is published for extensions to copy — copied as written,
  it rejected every conforming metadata element as invalid.
- SHOULD use canonical content representation
- MUST set appropriate encoding for block content

Extensions that consume duck_blocks:
- MUST handle both content-field and nested-children representations
- SHOULD validate input before processing
- MUST preserve unrecognized attributes

## Changelog

- 0.4.0: Moved heading level from `level` field to `attributes['heading_level']` to separate semantic heading levels from structural nesting depth
- 0.3.0: Unified doc_block and doc_inline into single duck_block type with `kind` discriminator
- 0.2.0: Added Option C content rules for container types
- 0.1.0: Initial draft specification

## A heading may carry BOTH a flattened title and rich children

Ruling, 2026-09-01, after duckdb_markdown found their reader silently normalising
`# **Bold** title` and `# Bold title` to byte-identical output — undeclared and
irreversible, the exact category the metadata fixtures forbid, on the axis neither of
us was checking.

Their fix is additive and the shape is worth having in the vocabulary: `content` keeps
the flattened title because that is what a title IS for — slugs, section lookup and
`section_id` all read that field — and the formatting lives in inline children beside
it.

**This is a deliberate, narrow exception to the content rule, and it needs no new
vocabulary because the structure already carries the marker.**

```
heading  content='Bold title'   + inline children   -> content is a DERIVED flattening
heading  content='Plain title'  + no children       -> content IS the text
```

A single text child lives in `content` and produces no children, so **children present
alongside non-empty content can only mean the content is derived.** No attribute is
needed to say so.

- **Children are authoritative whenever both are present.** Measured, not asserted:
  this repo's exporter already reads the children and ignores the derived content, so
  markdown's shape round-trips byte-exact through it today.
- **`heading` only.** A title-string role genuinely distinct from rich content is a
  property of headings. Elsewhere, two copies of one fact is the shape that hid the
  image-alt loss here for months — written to both `content` and `attributes['alt']`,
  read from only one, and every round trip inside one repo came back perfect while any
  other producer lost it silently.

A consumer that reads only `content` gets a correct plain title and loses the
formatting, which is the benign failure. A consumer that reads only children gets the
formatting. Neither gets a wrong answer, which is the property that makes the
duplication tolerable here and not in general.

## Persisting duck_blocks: use PARQUET_VERSION v2

Measured 2026-09-01/02 on this spec document (14,969 elements) against the same
document as a pandoc AST. duck_blocks is a QUERY representation, not a wire format,
and it is larger than pandoc's AST for a structural reason: **98% of its rows are
inline**, and each pays explicitly for structure that pandoc's nesting gets for free.

Where the bytes go, per column, parquet v1 + zstd-22:

```
content        32,718     the text -- irreducible
element_order  15,512     a near-monotonic counter, 27% of the file
attributes      6,054
element_type    1,900
level           1,229
kind              780
encoding          386
```

**`element_order` was the whole problem, and PARQUET_VERSION v2 solves it.** 14,935 of
its 14,968 deltas are exactly 1; v1 stores it as raw INT32 while v2 reaches for
DELTA_BINARY_PACKED. The same column:

```
element_order alone      v1  15,512      v2  320       48x
```

Whole document:

```
                        v1        v2
uncompressed        204,817   145,271
zstd (default)       82,490    47,029     <- v2 default beats v1 at level 22
zstd level 22        57,987    42,797
brotli                    -    39,555     <- best measured
```

**Set `PARQUET_VERSION v2` and stop there.** v2 at DEFAULT zstd is smaller than v1 at
level 22, for a fraction of the CPU. Level 22 buys another 9%; brotli another 8% beyond
that. Verified to reproduce a byte-identical exported AST.

**Do NOT hand-roll a delta column.** It was the right fix under v1 -- 397 bytes against
15,512 -- and under v2 it is actively counterproductive: 42,897 against 42,797, because
a manually differenced column is less compressible than the original once the format
delta-encodes it itself. Recorded rather than deleted because the reasoning was sound
and its premise expired.

**And do NOT drop `element_order` for the row number.** It is monotonic but NOT
contiguous: this reader allocates a number for the `plain` wrapper it collapses into a
`list_item`, so any document with lists has gaps. Nothing is lost -- the round trip is
byte-exact -- but 12,059 of 14,969 values differ from their row index, and a consumer
assuming otherwise would silently renumber the document.

**Against pandoc, compare the forms each is actually delivered in:** pandoc as flat
JSON, duck_blocks as parquet. Nobody ships a pandoc AST inside parquet -- it is one
deeply nested column that does not columnarise, and its uncompressed parquet is larger
than its own JSON.

```
                         pandoc JSON   duck_blocks parquet v2
duck_blocks_spec.md (14,969 elements)
  uncompressed              368,592          145,271     duck_blocks 2.54x SMALLER
  zstd (default)             27,883           47,029     pandoc 1.69x smaller
  best (xz / brotli)         27,196           39,555     pandoc 1.45x smaller

README.md (1,957 elements)
  uncompressed              105,710           81,982     duck_blocks 1.29x SMALLER
  zstd (default)              9,932           18,965     pandoc 1.91x smaller
  best (xz / brotli)          9,860           15,332     pandoc 1.55x smaller
```

**The sign flips on compression, and both directions are real.** Uncompressed,
duck_blocks parquet is the smaller file -- v2's dictionary and delta encodings do the
work a general compressor would otherwise have to. Compressed, pandoc wins by 1.5-1.9x,
because its nesting never spends bytes on structure that a compressor then has to
squeeze back out.

**The two documents disagree on magnitude, and that is worth more than either number.**
The uncompressed advantage is 2.54x on the long document and 1.29x on the short one:
parquet's per-column dictionaries amortise over rows, so the longer and more repetitive
the document, the better duck_blocks does. A single measurement here would have
generalised badly in whichever direction it happened to fall.

**Orientation matters more than the container, and column-major JSON beats parquet.**
The same rows, reshaped three ways:

```
duck_blocks_spec.md            raw     +zstd19        +xz
  row objects (JSONL)      1,910,669     76,802     63,200
  header + rows (arrays)     713,824     68,451     60,064
  column-major               683,879     38,471     34,600   <- best duck_blocks form
  parquet v2 (brotli)        145,271          -     39,555
  pandoc JSON                368,592          -     27,196

README.md
  row objects (JSONL)        318,095     18,801     16,872
  header + rows (arrays)     161,795     17,592     16,304
  column-major               157,874     14,039     12,988   <- best duck_blocks form
  parquet v2 (brotli)         81,982          -     15,332
  pandoc JSON                105,710          -      9,860
```

Column-major -- one array per field, `{"kind":[...],"element_type":[...],...}` --
is **13-16% smaller than parquet** and closes the gap with pandoc from 1.45x to 1.27x.
It groups like values the way parquet does, and then lets a single xz stream find
long-range redundancy ACROSS columns that parquet cannot, because parquet compresses
each column chunk independently and adds page and footer overhead. Verified lossless:
reshaping back reproduces the rows exactly and the same exported AST.

**Two things not to conclude from the raw column.** Header-plus-arrays cuts the raw
size 2.7x by removing repeated field names and saves only 5% after compression --
repeated keys are exactly what a compressor eliminates for free. Optimising the raw
size of a payload you are going to compress is close to wasted effort. And parquet's
raw size (145,271) is the smallest of the uncompressed forms while its compressed size
is not the smallest -- the encodings that win before compression are not the ones that
win after.

**Row group size matters in one direction only.** Parquet compresses each column chunk
independently WITHIN a row group, so more groups means more independent streams and
worse ratios. Measured on the spec document, v2 + brotli:

```
ROW_GROUP_SIZE     file      groups   data only
         1,024   56,502           8      48,294     +43%
         4,096   48,515           4      44,279     +23%
        15,000   39,555           1      34,087     baseline
     1,000,000   39,555           1      34,087     no change
```

Once the table fits in ONE row group there is nothing left to tune -- DuckDB's default
of 122,880 rows already covers a 15,000-element document, so the file was a single row
group before any of this. Shrinking it is the only lever, and it only costs.

**Where parquet's remaining gap actually lives, measured rather than assumed.** Summing
the column chunks of the spec document gives 34,087 bytes against a 39,555-byte file --
so **5,468 bytes, 14%, is footer and page metadata**, and parquet's compressed DATA is
slightly smaller than column-major xz's 34,600. On one mid-sized document the whole
deficit is overhead.

That stops being true at corpus scale, and the reason is worth knowing. 20 distinct
documents, 33,716 elements, one row group:

```
pandoc JSON concatenated + xz      86,808
duck_blocks column-major + xz     105,280
duck_blocks parquet v2 brotli     133,031    (data 121,799 + overhead 11,232)
```

Overhead has fallen to 8%, but parquet's DATA is now larger than column-major xz --
121,799 against 105,280. Cross-document redundancy is real, and a single xz stream over
concatenated columns finds it where per-column-chunk brotli does not. So parquet's
disadvantage shifts from metadata to compression window as the corpus grows.

**A caution about how not to measure this.** Ten copies of the same two documents made
column-major look 2.8x better than parquet -- an artifact of long-range matching over
identical text, not a property of either format. Duplicating a document is not a corpus.
The numbers above are 20 genuinely different files.

**What each buys.** pandoc's compressed JSON is an opaque blob: to ask anything of it
you decompress and parse all of it. duck_blocks parquet answers a predicate over one
column without touching the rest, and is smaller on disk before compression. The
1.5x is what querying in place costs when you also compress -- and nothing at all when
you do not.

## Two extensions must not register the same function name

Measured by panduck 2026-09-01, before writing any converter code, because the
converter relocation puts duck_block_utils and panduck briefly in possession of the
same functions. The outcome is none of the three anyone predicted:

```
LOAD both            -> SUCCEEDS, either order, no error
duckdb_functions()   -> TWO overloads of the same name
calling it           -> Binder Error: Could not choose a best candidate function
                          pandoc_ast_to_blocks(VARCHAR) -> STRUCT(...)[]
                          pandoc_ast_to_blocks(VARCHAR) -> VARCHAR
```

Both registrations survive as ambiguous overloads and **every call fails at bind
time.** Overload resolution keys on parameters, which are identical; the differing
return types cannot break the tie and are what make it unresolvable.

Better than silent shadowing — nobody gets the wrong implementation without knowing —
and worse in the way that matters operationally: it does not degrade, it BREAKS, and
the error names a construct the user did not write.

**So a name is owned by exactly one extension in this family.** The converter
relocation resolves it by panduck registering NEW names (`read_pandoc_blocks`,
`read_pandoc_blocks_string`, table functions it wanted anyway) while
`pandoc_ast_to_blocks` and friends stay here until this repo's release cycle retires
them. No two-copy collision, and no hard cutover needed.

**What is measured, and what is not.** duck_block_utils (91 registered names) against
panduck (36): **zero shared**, with a positive control confirming the comparison can
find a collision. The other five pairs are UNMEASURED — markdown and webbed vendor a
DuckDB off the release tag, so neither can load into the same process as the others
today and no collision between them is observable. That check becomes possible when
the family aligns on one DuckDB version, and should not be written before then: a
check that can see one pair of six while printing a family-wide verdict is worse than
none.

