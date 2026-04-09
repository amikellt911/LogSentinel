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
    // 这里返回 TraceAiResponse 而不是裸 LogAnalysisResult，是为了把业务分析结果和 usage 元数据分开。
    // 既然后续系统监控和告警 token_count 都会依赖 usage，
    // 那就不应该再把“真实 token 使用量”硬塞进 analysis JSON 里假装它是业务字段。
    virtual TraceAiResponse AnalyzeTrace(const std::string& trace_payload) = 0;
};
