#pragma once

#include <string>
#include <unordered_map>
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
    virtual bool SaveSingleTraceSummary(const TraceSummary& summary) = 0;
    virtual bool SaveSingleTraceSpans(const std::string& trace_id, const std::vector<TraceSpanRecord>& spans) = 0;
    // AI 分析完成后存储结果，供 Trace 详情展示。
    virtual bool SaveSingleTraceAnalysis(const TraceAnalysisRecord& analysis) = 0;
    // Prompt 调试信息存储，便于后续排查与回溯。
    virtual bool SaveSinglePromptDebug(const PromptDebugRecord& record) = 0;

    // 单个 Trace 的多表原子写入；analysis 与 prompt_debug 可为空指针。
    virtual bool SaveSingleTraceAtomic(const TraceSummary& summary,
                                const std::vector<TraceSpanRecord>& spans,
                                const TraceAnalysisRecord* analysis,
                                const PromptDebugRecord* prompt_debug) = 0;

    // 批量写主数据。这里直接按表给两条平铺数据流：
    // summaries 对 trace_summary 表，spans 对 trace_span 表。
    // 既然缓冲桶内部本来就是 SoA 形状，那么后台 flush 就不该再把它们重组回 AoS。
    virtual bool SavePrimaryBatch(const std::vector<TraceSummary>& summaries,
                                  const std::vector<TraceSpanRecord>& spans)
    {
        for (const auto& summary : summaries) {
            if (!SaveSingleTraceSummary(summary)) {
                return false;
            }
        }
        if (!spans.empty()) {
            std::unordered_map<std::string, std::vector<TraceSpanRecord>> grouped_spans;
            for (const auto& span : spans) {
                grouped_spans[span.trace_id].push_back(span);
            }
            for (const auto& entry : grouped_spans) {
                if (!SaveSingleTraceSpans(entry.first, entry.second)) {
                    return false;
                }
            }
        }
        return true;
    }

    // 批量写分析结果，同样直接按表给两条平铺数据流。
    virtual bool SaveAnalysisBatch(const std::vector<TraceAnalysisRecord>& analyses,
                                   const std::vector<PromptDebugRecord>& prompt_debugs)
    {
        for (const auto& analysis : analyses) {
            if (!SaveSingleTraceAnalysis(analysis)) {
                return false;
            }
        }
        for (const auto& prompt_debug : prompt_debugs) {
            if (!SaveSinglePromptDebug(prompt_debug)) {
                return false;
            }
        }
        return true;
    }
};
