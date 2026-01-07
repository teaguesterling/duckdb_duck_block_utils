-- Pandoc JSON AST expectations for block types
-- Maps duck_block structures to their expected Pandoc JSON output
-- These can be used to test Pandoc conversion in any direction

CREATE OR REPLACE TABLE pandoc_block_expectations AS
SELECT * FROM (VALUES
    -- Headings
    ('heading_h1',
     db_heading(1, 'Title'),
     '{"t":"Header","c":[1,["title",[],[]],[{"t":"Str","c":"Title"}]]}'),

    ('heading_h2',
     db_heading(2, 'Section'),
     '{"t":"Header","c":[2,["section",[],[]],[{"t":"Str","c":"Section"}]]}'),

    ('heading_with_spaces',
     db_heading(1, 'My Great Title'),
     '{"t":"Header","c":[1,["my-great-title",[],[]],[{"t":"Str","c":"My"},{"t":"Space"},{"t":"Str","c":"Great"},{"t":"Space"},{"t":"Str","c":"Title"}]]}'),

    -- Paragraphs
    ('paragraph_simple',
     db_paragraph('Hello world'),
     '{"t":"Para","c":[{"t":"Str","c":"Hello"},{"t":"Space"},{"t":"Str","c":"world"}]}'),

    ('paragraph_with_bold',
     db_paragraph([db_text('This is '), db_bold('important')]),
     '{"t":"Para","c":[{"t":"Str","c":"This"},{"t":"Space"},{"t":"Str","c":"is"},{"t":"Space"},{"t":"Strong","c":[{"t":"Str","c":"important"}]}]}'),

    ('paragraph_with_italic',
     db_paragraph([db_text('This is '), db_italic('emphasized')]),
     '{"t":"Para","c":[{"t":"Str","c":"This"},{"t":"Space"},{"t":"Str","c":"is"},{"t":"Space"},{"t":"Emph","c":[{"t":"Str","c":"emphasized"}]}]}'),

    ('paragraph_with_code',
     db_paragraph([db_text('Use '), db_inline_code('SELECT')]),
     '{"t":"Para","c":[{"t":"Str","c":"Use"},{"t":"Space"},{"t":"Code","c":[["",[],[]],"SELECT"]}]}'),

    ('paragraph_with_link',
     db_paragraph([db_link('https://example.com', 'Click here')]),
     '{"t":"Para","c":[{"t":"Link","c":[["",[],[]],[{"t":"Str","c":"Click"},{"t":"Space"},{"t":"Str","c":"here"}],["https://example.com",""]]}]}'),

    -- Code blocks
    ('code_block_python',
     db_code('python', 'print("hello")'),
     '{"t":"CodeBlock","c":[["",["python"],[]],"print(\"hello\")"]}'),

    ('code_block_no_lang',
     db_code('', 'plain text'),
     '{"t":"CodeBlock","c":[["",[""],["attribute-key"]],"plain text"]}'),

    -- Blockquotes
    ('blockquote_simple',
     db_blockquote('A wise quote.'),
     '{"t":"BlockQuote","c":[{"t":"Para","c":[{"t":"Str","c":"A"},{"t":"Space"},{"t":"Str","c":"wise"},{"t":"Space"},{"t":"Str","c":"quote."}]}]}'),

    -- Horizontal rule
    ('horizontal_rule',
     db_hr(),
     '{"t":"HorizontalRule"}'),

    -- Images (wrapped in Para since Image is inline)
    ('image_simple',
     db_image('img.png', 'Alt', ''),
     '{"t":"Para","c":[{"t":"Image","c":[["",[],[]],[{"t":"Str","c":"Alt"}],["img.png",""]]}]}'),

    ('image_with_title',
     db_image('img.png', 'Alt text', 'Title'),
     '{"t":"Para","c":[{"t":"Image","c":[["",[],[]],[{"t":"Str","c":"Alt"},{"t":"Space"},{"t":"Str","c":"text"}],["img.png","Title"]]}]}'),

    -- Raw blocks
    ('raw_html',
     db_raw('html', '<div>content</div>'),
     '{"t":"RawBlock","c":["html","<div>content</div>"]}'),

    ('raw_latex',
     db_raw('latex', '\\textbf{bold}'),
     '{"t":"RawBlock","c":["latex","\\textbf{bold}"]}')

) AS t(name, blocks, expected_pandoc_json);

-- Inline element Pandoc expectations (for inlines, not wrapped in blocks)
CREATE OR REPLACE TABLE pandoc_inline_expectations AS
SELECT * FROM (VALUES
    ('text_simple',
     db_text('hello'),
     '[{"t":"Str","c":"hello"}]'),

    ('text_with_spaces',
     db_text('hello world'),
     '[{"t":"Str","c":"hello"},{"t":"Space"},{"t":"Str","c":"world"}]'),

    ('bold_simple',
     db_bold('strong'),
     '[{"t":"Strong","c":[{"t":"Str","c":"strong"}]}]'),

    ('italic_simple',
     db_italic('emphasized'),
     '[{"t":"Emph","c":[{"t":"Str","c":"emphasized"}]}]'),

    ('inline_code',
     db_inline_code('code'),
     '[{"t":"Code","c":[["",[],[]],"code"]}]'),

    ('link_simple',
     db_link('https://example.com', 'text'),
     '[{"t":"Link","c":[["",[],[]],[{"t":"Str","c":"text"}],["https://example.com",""]]}]'),

    ('link_with_title',
     db_link('https://example.com', 'text', 'Title'),
     '[{"t":"Link","c":[["",[],[]],[{"t":"Str","c":"text"}],["https://example.com","Title"]]}]'),

    ('strikethrough',
     db_strikethrough('deleted'),
     '[{"t":"Strikeout","c":[{"t":"Str","c":"deleted"}]}]'),

    ('superscript',
     db_superscript('2'),
     '[{"t":"Superscript","c":[{"t":"Str","c":"2"}]}]'),

    ('subscript',
     db_subscript('i'),
     '[{"t":"Subscript","c":[{"t":"Str","c":"i"}]}]'),

    ('math_inline',
     db_math('x^2'),
     '[{"t":"Math","c":[{"t":"InlineMath"},"x^2"]}]')

) AS t(name, blocks, expected_pandoc_json);

-- List Pandoc expectations
CREATE OR REPLACE TABLE pandoc_list_expectations AS
SELECT * FROM (VALUES
    ('bullet_list_simple',
     db_list([
         db_list_item('One'),
         db_list_item('Two')
     ]),
     '{"t":"BulletList","c":[[{"t":"Plain","c":[{"t":"Str","c":"One"}]}],[{"t":"Plain","c":[{"t":"Str","c":"Two"}]}]]}'),

    ('ordered_list_simple',
     db_list(true, [
         db_list_item('First'),
         db_list_item('Second')
     ]),
     '{"t":"OrderedList","c":[[1,{"t":"Decimal"},{"t":"Period"}],[[{"t":"Plain","c":[{"t":"Str","c":"First"}]}],[{"t":"Plain","c":[{"t":"Str","c":"Second"}]}]]]}')

) AS t(name, blocks, expected_pandoc_json);
