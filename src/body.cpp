#include "body.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"

namespace duckdb {

namespace {

string Field(const Value &v, idx_t idx) {
	auto &c = StructValue::GetChildren(v);
	return (idx < c.size() && !c[idx].IsNull()) ? c[idx].GetValue<string>() : string();
}

int32_t IntField(const Value &v, idx_t idx, int32_t def) {
	auto &c = StructValue::GetChildren(v);
	return (idx < c.size() && !c[idx].IsNull()) ? c[idx].GetValue<int32_t>() : def;
}

} // namespace

void BodyFunctions::DbBlocksBodyFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto block_type = BlockTypes::DuckBlockType();
	for (idx_t i = 0; i < args.size(); i++) {
		auto in = args.data[0].GetValue(i);
		if (in.IsNull()) {
			result.SetValue(i, in);
			continue;
		}
		vector<Value> out;
		bool in_subtree = false;
		int32_t root_level = 0;
		for (auto &el : ListValue::GetChildren(in)) {
			if (el.IsNull()) {
				continue;
			}
			const auto level = IntField(el, BlockTypes::LEVEL_IDX, 1);
			if (in_subtree) {
				if (level > root_level) {
					continue; // a descendant of the value/metadata root
				}
				in_subtree = false; // the subtree ended at this row
			}
			const auto kind = Field(el, BlockTypes::KIND_IDX);
			const auto type = Field(el, BlockTypes::ELEMENT_TYPE_IDX);
			// A value or metadata row roots a subtree; anything deeper is its own. Named
			// explicitly rather than as !IsBody: a future kind must not silently become
			// a subtree root that swallows what nests under it.
			if (kind == BlockTypes::KIND_VALUE || type == BlockTypes::TYPE_METADATA) {
				in_subtree = true;
				root_level = level;
				continue;
			}
			if (!BlockTypes::IsBody(kind.c_str(), type.c_str())) {
				continue; // not body on its own account, and not a subtree root
			}
			out.push_back(el);
		}
		result.SetValue(i, Value::LIST(block_type, std::move(out)));
	}
}

void BodyFunctions::Register(ExtensionLoader &loader) {
	loader.RegisterFunction(ScalarFunction("duck_blocks_body", {BlockTypes::DuckBlockListType()},
	                                       BlockTypes::DuckBlockListType(), DbBlocksBodyFun));
}

} // namespace duckdb
