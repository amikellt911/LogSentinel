#pragma once

#include <optional>
#include <string>
#include <vector>

#include "persistence/TraceRepository.h"

// TraceWriteSink 抽的是 TraceSessionManager 往持久化层送写请求的最小公共面。
// 这里故意只保留 primary / analysis / ai_status 三个入口，避免 benchmark 这刀为了切 no-buffer，
// 反手把查询、批量 flush、运行时统计也一股脑塞成“大而全”的新抽象。
class TraceWriteSink
{
public:
    virtual ~TraceWriteSink() = default;

    using TraceSummary = TraceRepository::TraceSummary;
    using TraceSpanRecord = TraceRepository::TraceSpanRecord;
    using TraceAnalysisRecord = TraceRepository::TraceAnalysisRecord;

    struct TracePrimaryWrite
    {
        TraceSummary summary;
        std::vector<TraceSpanRecord> spans;
    };

    struct TraceAnalysisWrite
    {
        std::optional<TraceAnalysisRecord> analysis;
    };

    virtual bool AppendPrimary(TracePrimaryWrite write) = 0;
    virtual bool AppendAnalysis(TraceAnalysisWrite write) = 0;
    virtual bool UpdateTraceAiState(const std::string& trace_id,
                                    const std::string& ai_status,
                                    const std::string& ai_error) = 0;
};
