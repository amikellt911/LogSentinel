// ai/MockTraceAi.h
#pragma once

#include "ai/TraceAiProvider.h"

// MockTraceAi 作为 Trace AI 的占位实现，便于后续联调与单测。
class MockTraceAi : public TraceAiProvider
{
public:
    MockTraceAi();
    ~MockTraceAi() override;

    LogAnalysisResult AnalyzeTrace(const std::string& trace_payload) override;
};
