#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace persistence {
// Trace 相关的持久化结构定义，便于在不同仓储实现中复用。
struct TraceSummary
{
    std::string trace_id;
    std::string service_name;
    int64_t start_time_ms = 0;
    std::optional<int64_t> end_time_ms;
    int64_t duration_ms = 0;
    size_t span_count = 0;
    size_t token_count = 0;
    std::string risk_level;
    // ai_status 记录“这条 trace 的 AI 执行最后走到了哪一步”，
    // 它和真正的 analysis 内容不是一回事：
    // 即使没产出 trace_analysis，也要能区分是人工关闭、熔断跳过还是调用失败。
    std::string ai_status = "pending";
    // ai_error 只保留原始异常摘要，不负责前端展示文案。
    // 真正的人话提示由前端根据 ai_status 自己映射，避免数据库里反复存一堆重复文案。
    std::string ai_error;
};

struct TraceSpanRecord
{
    std::string trace_id;
    std::string span_id;
    std::optional<std::string> parent_id;
    std::string service_name;
    std::string operation;
    int64_t start_time_ms = 0;
    int64_t duration_ms = 0;
    std::string status;
    std::string attributes_json;
};

struct TraceAnalysisRecord
{
    std::string trace_id;
    std::string risk_level;
    std::string summary;
    std::string root_cause;
    std::string solution;
    double confidence = 0.0;
};
} // namespace persistence
