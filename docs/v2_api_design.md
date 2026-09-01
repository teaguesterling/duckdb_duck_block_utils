# Duck Block Utils v2 API Design

This document describes the v2 API redesign for `duck_block_utils`, introducing a unified content type and consistent return types across all builders.

## Design Goals

1. **Uniform return type**: ALL `db_*` builders return `LIST(duck_block)`
2. **Flexible content input**: Accept text, single element, or list of elements uniformly
3. **Config-first parameters**: Configuration params before content params
4. **Composable by default**: Natural composition with `duck_blocks_assemble([...])`

## Core Types

### duck_block (unchanged)

```sql
STRUCT(
    kind VARCHAR,                       -- 'block', 'inline' or 'value'
    element_type VARCHAR,               -- 'heading', 'paragraph', 'bold', etc.
    content VARCHAR,                    -- Text content (NULL if has children)
    level INTEGER,                      -- Hierarchy depth (1 = top level)
    encoding VARCHAR,                   -- 'text', 'json', 'yaml', 'html', 'xml'
    attributes MAP(VARCHAR, VARCHAR),   -- Type-specific metadata
    element_order INTEGER               -- Position in sequence (0-indexed)
)
```

### duck_block_content (NEW)

Union type for builder content parameters:

```sql
UNION(
    text VARCHAR,                       -- Simple text content
    block duck_block,                   -- Single child element
    blocks LIST(duck_block)             -- Multiple child elements
)
```

This allows all three forms to work with a single function signature:

```sql
duck_block_paragraph('Simple text')                              -- VARCHAR
duck_block_paragraph(duck_block_bold('Important'))                       -- single duck_block
duck_block_paragraph([duck_block_text('Click '), duck_block_link(url, 'here')]) -- LIST(duck_block)
```

## Universal Builder Pattern

All builders follow the same internal pattern:

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Input: parent_block + duck_block_content                                │
│                                                                          │
│  ┌──────────────────┐     ┌─────────────────────────────────────────┐   │
│  │ VARCHAR content  │ →   │ [parent.content = text]                 │   │
│  └──────────────────┘     └─────────────────────────────────────────┘   │
│                                                                          │
│  ┌──────────────────┐     ┌─────────────────────────────────────────┐   │
│  │ STRUCT (1 child) │ →   │ [parent.content = NULL], [child@lvl+1]  │   │
│  └──────────────────┘     └─────────────────────────────────────────┘   │
│                                                                          │
│  ┌──────────────────┐     ┌─────────────────────────────────────────┐   │
│  │ LIST (N children)│ →   │ [parent.content = NULL], [c1,c2...@lvl+1]│  │
│  └──────────────────┘     └─────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

**Rules:**
1. `content` field is set IFF there are no children (VARCHAR case)
2. `content` field is NULL when children exist (STRUCT/LIST case)
3. Children are placed at `parent.level + 1`
4. Every builder returns `LIST(duck_block)`

## Function Signatures

### Block Builders

| Function | Signature | Notes |
|----------|-----------|-------|
| `duck_block_heading` | `(level INT, content duck_block_content) → LIST(duck_block)` | Level stored in `attributes['heading_level']` |
| `duck_block_paragraph` | `(content duck_block_content) → LIST(duck_block)` | |
| `duck_block_code` | `(content duck_block_content) → LIST(duck_block)` | No language |
| `duck_block_code` | `(language VARCHAR, content duck_block_content) → LIST(duck_block)` | With language |
| `duck_block_blockquote` | `(content duck_block_content) → LIST(duck_block)` | Level defaults to 1 |
| `duck_block_blockquote` | `(level INT, content duck_block_content) → LIST(duck_block)` | Nested blockquote |
| `duck_block_list_block` | `(items duck_block_content[]) → LIST(duck_block)` | Unordered list |
| `duck_block_list_block` | `(ordered BOOL, items duck_block_content[]) → LIST(duck_block)` | Ordered/unordered |
| `duck_block_list_item` | `(content duck_block_content) → LIST(duck_block)` | **NEW** - single list item |
| `duck_block_list_item` | `(ordered BOOL, content duck_block_content) → LIST(duck_block)` | With bullet type |
| `duck_block_hr` | `() → LIST(duck_block)` | |
| `duck_block_metadata` | `(yaml VARCHAR) → LIST(duck_block)` | |
| `duck_block_image` | `(src VARCHAR) → LIST(duck_block)` | |
| `duck_block_image` | `(src VARCHAR, alt VARCHAR) → LIST(duck_block)` | |
| `duck_block_image` | `(src VARCHAR, alt VARCHAR, title VARCHAR) → LIST(duck_block)` | |
| `duck_block_raw` | `(content VARCHAR) → LIST(duck_block)` | HTML format |
| `duck_block_raw` | `(format VARCHAR, content VARCHAR) → LIST(duck_block)` | Custom format |

