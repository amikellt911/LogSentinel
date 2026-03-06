#pragma once

#include "ai/TraceAiBackend.h"
#include "ai/TraceAiProvider.h"
#include <string>

// 统一通过 Python proxy 的 trace 分析实现。
// backend 控制访问 /analyze/trace/{mock|gemini} 哪个路由。
class TraceProxyAi : public TraceAiProvider
{
public:
    explicit TraceProxyAi(std::string base_url,
                          TraceAiBackend backend,
                          int timeout_ms = 10000);
    ~TraceProxyAi() override;

    LogAnalysisResult AnalyzeTrace(const std::string& trace_payload) override;

private:
    std::string analyze_trace_url_;
    int timeout_ms_ = 10000;
};
