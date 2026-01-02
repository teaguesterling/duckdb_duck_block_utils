#include "block_types.hpp"

namespace duckdb {

LogicalType BlockTypes::DocElementType() {
	child_list_t<LogicalType> struct_children;
	struct_children.push_back(make_pair("kind", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("element_type", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("level", LogicalType::INTEGER));
	struct_children.push_back(make_pair("encoding", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("attributes",
	                                    LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)));
	struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));

	return LogicalType::STRUCT(std::move(struct_children));
}

LogicalType BlockTypes::DocElementListType() {
	return LogicalType::LIST(DocElementType());
}

LogicalType BlockTypes::DocElementExtType() {
	child_list_t<LogicalType> struct_children;
	struct_children.push_back(make_pair("kind", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("element_type", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("content", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("level", LogicalType::INTEGER));
	struct_children.push_back(make_pair("encoding", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("attributes",
	                                    LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)));
	struct_children.push_back(make_pair("element_order", LogicalType::INTEGER));
	// Extended fields for provenance
	struct_children.push_back(make_pair("source_format", LogicalType::VARCHAR));
	struct_children.push_back(make_pair("file_path", LogicalType::VARCHAR));

	return LogicalType::STRUCT(std::move(struct_children));
}

void BlockTypes::Register(ExtensionLoader &loader) {
	loader.RegisterType("doc_element", DocElementType());
	loader.RegisterType("doc_element_ext", DocElementExtType());
}

} // namespace duckdb
