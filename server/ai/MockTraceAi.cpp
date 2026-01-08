// ai/MockTraceAi.cpp
#include "ai/MockTraceAi.h"

MockTraceAi::MockTraceAi() = default;

MockTraceAi::~MockTraceAi() = default;

LogAnalysisResult MockTraceAi::AnalyzeTrace(const std::string& trace_payload)
{
    // 仅占位，后续接入真实 Trace 语义分析逻辑。
    (void)trace_payload;
    return {};
}
