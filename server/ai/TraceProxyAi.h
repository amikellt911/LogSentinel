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
                          int timeout_ms = 10000,
                          std::string prompt_template = "");
    ~TraceProxyAi() override;

    // 代理返回现在既包含结构化 analysis，也可能带 usage 元数据。
    // 所以这里直接把两者一起还给上层，避免 manager 再自己反序列化 HTTP JSON。
    TraceAiResponse AnalyzeTrace(const std::string& trace_payload) override;

private:
    std::string analyze_trace_url_;
    int timeout_ms_ = 10000;
    // 这里缓存的是“已经带语言约束和业务 guidance 的 Trace Prompt 模板”，
    // 但还没注入本次 trace_text；真正的 trace 上下文会在 proxy 路由里最后一步填进去。
    std::string prompt_template_;
};
