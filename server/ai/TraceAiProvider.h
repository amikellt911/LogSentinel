// ai/TraceAiProvider.h
#pragma once

#include <string>
#include "ai/AiTypes.h"

// TraceAiProvider 作为 Trace 语义分析的抽象接口，便于后续替换不同模型或代理实现。
class TraceAiProvider
{
public:
    virtual ~TraceAiProvider() = default;

    // 对单条 Trace 进行分析，输入为序列化后的 Trace 数据。
    virtual LogAnalysisResult AnalyzeTrace(const std::string& trace_payload) = 0;
};
