# Duck Blocks Canonical Specification

**Version:** 0.1.0 (Draft)
**Status:** Under Development (See Issue #2)

This document defines the canonical representation for duck_blocks - structured document elements for DuckDB. Extensions that produce or consume duck_blocks (markdown, webbed, etc.) MUST conform to this specification.

## Type Definitions

### doc_block

```sql
STRUCT(
    block_type    VARCHAR,                      -- Block type identifier
    content       VARCHAR,                      -- Text content (see Content Rules)
    level         INTEGER,                      -- Semantic level (e.g., heading level)
    encoding      VARCHAR,                      -- Content encoding hint
    attributes    MAP(VARCHAR, VARCHAR),        -- Key-value metadata
    block_order   INTEGER                       -- Position in document
)
```

### doc_inline

```sql
STRUCT(
    inline_type   VARCHAR,                      -- Inline type identifier
    content       VARCHAR,                      -- Text content (see Content Rules)
    level         INTEGER,                      -- Nesting depth (1 = top level)
    attributes    MAP(VARCHAR, VARCHAR),        -- Key-value metadata
    inline_order  INTEGER                       -- Position in inline sequence
)
```

## Block Types

| Type | Description | level Usage | encoding Values |
|------|-------------|-------------|-----------------|
| `heading` | Section heading | 1-6 (h1-h6) | `text` |
| `paragraph` | Text paragraph | NULL | `text`, `markdown`, `doc_inlines` |
| `code` | Code block | NULL | `text` |
| `blockquote` | Quoted content | Nesting depth | `text`, `markdown` |
| `list` | List container | NULL | `json` (items array) |
| `hr` | Horizontal rule | NULL | NULL |
| `metadata` | YAML frontmatter | NULL | `yaml` |
| `image` | Block-level image | NULL | NULL (src in attrs) |
| `raw` | Raw format content | NULL | format name |

## Inline Types

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

**DRAFT - See Issue #2 for discussion**

### Proposed Rule

**`content` is populated if and only if the container has a single text child.**

This enables both ergonomic simple cases and full structured representation:

#### Simple Case (content populated)
```sql
-- Single text child: use content field
doc_link('Click here', 'https://example.com')

-- Returns:
{inline_type: 'link', content: 'Click here', level: 1,
 attributes: {href: 'https://example.com'}, inline_order: 0}
```

#### Complex Case (nested children)
```sql
-- Multiple or non-text children: use nested structure
doc_link([doc_bold('Important'), doc_text(' link')], 'https://example.com')

-- Returns LIST:
[{inline_type: 'link', content: '', level: 1, attrs: {href: '...'}},
 {inline_type: 'bold', content: 'Important', level: 2},
 {inline_type: 'text', content: ' link', level: 2}]
```

### Level Field Semantics

- `level = 1`: Top-level inline element
- `level = N`: Nested N levels deep
- Children of a container at level N are at level N+1
- Siblings share the same level

### Inline Order Field Semantics

- Sequential ordering within the inline list
- Used to reconstruct document order
- Starts at 0 for each inline list

## Block Content Encoding

The `encoding` field indicates how to interpret `content`:

| Encoding | Description |
|----------|-------------|
| `text` | Plain text, no markup |
| `markdown` | Markdown-formatted text |
| `doc_inlines` | JSON-serialized LIST(doc_inline) |
| `yaml` | YAML content |
| `json` | JSON content |
| `html` | HTML content |
| `latex` | LaTeX content |

## Validation Rules

A doc_block or doc_inline is **canonical** if:

### Block Validation

1. `block_type` is a recognized type (or custom type with `x-` prefix)
2. `block_order` is non-negative integer
3. `level` is appropriate for block_type (1-6 for headings, NULL or valid for others)
4. `encoding` matches content format
5. `attributes` keys are valid identifiers

### Inline Validation

1. `inline_type` is a recognized type
2. `level` >= 1
3. `inline_order` >= 0
4. For container types: content is empty OR no nested children exist
5. For leaf types: content contains the actual content
6. Whitespace types (`space`, `softbreak`, `linebreak`): content is empty

## Validation Macro

Extensions can use this macro to validate duck_blocks without depending on duck_block_utils:

```sql
-- Validate a single doc_block
CREATE OR REPLACE MACRO doc_block_is_valid(block) AS (
    block.block_type IS NOT NULL
    AND block.block_order >= 0
    AND (block.block_type != 'heading' OR block.level BETWEEN 1 AND 6)
);

-- Validate a single doc_inline
CREATE OR REPLACE MACRO doc_inline_is_valid(inline) AS (
    inline.inline_type IS NOT NULL
    AND inline.level >= 1
    AND inline.inline_order >= 0
    AND (
        -- Leaf types should have content (except whitespace)
        inline.inline_type IN ('space', 'softbreak', 'linebreak')
        OR inline.inline_type NOT IN ('text', 'code', 'math', 'raw')
        OR length(inline.content) > 0
    )
);

-- Check if content field usage is canonical for container inline
CREATE OR REPLACE MACRO doc_inline_content_is_canonical(inline, has_nested_children) AS (
    CASE
        -- Leaf types: always use content
        WHEN inline.inline_type IN ('text', 'code', 'math', 'raw', 'cite', 'note') THEN true
        -- Whitespace: content must be empty
        WHEN inline.inline_type IN ('space', 'softbreak', 'linebreak') THEN
            inline.content = '' OR inline.content IS NULL
        -- Container types: content XOR nested children
        ELSE (length(coalesce(inline.content, '')) > 0) != has_nested_children
    END
);
```

## Compatibility Notes

### Pandoc AST Mapping

Duck_blocks inline representation is designed to be convertible to/from Pandoc AST:

- Pandoc's nested inlines map to our flat representation with levels
- Container types (Strong, Emph, Link, etc.) become level markers
- Round-trip conversion should preserve structure

### Extension Responsibilities

Extensions that produce duck_blocks:
- MUST generate valid structures per this spec
- SHOULD use canonical content representation
- MUST set appropriate encoding for block content

Extensions that consume duck_blocks:
- MUST handle both content-field and nested-children representations
- SHOULD validate input before processing
- MUST preserve unrecognized attributes

## Changelog

- 0.1.0: Initial draft specification
