#pragma once

#include "ai/TraceAiBackend.h"
#include "ai/TraceAiProvider.h"
#include "ai/TraceAiFactory.h"
#include <functional>
#include <memory>
#include <string>

// 统一通过 Python proxy 的 trace 分析实现。
// backend 控制访问 /analyze/trace/{mock|gemini} 哪个路由。
class TraceProxyAi : public TraceAiProvider
{
public:
    explicit TraceProxyAi(std::string base_url,
                          TraceAiBackend backend,
                          int timeout_ms = 10000,
                          std::string prompt_template = "",
                          std::string model = "",
                          std::string api_key = "",
                          bool retry_enabled = false,
                          int retry_max_attempts = 3,
                          std::function<uint64_t()> runtime_version_reader = {},
                          std::function<TraceAiRuntimeCredentials()> runtime_credentials_reader = {});
    ~TraceProxyAi() override;

    // 代理返回现在既包含结构化 analysis，也可能带 usage 元数据。
    // 所以这里直接把两者一起还给上层，避免 manager 再自己反序列化 HTTP JSON。
    TraceAiResponse AnalyzeTrace(const std::string& trace_payload) override;

private:
    struct RuntimeRequestConfig
    {
        uint64_t version = 0;
        std::string model;
        std::string api_key;
    };

    std::shared_ptr<const RuntimeRequestConfig> GetRuntimeRequestConfig();

    std::string analyze_trace_url_;
    int timeout_ms_ = 10000;
    // 这里缓存的是“已经带语言约束和业务 guidance 的 Trace Prompt 模板”，
    // 但还没注入本次 trace_text；真正的 trace 上下文会在 proxy 路由里最后一步填进去。
    std::string prompt_template_;
    // 运行时凭证单独做成不可变小快照，并通过 shared_ptr 原子换代。
    // 原因是同一个 provider 对象会被多个 worker 线程并发复用，不能直接在成员字符串上原地改值。
    std::shared_ptr<const RuntimeRequestConfig> runtime_request_config_;
    std::function<uint64_t()> runtime_version_reader_;
    std::function<TraceAiRuntimeCredentials()> runtime_credentials_reader_;
    // retry 配置不在 SessionManager 热路径里反复判断，而是跟着 provider 对象一起缓存。
    // 这样每次 AnalyzeTrace 只负责把本次 trace payload 送出去，不需要再自己拼 retry 语义。
    bool retry_enabled_ = false;
    int retry_max_attempts_ = 3;
};