### Inline Builders

| Function | Signature | Notes |
|----------|-----------|-------|
| `duck_block_text` | `(content VARCHAR) → LIST(duck_block)` | Plain text |
| `duck_block_space` | `() → LIST(duck_block)` | Word separator |
| `duck_block_softbreak` | `() → LIST(duck_block)` | Soft line break |
| `duck_block_linebreak` | `() → LIST(duck_block)` | Hard line break |
| `duck_block_bold` | `(content duck_block_content) → LIST(duck_block)` | |
| `duck_block_italic` | `(content duck_block_content) → LIST(duck_block)` | |
| `duck_block_strikethrough` | `(content duck_block_content) → LIST(duck_block)` | |
| `duck_block_superscript` | `(content duck_block_content) → LIST(duck_block)` | |
| `duck_block_subscript` | `(content duck_block_content) → LIST(duck_block)` | |
| `duck_block_smallcaps` | `(content duck_block_content) → LIST(duck_block)` | |
| `duck_block_underline` | `(content duck_block_content) → LIST(duck_block)` | |
| `duck_block_inline_code` | `(content VARCHAR) → LIST(duck_block)` | Code doesn't nest |
| `duck_block_math` | `(content VARCHAR) → LIST(duck_block)` | Inline math |
| `duck_block_math` | `(display BOOL, content VARCHAR) → LIST(duck_block)` | Block math if true |
| `duck_block_link` | `(href VARCHAR, content duck_block_content) → LIST(duck_block)` | |
| `duck_block_link` | `(href VARCHAR, title VARCHAR, content duck_block_content) → LIST(duck_block)` | |
| `duck_block_inline_image` | `(src VARCHAR) → LIST(duck_block)` | |
| `duck_block_inline_image` | `(src VARCHAR, alt VARCHAR) → LIST(duck_block)` | |
| `duck_block_inline_image` | `(src VARCHAR, alt VARCHAR, title VARCHAR) → LIST(duck_block)` | |
| `duck_block_quoted` | `(content duck_block_content) → LIST(duck_block)` | Double quotes |
| `duck_block_quoted` | `(quote_type VARCHAR, content duck_block_content) → LIST(duck_block)` | |
| `duck_block_cite` | `(key VARCHAR) → LIST(duck_block)` | |
| `duck_block_note` | `(content duck_block_content) → LIST(duck_block)` | Footnote |
| `duck_block_span` | `(content duck_block_content) → LIST(duck_block)` | |
| `duck_block_span` | `(id VARCHAR, content duck_block_content) → LIST(duck_block)` | |
| `duck_block_span` | `(id VARCHAR, class VARCHAR, content duck_block_content) → LIST(duck_block)` | |
| `duck_block_raw_inline` | `(content VARCHAR) → LIST(duck_block)` | HTML |
| `duck_block_raw_inline` | `(format VARCHAR, content VARCHAR) → LIST(duck_block)` | |

### Assembly Functions

| Function | Signature | Notes |
|----------|-----------|-------|
| `duck_blocks_assemble` | `(blocks LIST(LIST(duck_block))) → LIST(duck_block)` | Flatten + renumber |
| `duck_blocks_document` | `(blocks LIST(LIST(duck_block))) → LIST(duck_block)` | Alias for assemble |
| `duck_block_section` | `(level INT, title duck_block_content) → LIST(duck_block)` | Heading only |
| `duck_block_section` | `(level INT, title duck_block_content, children LIST(LIST(duck_block))) → LIST(duck_block)` | Heading + content |
| `duck_blocks_concat` | `(a LIST(duck_block), b LIST(duck_block)) → LIST(duck_block)` | No renumbering |
| `duck_blocks_rebase_levels` | `(blocks LIST(duck_block), offset INT) → LIST(duck_block)` | Adjust heading levels |

## Usage Examples

### Simple Document

```sql
SELECT duck_blocks_assemble([
    duck_block_heading(1, 'My Document'),
    duck_block_paragraph('This is the introduction.'),
    duck_block_heading(2, 'Section One'),
    duck_block_paragraph([
        duck_block_text('Click '),
        duck_block_link('https://example.com', 'here'),
        duck_block_text(' to learn more.')
    ]),
    duck_block_hr()
]);
```

