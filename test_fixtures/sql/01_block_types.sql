-- Test fixtures for individual block types
-- Each test case includes: name, duck_blocks, expected_markdown, expected_html

CREATE OR REPLACE TABLE block_type_fixtures AS
SELECT * FROM (VALUES
    -- Headings
    ('heading_h1',
     db_heading(1, 'Main Title'),
     '# Main Title',
     '<h1>Main Title</h1>'),

    ('heading_h2',
     db_heading(2, 'Section'),
     '## Section',
     '<h2>Section</h2>'),

    ('heading_h3',
     db_heading(3, 'Subsection'),
     '### Subsection',
     '<h3>Subsection</h3>'),

    -- Paragraphs
    ('paragraph_simple',
     db_paragraph('This is a simple paragraph.'),
     'This is a simple paragraph.',
     '<p>This is a simple paragraph.</p>'),

    ('paragraph_with_bold',
     db_paragraph([db_text('This has '), db_bold('bold'), db_text(' text.')]),
     'This has **bold** text.',
     '<p>This has <strong>bold</strong> text.</p>'),

    ('paragraph_with_italic',
     db_paragraph([db_text('This has '), db_italic('italic'), db_text(' text.')]),
     'This has *italic* text.',
     '<p>This has <em>italic</em> text.</p>'),

    ('paragraph_with_link',
     db_paragraph([db_text('Click '), db_link('https://example.com', 'here'), db_text(' for more.')]),
     'Click [here](https://example.com) for more.',
     '<p>Click <a href="https://example.com">here</a> for more.</p>'),

    ('paragraph_with_code',
     db_paragraph([db_text('Use '), db_inline_code('SELECT *'), db_text(' to query.')]),
     'Use `SELECT *` to query.',
     '<p>Use <code>SELECT *</code> to query.</p>'),

    -- Code blocks
    ('code_python',
     db_code('python', 'def hello():
    print("Hello, World!")'),
     '```python
def hello():
    print("Hello, World!")
```',
     '<pre><code class="language-python">def hello():
    print("Hello, World!")</code></pre>'),

    ('code_sql',
     db_code('sql', 'SELECT * FROM users WHERE active = true;'),
     '```sql
SELECT * FROM users WHERE active = true;
```',
     '<pre><code class="language-sql">SELECT * FROM users WHERE active = true;</code></pre>'),

    ('code_no_language',
     db_code('', 'plain text code'),
     '```
plain text code
```',
     '<pre><code>plain text code</code></pre>'),

    -- Blockquotes
    ('blockquote_simple',
     db_blockquote('This is a quote.'),
     '> This is a quote.',
     '<blockquote><p>This is a quote.</p></blockquote>'),

    ('blockquote_with_formatting',
     db_blockquote([db_text('A quote with '), db_bold('emphasis'), db_text('.')]),
     '> A quote with **emphasis**.',
     '<blockquote><p>A quote with <strong>emphasis</strong>.</p></blockquote>'),

    -- Horizontal rules
    ('hr',
     db_hr(),
     '---',
     '<hr>'),

    -- Images
    ('image_simple',
     db_image('https://example.com/img.png', 'Alt text', ''),
     '![Alt text](https://example.com/img.png)',
     '<img src="https://example.com/img.png" alt="Alt text">'),

    ('image_with_title',
     db_image('https://example.com/img.png', 'Alt text', 'Image title'),
     '![Alt text](https://example.com/img.png "Image title")',
     '<img src="https://example.com/img.png" alt="Alt text" title="Image title">'),

    -- Raw blocks
    ('raw_html',
     db_raw('html', '<div class="custom">Custom HTML</div>'),
     '<div class="custom">Custom HTML</div>',
     '<div class="custom">Custom HTML</div>'),

    ('raw_latex',
     db_raw('latex', '\textbf{bold}'),
     NULL,  -- No markdown equivalent
     NULL)  -- No HTML equivalent

) AS t(name, blocks, expected_markdown, expected_html);
