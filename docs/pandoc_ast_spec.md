# Pandoc AST Conversion Specification

This document specifies the bidirectional conversion between [Pandoc's JSON AST](https://hackage.haskell.org/package/pandoc-types) and the [Document Block Specification](https://github.com/teaguesterling/duckdb_markdown/blob/main/docs/doc_block_spec.md).

The `duck_block_utils` extension provides these conversions without requiring Pandoc itself - enabling interoperability with any tool that produces or consumes Pandoc-format JSON.

## Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    Pandoc JSON AST                               │
│  (nested structure, inline elements, rich attributes)            │
└─────────────────────────────────────────────────────────────────┘
                              │
            pandoc_ast_to_blocks() / duck_blocks_to_pandoc_blocks()
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    doc_blocks (flat rows)                        │
│  (sequential blocks, text content, MAP attributes)               │
└─────────────────────────────────────────────────────────────────┘
```

## Pandoc AST Structure

### Document Structure

```json
{
  "pandoc-api-version": [1, 23, 1],
  "meta": { /* document metadata */ },
  "blocks": [ /* array of block elements */ ]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `pandoc-api-version` | int[] | API version (major, minor, patch) |
| `meta` | object | Document metadata (title, author, etc.) |
| `blocks` | array | Ordered list of block elements |

### Block Elements

Each block is an object with `t` (type) and `c` (content):

```json
{"t": "BlockType", "c": [ /* type-specific content */ ]}
```

#### Header
```json
{
  "t": "Header",
  "c": [
    1,                           // level (1-6)
    ["section-id", ["class1"], [["key", "value"]]],  // Attr: (id, classes, kvpairs)
    [/* inline content */]       // heading text as inlines
  ]
}
```

#### Para (Paragraph)
```json
{
  "t": "Para",
  "c": [/* inline content */]
}
```

#### Plain (Paragraph without wrapping)
```json
{
  "t": "Plain",
  "c": [/* inline content */]
}
```

#### CodeBlock
```json
{
  "t": "CodeBlock",
  "c": [
    ["id", ["python", "class2"], [["linenos", "true"]]],  // Attr
    "def hello():\n    print('hi')"                        // code string
  ]
}
```

#### BlockQuote
```json
{
  "t": "BlockQuote",
  "c": [/* nested blocks */]
}
```

#### BulletList
```json
{
  "t": "BulletList",
  "c": [
    [/* blocks for item 1 */],
    [/* blocks for item 2 */],
    [/* blocks for item 3 */]
  ]
}
```

#### OrderedList
```json
{
  "t": "OrderedList",
  "c": [
    [1, {"t": "Decimal"}, {"t": "Period"}],  // (start, style, delimiter)
    [
      [/* blocks for item 1 */],
      [/* blocks for item 2 */]
    ]
  ]
}
```

#### Table (Pandoc 2.x+)
```json
{
  "t": "Table",
  "c": [
    ["", [], []],                    // Attr
    {"t": "Caption", "c": [null, [/* blocks */]]},
    [[{"t": "AlignDefault"}, {"t": "ColWidthDefault"}]],  // ColSpec[]
    {"t": "TableHead", "c": [["", [], []], [/* rows */]]},
    [{"t": "TableBody", "c": [["", [], []], 0, [/* header rows */], [/* body rows */]]}],
    {"t": "TableFoot", "c": [["", [], []], [/* rows */]]}
  ]
}
```

#### HorizontalRule
```json
{"t": "HorizontalRule"}
```

#### Div (Generic container)
```json
{
  "t": "Div",
  "c": [
    ["id", ["warning", "aside"], []],  // Attr
    [/* nested blocks */]
  ]
}
```

#### RawBlock
```json
{
  "t": "RawBlock",
  "c": ["html", "<div class='custom'>content</div>"]
}
```

#### LineBlock (Poetry/addresses)
```json
{
  "t": "LineBlock",
  "c": [
    [/* inlines for line 1 */],
    [/* inlines for line 2 */]
  ]
}
```

#### DefinitionList
```json
{
  "t": "DefinitionList",
  "c": [
    [
      [/* term inlines */],
      [[/* definition 1 blocks */], [/* definition 2 blocks */]]
    ]
  ]
}
```

#### Figure (Pandoc 3.0+)
```json
{
  "t": "Figure",
  "c": [
    ["fig-id", [], []],              // Attr
    {"t": "Caption", "c": [null, [/* caption blocks */]]},
    [/* content blocks */]
  ]
}
```

### Inline Elements

Inline elements appear within blocks (paragraphs, headings, etc.):

| Type | Content | Example |
|------|---------|---------|
| `Str` | string | `{"t": "Str", "c": "hello"}` |
| `Space` | (none) | `{"t": "Space"}` |
| `SoftBreak` | (none) | `{"t": "SoftBreak"}` |
| `LineBreak` | (none) | `{"t": "LineBreak"}` |
| `Emph` | inlines | `{"t": "Emph", "c": [/*inlines*/]}` |
| `Strong` | inlines | `{"t": "Strong", "c": [/*inlines*/]}` |
| `Strikeout` | inlines | `{"t": "Strikeout", "c": [/*inlines*/]}` |
| `Superscript` | inlines | `{"t": "Superscript", "c": [/*inlines*/]}` |
| `Subscript` | inlines | `{"t": "Subscript", "c": [/*inlines*/]}` |
| `SmallCaps` | inlines | `{"t": "SmallCaps", "c": [/*inlines*/]}` |
| `Quoted` | [type, inlines] | `{"t": "Quoted", "c": [{"t":"DoubleQuote"}, [/*inlines*/]]}` |
| `Code` | [Attr, string] | `{"t": "Code", "c": [["",["python"],[]], "x=1"]}` |
| `Math` | [type, string] | `{"t": "Math", "c": [{"t":"InlineMath"}, "E=mc^2"]}` |
| `Link` | [Attr, inlines, Target] | `{"t": "Link", "c": [["",[""],[]],[/*text*/],["url","title"]]}` |
| `Image` | [Attr, inlines, Target] | `{"t": "Image", "c": [["",[""],[]],[/*alt*/],["src","title"]]}` |
| `RawInline` | [format, string] | `{"t": "RawInline", "c": ["html", "<br>"]}` |
| `Note` | blocks | `{"t": "Note", "c": [/*footnote blocks*/]}` |
| `Span` | [Attr, inlines] | `{"t": "Span", "c": [["id",[],[]],[/*inlines*/]]}` |
| `Cite` | [citations, inlines] | `{"t": "Cite", "c": [[/*citations*/],[/*inlines*/]]}` |

### Attr (Attributes)

Attributes are a 3-tuple: `[id, classes, key-value-pairs]`

```json
["element-id", ["class1", "class2"], [["key1", "value1"], ["key2", "value2"]]]
```

### Meta Values

Metadata values have their own type system:

| Type | Content |
|------|---------|
| `MetaString` | string |
| `MetaBool` | boolean |
| `MetaList` | [MetaValue] |
| `MetaMap` | {string: MetaValue} |
| `MetaInlines` | [Inline] |
| `MetaBlocks` | [Block] |

## Conversion Functions

### Conversion limits & fidelity

- **Maximum nesting depth:** all converters (parse and emit) enforce a
  nesting-depth cap of **128**. Documents nested deeper (e.g. 129 nested
  `Div`s or `Strong` spans) are rejected with a clean `Invalid Input Error`
  rather than converted. Real documents do not approach this limit; it exists
  so pathological input cannot exhaust the call stack.
- **Best-effort conversions:** `Table` blocks round-trip as raw JSON
  passthrough (no validation of the inner structure). Inline-level attributes
  (on `Span`, `Code`, `Link`, ...) are not fully preserved. Block-level
  attributes (id, classes, key/value pairs on `Header`/`CodeBlock`/`Div`) and
  `BlockQuote` contents are preserved on round-trip.

### pandoc_ast_to_blocks

Convert Pandoc JSON AST to duck_blocks.

**Signature:**
```sql
pandoc_ast_to_blocks(
    ast JSON,
    inline_mode VARCHAR DEFAULT 'text',    -- 'text', 'markdown', 'json'
    flatten_nested BOOLEAN DEFAULT true     -- Flatten nested structures
) → LIST(duck_block)
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `ast` | JSON | Pandoc JSON AST (full document or blocks array) |
| `inline_mode` | VARCHAR | How to serialize inline content |
| `flatten_nested` | BOOLEAN | Flatten nested blocks (quotes, lists) |

**inline_mode options:**
| Mode | Description | Example Output |
|------|-------------|----------------|
| `text` | Plain text, no formatting | `Hello world` |
| `markdown` | Markdown-style formatting | `Hello **world**` |
| `json` | Preserve as JSON array | `[{"t":"Str","c":"Hello"},...]` |

**Example:**
```sql
-- Parse Pandoc JSON and convert to blocks
SELECT unnest(pandoc_ast_to_blocks(
    '{"pandoc-api-version":[1,23],"meta":{},"blocks":[{"t":"Para","c":[{"t":"Str","c":"Hello"}]}]}'::JSON
));
```

### duck_blocks_to_pandoc_blocks

Convert duck_blocks to Pandoc JSON AST.

**Signature:**
```sql
duck_blocks_to_pandoc_blocks(
    blocks LIST(duck_block),
    meta JSON DEFAULT '{}',
    api_version INTEGER[] DEFAULT [1, 23, 1]
) → JSON
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `blocks` | LIST(doc_block) | Input blocks |
| `meta` | JSON | Document metadata |
| `api_version` | INTEGER[] | Pandoc API version |

**Example:**
```sql
-- Convert blocks to Pandoc AST
SELECT duck_blocks_to_pandoc_blocks(
    (SELECT list(b) FROM read_markdown_blocks('doc.md') b)
);
```

### pandoc_inlines_to_text

Convert Pandoc inline array to text.

**Signature:**
```sql
pandoc_inlines_to_text(
    inlines JSON,
    mode VARCHAR DEFAULT 'markdown'
) → VARCHAR
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `inlines` | JSON | Array of Pandoc inline elements |
| `mode` | VARCHAR | Output mode: 'text', 'markdown', 'html' |

**Example:**
```sql
SELECT pandoc_inlines_to_text(
    '[{"t":"Str","c":"Hello"},{"t":"Space"},{"t":"Strong","c":[{"t":"Str","c":"world"}]}]'::JSON,
    'markdown'
);
-- Returns: 'Hello **world**'
```

### pandb_text_to_inlines

Convert text (with optional markup) to Pandoc inlines.

**Signature:**
```sql
pandb_text_to_inlines(
    text VARCHAR,
    parse_markdown BOOLEAN DEFAULT true
) → JSON
```

**Parameters:**
| Name | Type | Description |
|------|------|-------------|
| `text` | VARCHAR | Input text |
| `parse_markdown` | BOOLEAN | Parse markdown formatting |

**Example:**
```sql
SELECT pandb_text_to_inlines('Hello **world**', true);
-- Returns: [{"t":"Str","c":"Hello"},{"t":"Space"},{"t":"Strong","c":[{"t":"Str","c":"world"}]}]
```

### pandoc_meta_to_yaml

Convert Pandoc metadata to YAML string.

**Signature:**
```sql
pandoc_meta_to_yaml(meta JSON) → VARCHAR
```

**Example:**
```sql
SELECT pandoc_meta_to_yaml(
    '{"title":{"t":"MetaString","c":"My Doc"},"author":{"t":"MetaList","c":[...]}}'::JSON
);
-- Returns: 'title: My Doc\nauthor:\n  - ...'
```

### pandoc_yaml_to_meta

Convert YAML string to Pandoc metadata.

**Signature:**
```sql
pandoc_yaml_to_meta(yaml VARCHAR) → JSON
```

**Example:**
```sql
SELECT pandoc_yaml_to_meta('title: My Doc\nauthor: Jane');
```

## Block Type Mapping

### Pandoc → duck_block

| Pandoc Type | element_type | level | encoding | Notes |
|-------------|----------------|-------|----------|-------|
| `Header` | `heading` | 1-6 | text | id from Attr |
| `Para` | `paragraph` | NULL | text/json | Based on inline_mode |
| `Plain` | `paragraph` | NULL | text | `plain: true` in attrs |
| `CodeBlock` | `code` | NULL | text | language from classes |
| `BlockQuote` | `blockquote` | depth | text | Nested blocks flattened |
| `BulletList` | `list` | 1 | json | `ordered: false` |
| `OrderedList` | `list` | 1 | json | `ordered: true`, `start` |
| `Table` | `table` | NULL | json | Full structure preserved |
| `HorizontalRule` | `hr` | NULL | text | |
| `RawBlock` | `raw` | NULL | varies | format in attrs |
| `Div` | `pandoc:div` | NULL | json | Preserves nested structure |
| `LineBlock` | `pandoc:lineblock` | NULL | json | |
| `DefinitionList` | `pandoc:deflist` | NULL | json | |
| `Figure` | `pandoc:figure` | NULL | json | Pandoc 3.0+ |
| `Null` | (skipped) | - | - | Empty block |

### Inline Handling (Header, Para, Plain)

A block's inline run is converted one of two ways, decided by the run's content:

- **Text-only run** (`Str`, `Space`, `SoftBreak`, `LineBreak`): flattened into the
  block's `content`, and no inline children are emitted. This is the spec's
  normalized simple case and keeps one duck_block per Pandoc block.
- **Rich run** (anything containing `Code`, `Math`, `Emph`, `Strong`, `Link`,
  `Strikeout`, `Span`, …): the block is emitted with empty `content`, followed by
  its `kind='inline'` children — the same structured shape
  `parse_markdown_to_duck_blocks` produces, which the ANSI renderer styles and
  `duck_blocks_to_pandoc_blocks` round-trips.

Flattening a rich run into `content` is lossy: `Code` and `Math` have no text
representation there and used to be dropped outright, turning
``Run `make install` first.`` into `Run  first.`. Structured children are the
only representation that can carry them.

### duck_block → Pandoc

| element_type | Pandoc Type | Notes |
|----------------|-------------|-------|
| `heading` | `Header` | level → int, id from attrs |
| `paragraph` | `Para` | Content parsed for inlines |
| `code` | `CodeBlock` | language → first class |
| `blockquote` | `BlockQuote` | Wrap content in Para |
| `list` | `BulletList`/`OrderedList` | Based on `ordered` attr |
| `table` | `Table` | JSON content → full table |
| `hr` | `HorizontalRule` | |
| `metadata` | (document meta) | YAML → Meta values |
| `image` | `Para[Image]` | Wrapped in paragraph |
| `raw` | `RawBlock` | format from attrs |
| `pandoc:*` | (native type) | JSON content restored |

## Handling Nested Structures

### Flattening (flatten_nested = true)

Nested structures become multiple blocks with depth tracking:

**Input (BlockQuote containing Para):**
```json
{"t": "BlockQuote", "c": [
  {"t": "Para", "c": [{"t": "Str", "c": "Quoted text"}]},
  {"t": "Para", "c": [{"t": "Str", "c": "More quoted"}]}
]}
```

**Output (flattened):**
```sql
-- Single blockquote element with combined content
kind: 'block'
element_type: 'blockquote'
content: 'Quoted text\n\nMore quoted'
level: 1
encoding: 'text'
```

### Preserving (flatten_nested = false)

Nested structures preserved as JSON:

```sql
kind: 'block'
element_type: 'blockquote'
content: '[{"t":"Para","c":[...]},{"t":"Para","c":[...]}]'
level: 1
encoding: 'json'
attributes: {'nested': 'true'}
```

### List Flattening

**Input (nested list):**
```json
{"t": "BulletList", "c": [
  [{"t": "Para", "c": [{"t": "Str", "c": "Item 1"}]}],
  [{"t": "Para", "c": [{"t": "Str", "c": "Item 2"}]},
   {"t": "BulletList", "c": [
     [{"t": "Para", "c": [{"t": "Str", "c": "Sub-item"}]}]
   ]}]
]}
```

**Output (flatten_nested = true):**
```sql
kind: 'block'
element_type: 'list'
content: '[{"text": "Item 1"}, {"text": "Item 2", "children": [{"text": "Sub-item"}]}]'
level: 1
encoding: 'json'
attributes: {'ordered': 'false'}
```

## Round-Trip Fidelity

### Preserved
- Block structure and order
- Heading levels and IDs
- Code block languages
- List ordering and nesting
- Table structure
- Raw block format
- Document metadata

### Potentially Lost (when inline_mode = 'text')
- Inline formatting (bold, italic, etc.)
- Links and images within paragraphs
- Footnote references
- Math expressions
- Spans with attributes

### Preserved with inline_mode = 'json'
- All inline content
- Full round-trip fidelity
- More complex to process

## Implementation Notes

### JSON Parsing

Uses DuckDB's native JSON functions where possible:

```sql
-- Extract block type
json_extract_string(block, '$.t') as block_type

-- Extract header level
json_extract(block, '$.c[0]')::INTEGER as level

-- Extract code content
json_extract_string(block, '$.c[1]') as code
```

### Inline Serialization (markdown mode)

```cpp
std::string InlinesToMarkdown(const json& inlines) {
    std::string result;
    for (const auto& el : inlines) {
        std::string t = el["t"];
        if (t == "Str") {
            result += el["c"].get<std::string>();
        } else if (t == "Space") {
            result += " ";
        } else if (t == "SoftBreak") {
            result += " ";
        } else if (t == "LineBreak") {
            result += "\n";
        } else if (t == "Emph") {
            result += "*" + InlinesToMarkdown(el["c"]) + "*";
        } else if (t == "Strong") {
            result += "**" + InlinesToMarkdown(el["c"]) + "**";
        } else if (t == "Code") {
            result += "`" + el["c"][1].get<std::string>() + "`";
        } else if (t == "Link") {
            std::string text = InlinesToMarkdown(el["c"][1]);
            std::string url = el["c"][2][0].get<std::string>();
            result += "[" + text + "](" + url + ")";
        }
        // ... etc
    }
    return result;
}
```

## Examples

### Reading Pandoc JSON

```sql
-- Load JSON exported from pandoc
CREATE TABLE pandoc_docs AS
SELECT * FROM read_json('exported.json');

-- Convert to blocks
SELECT unnest(pandoc_ast_to_blocks(ast))
FROM pandoc_docs;
```

### Writing Pandoc JSON

```sql
-- Convert blocks to Pandoc AST and export
COPY (
    SELECT duck_blocks_to_pandoc_blocks(
        (SELECT list(b) FROM read_markdown_blocks('doc.md') b),
        '{"title": {"t": "MetaString", "c": "My Document"}}'::JSON
    ) as ast
) TO 'output.json';
```

### Cross-Tool Pipeline

```sql
-- Read from any tool that exports Pandoc JSON
-- Transform with SQL
-- Export for any tool that reads Pandoc JSON

WITH parsed AS (
    SELECT unnest(pandoc_ast_to_blocks(
        read_json_auto('sphinx_output.json')
    )) as block
),
transformed AS (
    SELECT * FROM parsed
    WHERE block.block_type != 'pandoc:div'  -- Remove divs
)
SELECT duck_blocks_to_pandoc_blocks(list(block))
FROM transformed;
```

## Version Compatibility

| Pandoc Version | API Version | Notes |
|----------------|-------------|-------|
| 2.0 - 2.19 | 1.17 - 1.22 | Table format differs |
| 3.0+ | 1.23+ | Figure type added |

The conversion functions handle both table formats automatically based on structure detection.
