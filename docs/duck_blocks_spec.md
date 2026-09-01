# Duck Blocks Canonical Specification

**Version:** 0.4.0
**Status:** Canonical (Unified duck_block type, heading_level attribute)

This document defines the canonical representation for duck_blocks - structured document elements for DuckDB. Extensions that produce or consume duck_blocks (markdown, webbed, etc.) MUST conform to this specification.

## Type Definition

### duck_block (Unified Type)

Both block-level and inline elements use the same unified type, distinguished by the `kind` field:

```sql
STRUCT(
    kind          VARCHAR,                      -- 'block' or 'inline'
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

Sibling extensions should consume the vocabulary as a **submodule**, not a copy:

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

Even with a submodule, **assert agreement at test time** — the introspection functions
above catch a submodule that was never synced, which a compile cannot.

## Block Types (kind='block')

| Type | Description | level Usage | encoding Values | Key Attributes |
|------|-------------|-------------|-----------------|----------------|
| `heading` | Section heading | NULL | `text` | `heading_level` (1-6) |
| `paragraph` | Text paragraph | NULL | `text`, `markdown` | |
| `code` | Code block | NULL | `text` | `language` |
| `blockquote` | Quoted content | Nesting depth | `text`, `markdown` | |
| `list` | List container | NULL | `json` (items array) | `ordered`, `list_type` |
| `list_item` | List item | Nesting depth | `text` | |
| `deflist` | Definition list | NULL | `json` | |
| `lineblock` | Preserved line breaks | NULL | `text` (lines joined with `\n`) | |
| `table` | Table | NULL | `json` | |
| `hr` | Horizontal rule | NULL | `text` | |
| `page_break` | **Physical** page boundary — a marker, not a container | NULL | `text` | `page_number` |
| `metadata` | YAML frontmatter | 0 | `yaml` | |
| `image` | Block-level image | NULL | `text` | `src`, `alt`, `title` |
| `raw` | Raw content in a *named* format | NULL | format name | `format` |
| `div` | Generic container | Nesting depth | `text` | `id`, `class` |
| `section` | **Semantic** sectioning container | Nesting depth | `text` | `role`, `id`, `class` |
| `figure` | Figure: content plus a caption | Nesting depth | `text` | `id`, `class` |
| `caption` | Caption belonging to the container before it | Nesting depth | `text` | `short_caption` |
| `generic` | Structurally valid, type not in this vocabulary | NULL | `json` | `source_type` |

### Containers nest by `level`

`div`, `section`, `figure` and `caption` carry no content of their own. Their
children follow them in document order at `level + 1`, and the container ends at
the first element back at its own level. This is the same mechanism throughout —
there is no separate child-list field.

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

The `level` field represents **structural nesting depth**, not semantic level:

- **For inlines**: `level = 1` is top-level, `level = N` is nested N levels deep
- **For blockquotes**: nesting depth (1 = single quote, 2 = nested quote)
- **For headings**: NULL (semantic level is in `attributes['heading_level']`)
- **For other blocks**: typically NULL

Children of a container at level N are at level N+1. Siblings share the same level.

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

## Validation Rules

A duck_block is **canonical** if:

### Block Validation (kind='block')

1. `kind` is `'block'`
2. `element_type` is a recognized block type (or custom type with `x-` prefix)
3. `element_order` is non-negative integer
4. `level` is NULL for headings (heading level is in attributes)
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
    elem.kind IN ('block', 'inline')
    AND elem.element_type IS NOT NULL
    AND elem.element_order >= 0
    AND (
        elem.kind != 'block'
        OR elem.element_type != 'heading'
        OR (
            -- For headings: level should be NULL and heading_level attribute should be 1-6
            (elem.level IS NULL OR elem.level BETWEEN 1 AND 6)  -- Allow level for backward compat
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
- MUST set `kind` to either `'block'` or `'inline'`
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
