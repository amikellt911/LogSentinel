#pragma once

#include <string>
#include "persistence/TraceTypes.h"

// TraceRepository 作为 Trace 持久化的抽象接口，便于后续替换不同存储实现。
class TraceRepository
{
public:
    virtual ~TraceRepository() = default;

    using TraceSummary = persistence::TraceSummary;
    using TraceSpanRecord = persistence::TraceSpanRecord;
    using TraceAnalysisRecord = persistence::TraceAnalysisRecord;
    using PromptDebugRecord = persistence::PromptDebugRecord;

    // 最小表结构建议（用于 TraceExplorer）：
    // 1) trace_summary: trace_id, service_name, start_time_ms, end_time_ms, duration_ms,
    //    span_count, token_count, risk_level, tags
    // 2) trace_span: trace_id, span_id, parent_id, service_name, operation,
    //    start_time_ms, duration_ms, status
    // 3) trace_analysis: trace_id, risk_level, summary, root_cause, solution, confidence
    // 4) prompt_debug（可选）: trace_id, input, output, metadata


    // Trace 聚合完成后存储 summary 与 spans，供列表与详情查询。
    virtual bool SaveTraceSummary(const TraceSummary& summary) = 0;
    virtual bool SaveTraceSpans(const std::string& trace_id, const std::vector<TraceSpanRecord>& spans) = 0;
    // AI 分析完成后存储结果，供 Trace 详情展示。
    virtual bool SaveTraceAnalysis(const TraceAnalysisRecord& analysis) = 0;
    // Prompt 调试信息存储，便于后续排查与回溯。
    virtual bool SavePromptDebug(const PromptDebugRecord& record) = 0;

    // 批量保存聚合结果，优先用于主线闭环；分析与调试信息可为空指针。
    virtual bool SaveTraceBatch(const TraceSummary& summary,
                                const std::vector<TraceSpanRecord>& spans,
                                const TraceAnalysisRecord* analysis,
                                const PromptDebugRecord* prompt_debug) = 0;
};
