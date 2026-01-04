#include "pragma_aliases.hpp"
#include "duckdb/function/pragma_function.hpp"
#include "duckdb/main/client_context.hpp"

namespace duckdb {

// Returns SQL to create all short alias macros
static string DuckBlockAliasesQuery(ClientContext &context, const FunctionParameters &parameters) {
	// Build a single SQL string with all CREATE MACRO statements
	// Using CREATE OR REPLACE to be idempotent
	string sql = R"(
-- Document Assembly
CREATE OR REPLACE MACRO page(children) AS db_assemble(children);
CREATE OR REPLACE MACRO doc(children) AS db_assemble(children);

-- Headings (h1-h6 with level preset)
CREATE OR REPLACE MACRO h1(content) AS db_heading(1, content);
CREATE OR REPLACE MACRO h2(content) AS db_heading(2, content);
CREATE OR REPLACE MACRO h3(content) AS db_heading(3, content);
CREATE OR REPLACE MACRO h4(content) AS db_heading(4, content);
CREATE OR REPLACE MACRO h5(content) AS db_heading(5, content);
CREATE OR REPLACE MACRO h6(content) AS db_heading(6, content);

-- Block Elements
CREATE OR REPLACE MACRO p(content) AS db_paragraph(content);
CREATE OR REPLACE MACRO pre(lang, content) AS db_code(lang, content);
CREATE OR REPLACE MACRO blockquote(content) AS db_blockquote(content);
CREATE OR REPLACE MACRO bq(content) AS db_blockquote(content);
CREATE OR REPLACE MACRO ul(items) AS db_list_block(false, items);
CREATE OR REPLACE MACRO ol(items) AS db_list_block(true, items);
CREATE OR REPLACE MACRO li(content) AS db_list_item(content);
CREATE OR REPLACE MACRO hr() AS db_hr();
CREATE OR REPLACE MACRO img(src, alt) AS db_image(src, alt);
CREATE OR REPLACE MACRO div(children) AS db_div(children);

-- Inline Elements
CREATE OR REPLACE MACRO text(content) AS db_text(content);
CREATE OR REPLACE MACRO b(content) AS db_bold(content);
CREATE OR REPLACE MACRO strong(content) AS db_bold(content);
CREATE OR REPLACE MACRO i(content) AS db_italic(content);
CREATE OR REPLACE MACRO em(content) AS db_italic(content);
CREATE OR REPLACE MACRO a(url, text) AS db_link(url, text);
CREATE OR REPLACE MACRO code(content) AS db_inline_code(content);
CREATE OR REPLACE MACRO s(content) AS db_strikethrough(content);
CREATE OR REPLACE MACRO del(content) AS db_strikethrough(content);
CREATE OR REPLACE MACRO sup(content) AS db_superscript(content);
CREATE OR REPLACE MACRO sub(content) AS db_subscript(content);
CREATE OR REPLACE MACRO span(content) AS db_span(content);
CREATE OR REPLACE MACRO math(content) AS db_math(content);

SELECT 'Duck block aliases registered: page, doc, h1-h6, p, pre, blockquote, bq, ul, ol, li, hr, img, div, text, b, strong, i, em, a, code, s, del, sup, sub, span, math' AS message;
)";
	return sql;
}

void PragmaAliases::Register(ExtensionLoader &loader) {
	// Register the pragma that enables short aliases
	auto pragma = PragmaFunction::PragmaStatement("duck_block_aliases", DuckBlockAliasesQuery);
	loader.RegisterFunction(pragma);
}

} // namespace duckdb
