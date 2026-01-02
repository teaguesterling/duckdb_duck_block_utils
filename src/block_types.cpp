#include "block_types.hpp"

namespace duckdb {

LogicalType BlockTypes::DocBlockType() {
	child_list_t<LogicalType> struct_children;
	struct_children.push_back(make_pair("block_type", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("level", LogicalType::INTEGER));
	struct_children.push_back(make_pair("encoding", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("attributes",
	                                    LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)));
	struct_children.push_back(make_pair("block_order", LogicalType::INTEGER));

	return LogicalType::STRUCT(std::move(struct_children));
}

LogicalType BlockTypes::DocBlockExtType() {
	child_list_t<LogicalType> struct_children;
	struct_children.push_back(make_pair("block_type", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("level", LogicalType::INTEGER));
	struct_children.push_back(make_pair("encoding", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("attributes",
	                                    LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)));
	struct_children.push_back(make_pair("block_order", LogicalType::INTEGER));
	// Extended fields for provenance
	struct_children.push_back(make_pair("source_format", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("file_path", LogicalType::VARCHAR));

	return LogicalType::STRUCT(std::move(struct_children));
}

LogicalType BlockTypes::DocBlockListType() {
	return LogicalType::LIST(DocBlockType());
}

LogicalType BlockTypes::DocInlineType() {
	child_list_t<LogicalType> struct_children;
	struct_children.push_back(make_pair("inline_type", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("attributes",
	                                    LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)));

	return LogicalType::STRUCT(std::move(struct_children));
}

LogicalType BlockTypes::DocInlineListType() {
	return LogicalType::LIST(DocInlineType());
}

void BlockTypes::Register(ExtensionLoader &loader) {
	// Register the core doc_block type
	loader.RegisterType("doc_block", DocBlockType());

	// Register the extended type with provenance fields
	loader.RegisterType("doc_block_ext", DocBlockExtType());

	// Register the doc_inline type for inline elements
	loader.RegisterType("doc_inline", DocInlineType());
}

} // namespace duckdb