### Rich Inline Content

```sql
-- Heading with formatted content
SELECT duck_block_heading(1, [
    duck_block_text('Hello '),
    duck_block_bold('World'),
    duck_block_text('!')
]);
-- Returns: [{heading, content=NULL, level=1}, {text, 'Hello ', level=2}, {bold, 'World', level=2}, {text, '!', level=2}]

-- Paragraph with link containing formatted text
SELECT duck_block_paragraph([
    duck_block_text('See the '),
    duck_block_link('https://docs.example.com', [
        duck_block_bold('official'),
        duck_block_text(' documentation')
    ]),
    duck_block_text('.')
]);
```

### List with Rich Items

```sql
-- List items with inline content
SELECT duck_blocks_assemble([
    duck_block_heading(2, 'Resources'),
    duck_block_list_block(false, [
        duck_block_list_item([
            duck_block_link('https://github.com/org/repo', 'GitHub'),
            duck_block_text(' '),
            duck_block_inline_image('https://github.com/org/repo/badge.svg', 'CI')
        ]),
        duck_block_list_item([
            duck_block_link('https://docs.example.com', 'Documentation'),
            duck_block_text(' '),
            duck_block_inline_image('https://readthedocs.org/badge/', 'Docs')
        ])
    ])
]);
```

### Dashboard Pattern (v5)

```sql
-- Single list production, single render
CREATE TABLE page_blocks AS
SELECT duck_blocks_assemble([
    duck_block_heading(1, 'DuckDB Extensions Dashboard'),
    duck_block_paragraph('A collection of extensions.'),

    -- Dynamic per-project
    duck_block_heading(2, project.category),
    duck_block_heading(3, project.name),
    duck_block_paragraph(COALESCE(project.description, duck_block_italic('No description'))),
    duck_block_list_block(false, [
        duck_block_list_item([
            duck_block_link(project.github_url, 'GitHub'),
            duck_block_text(' '),
            duck_block_inline_image(project.ci_badge, 'CI')
        ])
    ])
]) AS blocks
FROM projects;
```

## Migration from v1

### Return Type Changes

```sql
-- v1: Single block
SELECT duck_block_heading('Title', 1).content;

-- v2: List of blocks
SELECT duck_block_heading(1, 'Title')[1].content;
```

### Parameter Order Changes

```sql
-- v1: content first
duck_block_heading('Title', 1)
duck_block_code('print(1)', 'python')
duck_block_link('Click', 'https://example.com')

-- v2: config first, content last
duck_block_heading(1, 'Title')
duck_block_code('python', 'print(1)')
duck_block_link('https://example.com', 'Click')
```

### Assembly Changes

```sql
-- v1: List of blocks (mixed single/list didn't work well)
duck_blocks_assemble([duck_block_heading('A', 1), duck_block_paragraph('B')])

-- v2: List of lists (uniform, each builder returns list)
duck_blocks_assemble([duck_block_heading(1, 'A'), duck_block_paragraph('B')])
-- Works because each builder returns LIST, duck_blocks_assemble flattens LIST(LIST)
```

## Implementation Notes

### Core Utility Function

A single `BuildWithContent` helper handles all builders:

```cpp
vector<Value> BuildWithContent(
    const string& kind,
    const string& element_type,
    const string& encoding,
    const map<string,string>& attrs,
    int level,
    const Value& content  // duck_block_content UNION
) {
    vector<Value> result;

    string content_str;
    vector<Value> children;

    // Determine content vs children based on union variant
    auto union_tag = UnionValue::GetTag(content);
    if (union_tag == 0) {  // VARCHAR
        content_str = UnionValue::GetValue(content).GetValue<string>();
    } else if (union_tag == 1) {  // STRUCT (single duck_block)
        children.push_back(UnionValue::GetValue(content));
    } else {  // LIST (multiple duck_blocks)
        children = ListValue::GetChildren(UnionValue::GetValue(content));
    }

    // Create parent block
    Value content_val = children.empty() ? Value(content_str) : Value();
    result.push_back(CreateBlock(kind, element_type, content_val, level, encoding, attrs));

    // Append children at level+1
    for (auto& child : children) {
        result.push_back(SetLevel(child, level + 1));
    }

    return result;
}
```

### Rendering Logic

Renderers reconstruct parent-child relationships from levels:

```
Parent at level N
├── Child at level N+1
├── Child at level N+1
│   └── Grandchild at level N+2
└── Child at level N+1
Next sibling at level N
```

No `num_children` field needed - level transitions define boundaries.
