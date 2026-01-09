#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// TraceRepository 作为 Trace 持久化的抽象接口，便于后续替换不同存储实现。
class TraceRepository
{
public:
    virtual ~TraceRepository() = default;

    // 最小表结构建议（用于 TraceExplorer）：
    // 1) trace_summary: trace_id, service_name, start_time_ms, end_time_ms, duration_ms,
    //    span_count, token_count, risk_level, tags
    // 2) trace_span: trace_id, span_id, parent_id, service_name, operation,
    //    start_time_ms, duration_ms, status
    // 3) trace_analysis: trace_id, risk_level, summary, root_cause, solution, confidence
    // 4) prompt_debug（可选）: trace_id, input, output, metadata

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
        std::string tags_json;
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
        std::string tags_json;
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

    // Trace 聚合完成后存储 summary 与 spans，供列表与详情查询。
    virtual bool SaveTraceSummary(const TraceSummary& summary) = 0;
    virtual bool SaveTraceSpans(const std::string& trace_id, const std::vector<TraceSpanRecord>& spans) = 0;
    // AI 分析完成后存储结果，供 Trace 详情展示。
    virtual bool SaveTraceAnalysis(const TraceAnalysisRecord& analysis) = 0;
    // Prompt 调试信息存储，便于后续排查与回溯。
    virtual bool SavePromptDebug(const PromptDebugRecord& record) = 0;
};
