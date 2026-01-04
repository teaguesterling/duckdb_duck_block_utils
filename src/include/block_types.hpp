#pragma once

#include "duckdb.hpp"
#include "duckdb/common/types.hpp"

namespace duckdb {

class BlockTypes {
public:
	// ========================================================================
	// Unified duck_block type
	// ========================================================================

	// Create the duck_block type
	static LogicalType DuckBlockType();

	// Create a LIST(duck_block) type
	static LogicalType DuckBlockListType();

	// Create the extended duck_block type with provenance fields
	static LogicalType DuckBlockExtType();

	// Register types with the extension loader
	static void Register(ExtensionLoader &loader);

	// Field indices for duck_block struct
	static constexpr idx_t KIND_IDX = 0;
	static constexpr idx_t ELEMENT_TYPE_IDX = 1;
	static constexpr idx_t CONTENT_IDX = 2;
	static constexpr idx_t LEVEL_IDX = 3;
	static constexpr idx_t ENCODING_IDX = 4;
	static constexpr idx_t ATTRIBUTES_IDX = 5;
	static constexpr idx_t ELEMENT_ORDER_IDX = 6;

	// Additional field indices for duck_block_ext
	static constexpr idx_t SOURCE_FORMAT_IDX = 7;
	static constexpr idx_t FILE_PATH_IDX = 8;

	// Kind values
	static constexpr const char *KIND_BLOCK = "block";
	static constexpr const char *KIND_INLINE = "inline";

	// ========================================================================
	// Block type names
	// ========================================================================
	static constexpr const char *TYPE_HEADING = "heading";
	static constexpr const char *TYPE_PARAGRAPH = "paragraph";
	static constexpr const char *TYPE_CODE = "code";
	static constexpr const char *TYPE_BLOCKQUOTE = "blockquote";
	static constexpr const char *TYPE_LIST = "list";
	static constexpr const char *TYPE_LIST_ITEM = "list_item";
	static constexpr const char *TYPE_TABLE = "table";
	static constexpr const char *TYPE_HR = "hr";
	static constexpr const char *TYPE_METADATA = "metadata";
	static constexpr const char *TYPE_IMAGE = "image";
	static constexpr const char *TYPE_RAW = "raw";
	static constexpr const char *TYPE_DIV = "div";

	// ========================================================================
	// Inline type names
	// ========================================================================
	// Text and whitespace
	static constexpr const char *INLINE_TEXT = "text";
	static constexpr const char *INLINE_SPACE = "space";
	static constexpr const char *INLINE_SOFTBREAK = "softbreak";
	static constexpr const char *INLINE_LINEBREAK = "linebreak";

	// Formatting (container types)
	static constexpr const char *INLINE_BOLD = "bold";
	static constexpr const char *INLINE_ITALIC = "italic";
	static constexpr const char *INLINE_STRIKETHROUGH = "strikethrough";
	static constexpr const char *INLINE_SUPERSCRIPT = "superscript";
	static constexpr const char *INLINE_SUBSCRIPT = "subscript";
	static constexpr const char *INLINE_SMALLCAPS = "smallcaps";
	static constexpr const char *INLINE_UNDERLINE = "underline";

	// Semantic
	static constexpr const char *INLINE_CODE = "code";
	static constexpr const char *INLINE_MATH = "math";
	static constexpr const char *INLINE_LINK = "link";
	static constexpr const char *INLINE_IMAGE = "image";
	static constexpr const char *INLINE_QUOTED = "quoted";
	static constexpr const char *INLINE_CITE = "cite";
	static constexpr const char *INLINE_NOTE = "note";
	static constexpr const char *INLINE_SPAN = "span";
	static constexpr const char *INLINE_RAW = "raw";

	// ========================================================================
	// Encoding values
	// ========================================================================
	static constexpr const char *ENCODING_TEXT = "text";
	static constexpr const char *ENCODING_JSON = "json";
	static constexpr const char *ENCODING_YAML = "yaml";
	static constexpr const char *ENCODING_HTML = "html";
	static constexpr const char *ENCODING_XML = "xml";
	static constexpr const char *ENCODING_LATEX = "latex";
	static constexpr const char *ENCODING_MARKDOWN = "markdown";
};

} // namespace duckdb
