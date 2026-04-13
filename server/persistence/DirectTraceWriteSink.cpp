#include "persistence/DirectTraceWriteSink.h"

#include <stdexcept>

DirectTraceWriteSink::DirectTraceWriteSink(std::shared_ptr<TraceRepository> sink)
    : sink_(std::move(sink))
{
    if (!sink_) {
        throw std::invalid_argument("DirectTraceWriteSink requires non-null sink");
    }
}

bool DirectTraceWriteSink::AppendPrimary(TracePrimaryWrite write)
{
    // no-buffer 模式拿掉的是“前置缓冲 + 后台 flush 线程”，不是 primary 一致性语义。
    // 所以这里继续用原子写接口，把 summary + spans 放进同一事务。
    return sink_->SaveSingleTraceAtomic(write.summary, write.spans, nullptr);
}

bool DirectTraceWriteSink::AppendAnalysis(TraceAnalysisWrite write)
{
    // 当前主链只有 analysis 存在时才会走到这里。
    // 这里保留空 optional 兜底，是为了避免测试辅助以后误传空值时直接把 no-buffer 分支打崩。
    if (!write.analysis.has_value()) {
        return true;
    }
    return sink_->SaveSingleTraceAnalysis(write.analysis.value());
}

bool DirectTraceWriteSink::UpdateTraceAiState(const std::string& trace_id,
                                              const std::string& ai_status,
                                              const std::string& ai_error)
{
    return sink_->UpdateTraceAiState(trace_id, ai_status, ai_error);
}
