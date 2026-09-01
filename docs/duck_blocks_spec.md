# Duck Blocks Canonical Specification

**Version:** 6.1 — this MUST equal `duck_block_spec_version()`, and
`test/check_spec_alignment.py` fails if it does not. It read `0.4.0` from v1.0.0
through 2026-08-31 while the shipped value moved to 6.0, so a reader trusting the
document disagreed with every consumer asserting the function.
**Status:** Canonical (Unified duck_block type, heading_level attribute)

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

Spec 1.x and 2.0 documented a NULL at top level. That convention was never
approved and is removed in 3.0: NULL and 1 both resolved to depth 1, so a child at
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
| `metadata` | YAML frontmatter | depth (top level 1) | `yaml` | |
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

> This paragraph said containers "carry no content of their own" until spec 6.1, which
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

Metadata is appended **after** a document's blocks, so `blocks[1]` still points at the
first content block — but see the `kind` filtering rule above; ordering is a
convenience, not a contract.

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
`attributes['ordered']` = 'true'/'false' is a legacy alias, kept because spec 1.0
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
whatever it actually is. That is a live hazard, not a hypothetical: it is a fair
reading of spec 1.x/2.0, which said headings carried NULL there, and 3.0 turns it
into silent corruption. Default to 1 and treat the block as malformed;
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

The `encoding` field indicates how to interpret `content`:

| Encoding | Description |
|----------|-------------|
| `text` | Plain text, no markup |
| `markdown` | Markdown-formatted text |
| `yaml` | YAML content |
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

Relatedly, "no content of its own" is spelled two ways: the block builders write
NULL, the Pandoc reader writes an empty string. Treat both as absent.

**`content` is populated IF AND ONLY IF the container has a single text child.**
This is spec v1.0's rule and it covers inline and block containers alike:

```
duck_block_blockquote('x')          blockquote content='x'      <- single text child
duck_block_blockquote([p1, p2])     blockquote content=NULL
                                      paragraph, paragraph      <- children at level+1
duck_block_bold('y')                bold content='y'
duck_block_bold([text, italic])     bold content=NULL + children
```

One rule, two inputs, two representations — not two shapes for one input. Spec 2.0
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

**As of spec 6.0, `plain` appears only where the text has nowhere else to live.**
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

Spec 5.0 shipped the first column as `list_item > plain(...)`, which gave a
container with a single text child **two** legal shapes depending on which producer
built it. Collapsing that back to one is the whole point of every version since 2.0.

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

**As of spec 5.0 only `table` carries JSON, and it carries the NATIVE schema.**

| Type | content | also |
|------|---------|------|
| `table` | `{"headers": [...], "rows": [[...]]}` | `attributes['pandoc_ast']` holds the full Pandoc tuple |

`table` previously emitted the raw Pandoc tuple, which nothing understood: tables
rendered as NOTHING and `duck_blocks_to_text` returned the raw AST, so cell text
was unsearchable while `AlignDefault` matched every search. That was the same
two-schemas-under-one-element_type defect `list` had — the native schema was
already understood by this extension's renderer, `render_macros.cpp`,
duckdb_markdown's writer and webbed's decoder, and the reader emitted the other one.

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

Extensions can use this macro to validate duck_blocks without depending on duck_block_utils:

```sql
-- Validate a single duck_block
CREATE OR REPLACE MACRO duck_block_is_valid(elem) AS (
    elem.kind IN ('block', 'inline', 'value')  -- 'value' since 3.0; omitting it rejects all metadata
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

### Extension Responsibilities

Extensions that produce duck_blocks:
- MUST generate valid structures per this spec
- **MUST NOT DISCARD DOCUMENT METADATA.** If the source format carries a title,
  author, date or equivalent, emit it as `kind='value'` (see "Value Types" below)
  rather than dropping it or placing it in the body.

  This is stated because two of the three consuming extensions currently **drop
  every document's title and author** — webbed never visits `<head>`, and none of
  panduck's five readers extracts metadata — and this document is a plausible cause:
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
  body as prose. `value` has existed since spec 3.0. The conformance macro above had
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
