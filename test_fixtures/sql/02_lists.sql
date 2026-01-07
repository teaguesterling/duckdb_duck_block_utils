-- Test fixtures for list structures
-- Each test case includes: name, duck_blocks, expected_markdown, expected_html

CREATE OR REPLACE TABLE list_fixtures AS
SELECT * FROM (VALUES
    -- Simple bullet list
    ('bullet_list_simple',
     db_list([
         db_list_item('First item'),
         db_list_item('Second item'),
         db_list_item('Third item')
     ]),
     '- First item
- Second item
- Third item',
     '<ul>
<li>First item</li>
<li>Second item</li>
<li>Third item</li>
</ul>'),

    -- Simple ordered list
    ('ordered_list_simple',
     db_list(true, [
         db_list_item('First item'),
         db_list_item('Second item'),
         db_list_item('Third item')
     ]),
     '1. First item
2. Second item
3. Third item',
     '<ol>
<li>First item</li>
<li>Second item</li>
<li>Third item</li>
</ol>'),

    -- List with formatted items
    ('bullet_list_formatted',
     db_list([
         db_list_item([db_bold('Important'), db_text(' item')]),
         db_list_item([db_text('Item with '), db_inline_code('code')]),
         db_list_item([db_link('https://example.com', 'Link item')])
     ]),
     '- **Important** item
- Item with `code`
- [Link item](https://example.com)',
     '<ul>
<li><strong>Important</strong> item</li>
<li>Item with <code>code</code></li>
<li><a href="https://example.com">Link item</a></li>
</ul>'),

    -- Nested bullet list
    ('bullet_list_nested',
     db_list([
         db_list_item('Parent item 1'),
         db_list([
             db_list_item('Child item 1a'),
             db_list_item('Child item 1b')
         ]),
         db_list_item('Parent item 2'),
         db_list([
             db_list_item('Child item 2a')
         ])
     ]),
     '- Parent item 1
  - Child item 1a
  - Child item 1b
- Parent item 2
  - Child item 2a',
     '<ul>
<li>Parent item 1
<ul>
<li>Child item 1a</li>
<li>Child item 1b</li>
</ul>
</li>
<li>Parent item 2
<ul>
<li>Child item 2a</li>
</ul>
</li>
</ul>'),

    -- Mixed nested list (ordered with unordered children)
    ('mixed_nested_list',
     db_list(true, [
         db_list_item('Step 1'),
         db_list([
             db_list_item('Detail A'),
             db_list_item('Detail B')
         ]),
         db_list_item('Step 2')
     ]),
     '1. Step 1
   - Detail A
   - Detail B
2. Step 2',
     '<ol>
<li>Step 1
<ul>
<li>Detail A</li>
<li>Detail B</li>
</ul>
</li>
<li>Step 2</li>
</ol>'),

    -- Deeply nested list
    ('deeply_nested_list',
     db_list([
         db_list_item('Level 1'),
         db_list([
             db_list_item('Level 2'),
             db_list([
                 db_list_item('Level 3'),
                 db_list([
                     db_list_item('Level 4')
                 ])
             ])
         ])
     ]),
     '- Level 1
  - Level 2
    - Level 3
      - Level 4',
     '<ul>
<li>Level 1
<ul>
<li>Level 2
<ul>
<li>Level 3
<ul>
<li>Level 4</li>
</ul>
</li>
</ul>
</li>
</ul>
</li>
</ul>')

) AS t(name, blocks, expected_markdown, expected_html);
