#pragma once

#include "duckdb.hpp"
#include "duckdb/common/types.hpp"

namespace duckdb {

class BlockTypes {
public:
	// Create the doc_block type
	static LogicalType DocBlockType();

	// Create the extended doc_block type with provenance fields
	static LogicalType DocBlockExtType();

	// Create a LIST(doc_block) type
	static LogicalType DocBlockListType();

	// Register types with the extension loader
	static void Register(ExtensionLoader &loader);

	// Field indices for doc_block struct
	static constexpr idx_t BLOCK_TYPE_IDX = 0;
	static constexpr idx_t CONTENT_IDX = 1;
	static constexpr idx_t LEVEL_IDX = 2;
	static constexpr idx_t ENCODING_IDX = 3;
	static constexpr idx_t ATTRIBUTES_IDX = 4;
	static constexpr idx_t BLOCK_ORDER_IDX = 5;

	// Additional field indices for doc_block_ext
	static constexpr idx_t SOURCE_FORMAT_IDX = 6;
	static constexpr idx_t FILE_PATH_IDX = 7;

	// Core block type names
	static constexpr const char *TYPE_HEADING = "heading";
	static constexpr const char *TYPE_PARAGRAPH = "paragraph";
	static constexpr const char *TYPE_CODE = "code";
	static constexpr const char *TYPE_BLOCKQUOTE = "blockquote";
	static constexpr const char *TYPE_LIST = "list";
	static constexpr const char *TYPE_TABLE = "table";
	static constexpr const char *TYPE_HR = "hr";
	static constexpr const char *TYPE_METADATA = "metadata";
	static constexpr const char *TYPE_IMAGE = "image";
	static constexpr const char *TYPE_RAW = "raw";

	// Valid encoding values
	static constexpr const char *ENCODING_TEXT = "text";
	static constexpr const char *ENCODING_JSON = "json";
	static constexpr const char *ENCODING_YAML = "yaml";
	static constexpr const char *ENCODING_HTML = "html";
	static constexpr const char *ENCODING_XML = "xml";

	// Create the doc_inline type for inline elements
	static LogicalType DocInlineType();

	// Create a LIST(doc_inline) type
	static LogicalType DocInlineListType();

	// Field indices for doc_inline struct
	static constexpr idx_t INLINE_TYPE_IDX = 0;
	static constexpr idx_t INLINE_CONTENT_IDX = 1;
	static constexpr idx_t INLINE_LEVEL_IDX = 2;
	static constexpr idx_t INLINE_ATTRIBUTES_IDX = 3;
	static constexpr idx_t INLINE_ORDER_IDX = 4;

	// Inline element type names - text and formatting
	static constexpr const char *INLINE_TEXT = "text";
	static constexpr const char *INLINE_SPACE = "space";
	static constexpr const char *INLINE_SOFTBREAK = "softbreak";
	static constexpr const char *INLINE_LINEBREAK = "linebreak";
	static constexpr const char *INLINE_BOLD = "bold";
	static constexpr const char *INLINE_ITALIC = "italic";
	static constexpr const char *INLINE_STRIKETHROUGH = "strikethrough";
	static constexpr const char *INLINE_SUPERSCRIPT = "superscript";
	static constexpr const char *INLINE_SUBSCRIPT = "subscript";
	static constexpr const char *INLINE_SMALLCAPS = "smallcaps";
	static constexpr const char *INLINE_UNDERLINE = "underline";

	// Inline element type names - semantic
	static constexpr const char *INLINE_CODE = "code";
	static constexpr const char *INLINE_MATH = "math";
	static constexpr const char *INLINE_LINK = "link";
	static constexpr const char *INLINE_IMAGE = "image";
	static constexpr const char *INLINE_QUOTED = "quoted";
	static constexpr const char *INLINE_CITE = "cite";
	static constexpr const char *INLINE_NOTE = "note";
	static constexpr const char *INLINE_SPAN = "span";
	static constexpr const char *INLINE_RAW = "raw";
};

} // namespace duckdb
