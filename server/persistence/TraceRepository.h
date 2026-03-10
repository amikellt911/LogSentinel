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

    struct TracePrimaryWrite
    {
        // 主数据在 Dispatch 前就已经齐全，所以这里直接按表拆成两个平铺对象。
        TraceSummary summary;
        std::vector<TraceSpanRecord> spans;
    };

    struct TraceAnalysisWrite
    {
        // analysis 与 prompt_debug 都在 AI 返回后才出现，所以单独归到分析缓冲线。
        // 两者都允许为空，目的是兼容“只有 analysis，没有 prompt_debug”的场景。
        std::optional<TraceAnalysisRecord> analysis;
        std::optional<PromptDebugRecord> prompt_debug;
    };

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

    // 批量写主数据。第一版默认退化成逐条调用旧接口，目的是先把“缓冲层和底层 sink 的边界”
    // 立起来，不在这一步强迫所有实现类一起改完。后续像 SQLite 这种实现再 override 成
    // “整批一次事务”的真正批量写。
    virtual bool SavePrimaryBatch(const std::vector<TracePrimaryWrite>& batch)
    {
        for (const auto& item : batch) {
            if (!SaveSingleTraceAtomic(item.summary, item.spans, nullptr, nullptr)) {
                return false;
            }
        }
        return true;
    }

    // 批量写分析结果。这里同样先给一个最小默认实现：
    // 既然 analysis 和 prompt_debug 是两张独立表，那就分别按存在性写入。
    // 后续具体存储实现可以自己 override 成更高效的一批一事务版本。
    virtual bool SaveAnalysisBatch(const std::vector<TraceAnalysisWrite>& batch)
    {
        for (const auto& item : batch) {
            if (item.analysis.has_value() && !SaveSingleTraceAnalysis(item.analysis.value())) {
                return false;
            }
            if (item.prompt_debug.has_value() && !SaveSinglePromptDebug(item.prompt_debug.value())) {
                return false;
            }
        }
        return true;
    }
};
