// ai/MockTraceAi.h
#pragma once

#include "ai/TraceAiProvider.h"
#include <string>

// 前向声明，避免在头文件里引入 cpr 头文件导致编译依赖膨胀。
namespace cpr {
    class Session;
}

// MockTraceAi 作为 Trace AI 的占位实现，便于先打通代理调用流程。
class MockTraceAi : public TraceAiProvider
{
public:
    MockTraceAi();
    ~MockTraceAi() override;

    // 保持与 TraceAiProvider 接口一致，便于后续替换为真实模型。
    // mock 默认不返回 usage，让上层继续走本地估算回退路径。
    TraceAiResponse AnalyzeTrace(const std::string& trace_payload) override;

private:
    std::string analyze_trace_url_;
};
