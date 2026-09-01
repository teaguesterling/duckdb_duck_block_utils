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
CREATE OR REPLACE MACRO page(children) AS duck_blocks_assemble(children);
CREATE OR REPLACE MACRO doc(children) AS duck_blocks_assemble(children);

-- Headings (h1-h6 with level preset)
CREATE OR REPLACE MACRO h1(content) AS duck_block_heading(1, content);
CREATE OR REPLACE MACRO h2(content) AS duck_block_heading(2, content);
CREATE OR REPLACE MACRO h3(content) AS duck_block_heading(3, content);
CREATE OR REPLACE MACRO h4(content) AS duck_block_heading(4, content);
CREATE OR REPLACE MACRO h5(content) AS duck_block_heading(5, content);
CREATE OR REPLACE MACRO h6(content) AS duck_block_heading(6, content);

-- Block Elements
CREATE OR REPLACE MACRO p(content) AS duck_block_paragraph(content);
CREATE OR REPLACE MACRO pre(lang, content) AS duck_block_code(lang, content);
CREATE OR REPLACE MACRO blockquote(content) AS duck_block_blockquote(content);
CREATE OR REPLACE MACRO bq(content) AS duck_block_blockquote(content);
CREATE OR REPLACE MACRO ul(items) AS duck_block_list_block(false, items);
CREATE OR REPLACE MACRO ol(items) AS duck_block_list_block(true, items);
CREATE OR REPLACE MACRO li(content) AS duck_block_list_item(content);
CREATE OR REPLACE MACRO hr() AS duck_block_hr();
CREATE OR REPLACE MACRO img(src, alt) AS duck_block_image(src, alt);
CREATE OR REPLACE MACRO div(children) AS duck_block_div(children);

-- Inline Elements
CREATE OR REPLACE MACRO text(content) AS duck_block_text(content);
CREATE OR REPLACE MACRO b(content) AS duck_block_bold(content);
CREATE OR REPLACE MACRO strong(content) AS duck_block_bold(content);
CREATE OR REPLACE MACRO i(content) AS duck_block_italic(content);
CREATE OR REPLACE MACRO em(content) AS duck_block_italic(content);
CREATE OR REPLACE MACRO a(url, text) AS duck_block_link(url, text);
CREATE OR REPLACE MACRO code(content) AS duck_block_inline_code(content);
CREATE OR REPLACE MACRO s(content) AS duck_block_strikethrough(content);
CREATE OR REPLACE MACRO del(content) AS duck_block_strikethrough(content);
CREATE OR REPLACE MACRO sup(content) AS duck_block_superscript(content);
CREATE OR REPLACE MACRO sub(content) AS duck_block_subscript(content);
CREATE OR REPLACE MACRO span(content) AS duck_block_span(content);
CREATE OR REPLACE MACRO math(content) AS duck_block_math(content);
)";
	return sql;
}

void PragmaAliases::Register(ExtensionLoader &loader) {
	// Register the pragma that enables short aliases
	auto pragma = PragmaFunction::PragmaStatement("duck_block_aliases", DuckBlockAliasesQuery);
	loader.RegisterFunction(pragma);
}

} // namespace duckdb
