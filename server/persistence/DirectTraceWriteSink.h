#pragma once

#include <memory>

#include "persistence/TraceWriteSink.h"

// DirectTraceWriteSink 是 benchmark 的 no-buffer 对照组实现。
// 它不做双缓冲、不起后台 flush 线程，而是把 manager 送来的写请求直接同步打到 SQLite repo。
class DirectTraceWriteSink : public TraceWriteSink
{
public:
    explicit DirectTraceWriteSink(std::shared_ptr<TraceRepository> sink);

    bool AppendPrimary(TracePrimaryWrite write) override;
    bool AppendAnalysis(TraceAnalysisWrite write) override;
    bool UpdateTraceAiState(const std::string& trace_id,
                            const std::string& ai_status,
                            const std::string& ai_error) override;

private:
    std::shared_ptr<TraceRepository> sink_;
};
