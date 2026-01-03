#pragma once
#include <string>
#include <utility>

namespace persistence {

enum class PromptType {
    Map,
    Reduce,
    Unknown
};

class PromptIdHelper {
public:
    // 定义偏移量，封装 Magic Number
    static constexpr int REDUCE_OFFSET = 100000000;

    // 将内部 ID 转换为对外的全局唯一 ID
    static int toExternalId(int internal_id, const std::string& type) {
        if (type == "reduce") {
            return internal_id + REDUCE_OFFSET;
        }
        return internal_id;
    }

    // 解析外部 ID，返回 {内部 ID, 类型}
    static std::pair<int, std::string> parseExternalId(int external_id) {
        if (external_id >= REDUCE_OFFSET) {
            return {external_id - REDUCE_OFFSET, "reduce"};
        }
        return {external_id, "map"};
    }

    // 判断是否为 Reduce ID
    static bool isReduceId(int external_id) {
        return external_id >= REDUCE_OFFSET;
    }
};

} // namespace persistence
