-- Test fixtures for complex multi-block documents
-- Each test case includes: name, duck_blocks, expected_markdown, expected_html

CREATE OR REPLACE TABLE document_fixtures AS
SELECT * FROM (VALUES
    -- Simple document with heading and paragraph
    ('doc_heading_paragraph',
     db_assemble([
         db_heading(1, 'Welcome'),
         db_paragraph('This is the introduction.')
     ]),
     '# Welcome

This is the introduction.',
     '<h1>Welcome</h1>
<p>This is the introduction.</p>'),

    -- Document with heading, formatted paragraph, and code
    ('doc_heading_para_code',
     db_assemble([
         db_heading(2, 'Getting Started'),
         db_paragraph([db_text('First, install the '), db_bold('extension'), db_text('.')]),
         db_code('sql', 'INSTALL duck_block_utils;')
     ]),
     '## Getting Started

First, install the **extension**.

```sql
INSTALL duck_block_utils;
```',
     '<h2>Getting Started</h2>
<p>First, install the <strong>extension</strong>.</p>
<pre><code class="language-sql">INSTALL duck_block_utils;</code></pre>'),

    -- Document with blockquote and attribution
    ('doc_quote_attribution',
     db_assemble([
         db_blockquote('The only way to do great work is to love what you do.'),
         db_paragraph([db_text('— '), db_italic('Steve Jobs')])
     ]),
     '> The only way to do great work is to love what you do.

— *Steve Jobs*',
     '<blockquote><p>The only way to do great work is to love what you do.</p></blockquote>
<p>— <em>Steve Jobs</em></p>'),

    -- Document with multiple headings (hierarchy)
    ('doc_heading_hierarchy',
     db_assemble([
         db_heading(1, 'Main Title'),
         db_paragraph('Introduction paragraph.'),
         db_heading(2, 'First Section'),
         db_paragraph('Section content.'),
         db_heading(3, 'Subsection'),
         db_paragraph('Subsection content.'),
         db_heading(2, 'Second Section'),
         db_paragraph('More content.')
     ]),
     '# Main Title

Introduction paragraph.

## First Section

Section content.

### Subsection

Subsection content.

## Second Section

More content.',
     '<h1>Main Title</h1>
<p>Introduction paragraph.</p>
<h2>First Section</h2>
<p>Section content.</p>
<h3>Subsection</h3>
<p>Subsection content.</p>
<h2>Second Section</h2>
<p>More content.</p>'),

    -- Document with list after heading
    ('doc_heading_list',
     db_assemble([
         db_heading(2, 'Features'),
         db_list([
             db_list_item('Easy to use'),
             db_list_item('Powerful'),
             db_list_item('Extensible')
         ])
     ]),
     '## Features

- Easy to use
- Powerful
- Extensible',
     '<h2>Features</h2>
<ul>
<li>Easy to use</li>
<li>Powerful</li>
<li>Extensible</li>
</ul>'),

    -- Document with paragraph, hr, paragraph
    ('doc_with_hr_separator',
     db_assemble([
         db_paragraph('Above the line.'),
         db_hr(),
         db_paragraph('Below the line.')
     ]),
     'Above the line.

---

Below the line.',
     '<p>Above the line.</p>
<hr>
<p>Below the line.</p>'),

    -- Document with image in context
    ('doc_with_image',
     db_assemble([
         db_heading(2, 'Screenshot'),
         db_paragraph('Here is what it looks like:'),
         db_image('https://example.com/screenshot.png', 'Application screenshot', 'Main interface')
     ]),
     '## Screenshot

Here is what it looks like:

![Application screenshot](https://example.com/screenshot.png "Main interface")',
     '<h2>Screenshot</h2>
<p>Here is what it looks like:</p>
<img src="https://example.com/screenshot.png" alt="Application screenshot" title="Main interface">'),

    -- Full README-style document
    ('doc_readme_style',
     db_assemble([
         db_heading(1, 'Project Name'),
         db_paragraph([db_text('A '), db_bold('powerful'), db_text(' tool for data processing.')]),
         db_heading(2, 'Installation'),
         db_code('bash', 'pip install project-name'),
         db_heading(2, 'Usage'),
         db_paragraph([db_text('Import and use the '), db_inline_code('process'), db_text(' function:')]),
         db_code('python', 'from project import process
result = process(data)'),
         db_heading(2, 'License'),
         db_paragraph([db_text('MIT License. See '), db_link('LICENSE', 'LICENSE file'), db_text(' for details.')])
     ]),
     '# Project Name

A **powerful** tool for data processing.

## Installation

```bash
pip install project-name
```

## Usage

Import and use the `process` function:

```python
from project import process
result = process(data)
```

## License

MIT License. See [LICENSE file](LICENSE) for details.',
     '<h1>Project Name</h1>
<p>A <strong>powerful</strong> tool for data processing.</p>
<h2>Installation</h2>
<pre><code class="language-bash">pip install project-name</code></pre>
<h2>Usage</h2>
<p>Import and use the <code>process</code> function:</p>
<pre><code class="language-python">from project import process
result = process(data)</code></pre>
<h2>License</h2>
<p>MIT License. See <a href="LICENSE">LICENSE file</a> for details.</p>'),

    -- Document with nested list in context
    ('doc_with_nested_list',
     db_assemble([
         db_heading(2, 'API Reference'),
         db_paragraph('The following functions are available:'),
         db_list([
             db_list_item([db_inline_code('db_heading'), db_text(' - Create headings')]),
             db_list([
                 db_list_item('Takes level and content'),
                 db_list_item('Returns LIST(duck_block)')
             ]),
             db_list_item([db_inline_code('db_paragraph'), db_text(' - Create paragraphs')])
         ])
     ]),
     '## API Reference

The following functions are available:

- `db_heading` - Create headings
  - Takes level and content
  - Returns LIST(duck_block)
- `db_paragraph` - Create paragraphs',
     '<h2>API Reference</h2>
<p>The following functions are available:</p>
<ul>
<li><code>db_heading</code> - Create headings
<ul>
<li>Takes level and content</li>
<li>Returns LIST(duck_block)</li>
</ul>
</li>
<li><code>db_paragraph</code> - Create paragraphs</li>
</ul>'),

    -- Document mixing many element types
    ('doc_mixed_elements',
     db_assemble([
         db_heading(1, 'Complete Example'),
         db_blockquote([db_bold('Note:'), db_text(' This demonstrates all features.')]),
         db_paragraph([
             db_text('Visit '),
             db_link('https://docs.example.com', 'our documentation'),
             db_text(' for more.')
         ]),
         db_list(true, [
             db_list_item('Feature one'),
             db_list_item([db_bold('Feature two'), db_text(' with emphasis')]),
             db_list_item([db_text('Feature with '), db_inline_code('code')])
         ]),
         db_hr(),
         db_code('sql', 'SELECT 42;'),
         db_paragraph([db_italic('The end.')])
     ]),
     '# Complete Example

> **Note:** This demonstrates all features.

Visit [our documentation](https://docs.example.com) for more.

1. Feature one
2. **Feature two** with emphasis
3. Feature with `code`

---

```sql
SELECT 42;
```

*The end.*',
     '<h1>Complete Example</h1>
<blockquote><p><strong>Note:</strong> This demonstrates all features.</p></blockquote>
<p>Visit <a href="https://docs.example.com">our documentation</a> for more.</p>
<ol>
<li>Feature one</li>
<li><strong>Feature two</strong> with emphasis</li>
<li>Feature with <code>code</code></li>
</ol>
<hr>
<pre><code class="language-sql">SELECT 42;</code></pre>
<p><em>The end.</em></p>'),

    -- Comprehensive technical README with all features
    ('doc_comprehensive_readme',
     db_assemble([
         -- Title and badges area
         db_heading(1, 'DuckDB Extension Template'),
         db_paragraph([
             db_bold('Status:'), db_text(' Production Ready | '),
             db_bold('Version:'), db_text(' 2.0.0 | '),
             db_bold('License:'), db_text(' MIT')
         ]),

         -- Introduction with mixed formatting
         db_paragraph([
             db_text('A '), db_bold('comprehensive'), db_text(' template for building '),
             db_link('https://duckdb.org', 'DuckDB'), db_text(' extensions with '),
             db_italic('modern C++ practices'), db_text(' and full CI/CD support.')
         ]),

         -- Blockquote callout
         db_blockquote([
             db_bold('Important:'), db_text(' This template requires DuckDB v1.0+ and a C++17 compatible compiler. See '),
             db_link('#requirements', 'Requirements'), db_text(' for details.')
         ]),

         -- Features section with nested lists
         db_heading(2, 'Features'),
         db_list([
             db_list_item([db_bold('Modern Build System')]),
             db_list([
                 db_list_item('CMake-based configuration'),
                 db_list_item([db_text('Supports '), db_inline_code('vcpkg'), db_text(' and '), db_inline_code('conan')]),
                 db_list_item('Cross-platform compatibility')
             ]),
             db_list_item([db_bold('Testing Infrastructure')]),
             db_list([
                 db_list_item('Unit tests with GoogleTest'),
                 db_list_item('Integration tests with sqllogictest'),
                 db_list_item([db_text('Coverage reports via '), db_inline_code('lcov')])
             ]),
             db_list_item([db_bold('CI/CD Pipeline')]),
             db_list([
                 db_list_item('GitHub Actions workflows'),
                 db_list_item('Automatic releases'),
                 db_list_item('Multi-platform builds (Linux, macOS, Windows)')
             ])
         ]),

         -- Installation section
         db_heading(2, 'Installation'),
         db_paragraph([db_text('Install directly from the DuckDB CLI:')]),
         db_code('sql', 'INSTALL extension_name FROM community;
LOAD extension_name;'),

         db_paragraph([db_text('Or build from source:')]),
         db_code('bash', 'git clone https://github.com/user/extension.git
cd extension
make release
make test'),

         -- Quick start with numbered steps
         db_heading(2, 'Quick Start'),
         db_list(true, [
             db_list_item([db_bold('Clone'), db_text(' the repository')]),
             db_list_item([db_bold('Configure'), db_text(' your extension name in '), db_inline_code('CMakeLists.txt')]),
             db_list_item([db_bold('Implement'), db_text(' your functions in '), db_inline_code('src/')]),
             db_list_item([db_bold('Test'), db_text(' with '), db_inline_code('make test')]),
             db_list_item([db_bold('Release'), db_text(' via GitHub Actions')])
         ]),

         -- API section with code examples
         db_heading(2, 'API Reference'),
         db_heading(3, 'Scalar Functions'),
         db_paragraph([
             db_text('The '), db_inline_code('my_function'), db_text(' scalar function accepts a '),
             db_inline_code('VARCHAR'), db_text(' and returns a '), db_inline_code('BIGINT'), db_text(':')
         ]),
         db_code('sql', '-- Basic usage
SELECT my_function(''input'') AS result;

-- With table data
SELECT id, my_function(name) AS processed
FROM my_table
WHERE active = true;'),

         db_heading(3, 'Table Functions'),
         db_paragraph([db_text('Use '), db_inline_code('my_table_function'), db_text(' to generate rows:')]),
         db_code('sql', 'SELECT * FROM my_table_function(10, ''config.json'');'),

         -- Configuration section
         db_heading(2, 'Configuration'),
         db_paragraph([db_text('Configure via DuckDB settings:')]),
         db_code('sql', 'SET extension_option = ''value'';
SET extension_debug = true;'),

         -- Contributing section with nested blockquote
         db_heading(2, 'Contributing'),
         db_paragraph([
             db_text('We welcome contributions! Please read our '),
             db_link('CONTRIBUTING.md', 'Contributing Guide'),
             db_text(' before submitting PRs.')
         ]),
         db_blockquote([
             db_bold('Note for contributors:'), db_text(' All PRs must include tests and pass CI checks. Run '),
             db_inline_code('make format'), db_text(' before committing.')
         ]),

         -- Separator
         db_hr(),

         -- Footer with multiple links
         db_paragraph([
             db_link('https://github.com/user/extension/issues', 'Report Issues'),
             db_text(' | '),
             db_link('https://discord.gg/duckdb', 'Discord'),
             db_text(' | '),
             db_link('https://duckdb.org/docs', 'DuckDB Docs')
         ]),
         db_paragraph([
             db_text('Made with '), db_inline_code('<3'), db_text(' by the DuckDB Community')
         ])
     ]),
     NULL,  -- Skip manual markdown expectation - generated by Pandoc
     NULL), -- Skip manual HTML expectation - generated by Pandoc

    -- Edge cases: special characters and escaping
    ('doc_edge_cases',
     db_assemble([
         db_heading(1, 'Edge Cases & Special Characters'),

         -- Quotes and apostrophes
         db_paragraph([
             db_text('Testing quotes: "double" and '), db_inline_code('''single'''),
             db_text(' and apostrophes: it''s working!')
         ]),

         -- Code with special chars
         db_heading(2, 'Code Escaping'),
         db_code('python', 'def example():
    """Docstring with "quotes" and ''apostrophes''"""
    return f"Value: {x} | Special: <>&"'),

         db_code('sql', '-- SQL with operators
SELECT * FROM t WHERE x > 10 AND y < 20;
SELECT ''string with ''''quotes'''''';'),

         -- Inline code with special content
         db_paragraph([
             db_text('Inline special chars: '),
             db_inline_code('<html>'), db_text(', '),
             db_inline_code('a && b'), db_text(', '),
             db_inline_code('x | y'), db_text(', '),
             db_inline_code('$PATH')
         ]),

         -- URLs with query params
         db_heading(2, 'URLs with Parameters'),
         db_paragraph([
             db_text('Link with params: '),
             db_link('https://example.com/search?q=test&page=1', 'Search Results')
         ]),

         -- Math and symbols
         db_heading(2, 'Mathematical Expressions'),
         db_paragraph([
             db_text('Comparisons: x < y, a > b, n <= m, p >= q')
         ]),
         db_paragraph([
             db_text('Operators: a + b - c * d / e % f')
         ]),

         -- Unicode
         db_heading(2, 'Unicode Support'),
         db_paragraph('International: cafe, naive, resume'),
         db_paragraph('Symbols: arrows, checks, stars'),
         db_paragraph('Emoji-adjacent: (tm), (c), (r)')
     ]),
     NULL,
     NULL),

    -- Deeply nested structure test
    ('doc_deeply_nested',
     db_assemble([
         db_heading(1, 'Nested Structure Test'),

         -- Multiple levels of nesting
         db_list([
             db_list_item([db_bold('Level 1 Item A')]),
             db_list([
                 db_list_item([db_text('Level 2 with '), db_italic('formatting')]),
                 db_list([
                     db_list_item([db_text('Level 3 with '), db_inline_code('code')]),
                     db_list([
                         db_list_item('Level 4 plain'),
                         db_list_item([db_link('https://example.com', 'Level 4 link')])
                     ])
                 ]),
                 db_list_item('Back to Level 2')
             ]),
             db_list_item([db_bold('Level 1 Item B')]),
             db_list([
                 db_list_item([db_text('Another L2 with '), db_bold('bold'), db_text(' and '), db_italic('italic')])
             ])
         ]),

         -- Blockquote with complex content
         db_heading(2, 'Complex Blockquote'),
         db_blockquote([
             db_bold('Quote Title'), db_text(': This is a quote with '),
             db_inline_code('inline code'), db_text(' and a '),
             db_link('https://example.com', 'link'), db_text(' inside.')
         ]),

         -- Multiple consecutive elements
         db_heading(2, 'Consecutive Elements'),
         db_code('python', 'print("first")'),
         db_code('python', 'print("second")'),
         db_code('python', 'print("third")'),

         db_blockquote('First quote.'),
         db_blockquote('Second quote.'),

         db_paragraph([db_bold('Bold paragraph.')]),
         db_paragraph([db_italic('Italic paragraph.')]),
         db_paragraph([db_inline_code('Code paragraph.')])
     ]),
     NULL,
     NULL)

) AS t(name, blocks, expected_markdown, expected_html);
