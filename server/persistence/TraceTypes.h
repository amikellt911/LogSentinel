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

struct PromptDebugRecord
{
    std::string trace_id;
    std::string input_json;
    std::string output_json;
    // metadata 直接拆字段，便于查询与展示。
    std::string model;
    int64_t duration_ms = 0;
    size_t total_tokens = 0;
    std::string timestamp;
};
} // namespace persistence
