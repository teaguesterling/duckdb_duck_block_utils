# Duck Blocks Canonical Specification

**Version:** 0.4.0
**Status:** Canonical (Unified doc_element type, heading_level attribute)

This document defines the canonical representation for duck_blocks - structured document elements for DuckDB. Extensions that produce or consume duck_blocks (markdown, webbed, etc.) MUST conform to this specification.

## Type Definition

### doc_element (Unified Type)

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

## Block Types (kind='block')

| Type | Description | level Usage | encoding Values | Key Attributes |
|------|-------------|-------------|-----------------|----------------|
| `heading` | Section heading | NULL | `text` | `heading_level` (1-6) |
| `paragraph` | Text paragraph | NULL | `text`, `markdown` | |
| `code` | Code block | NULL | `text` | `language` |
| `blockquote` | Quoted content | Nesting depth | `text`, `markdown` | |
| `list` | List container | NULL | `json` (items array) | `ordered` |
| `table` | Table | NULL | `json` | |
| `hr` | Horizontal rule | NULL | `text` | |
| `metadata` | YAML frontmatter | 0 | `yaml` | |
| `image` | Block-level image | NULL | `text` | `src`, `alt`, `title` |
| `raw` | Raw format content | NULL | format name | `format` |

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
| `underline` | Underlined | See below | - |
| `link` | Hyperlink | See below | `href`, `title` |
| `image` | Inline image | Alt text | `src`, `alt`, `title` |
| `quoted` | Quoted text | See below | `quote_type`: single/double |
| `span` | Generic container | See below | `id`, `class` |
| `cite` | Citation | Key | `key`, `prefix`, `suffix` |
| `note` | Footnote | Content | - |

## Content Rules for Container Types

**`content` is populated if and only if the container has a single text child.**

This enables both ergonomic simple cases and full structured representation.

### Simple Case: String Argument → Single Struct

When a container builder receives a simple string, it returns a single `doc_element` struct
with the text in the `content` field:

```sql
doc_link('Click here', 'https://example.com')

-- Returns doc_element struct:
{kind: 'inline', element_type: 'link', content: 'Click here', level: 1,
 encoding: 'text', attributes: {href: 'https://example.com'}, element_order: 0}
```

### Complex Case: List Argument → LIST with Nested Children

When a container builder receives a list of inlines, it returns a `LIST(doc_element)`
with the container as first element (empty content) and children at level+1:

```sql
doc_link([doc_bold('Important'), doc_text(' link')], 'https://example.com')

-- Returns LIST(doc_element):
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
doc_bold('text')
doc_bold([doc_text('text')])

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

A doc_element is **canonical** if:

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
-- Validate a single doc_element
CREATE OR REPLACE MACRO doc_element_is_valid(elem) AS (
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
CREATE OR REPLACE MACRO doc_element_content_is_canonical(elem, has_nested_children) AS (
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
- 0.3.0: Unified doc_block and doc_inline into single doc_element type with `kind` discriminator
- 0.2.0: Added Option C content rules for container types
- 0.1.0: Initial draft specification
