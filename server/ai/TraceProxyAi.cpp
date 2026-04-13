#include "ai/TraceProxyProtocol.h"
#include "ai/TraceProxyAi.h"
#include "ai/TraceProxyTransportError.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <stdexcept>

TraceProxyAi::TraceProxyAi(std::string base_url,
                           TraceAiBackend backend,
                           int timeout_ms,
                           std::string prompt_template,
                           std::string model,
                           std::string api_key,
                           bool retry_enabled,
                           int retry_max_attempts,
                           std::function<uint64_t()> runtime_version_reader,
                           std::function<TraceAiRuntimeCredentials()> runtime_credentials_reader)
    : timeout_ms_(timeout_ms > 0 ? timeout_ms : 10000),
      prompt_template_(std::move(prompt_template)),
      runtime_version_reader_(std::move(runtime_version_reader)),
      runtime_credentials_reader_(std::move(runtime_credentials_reader)),
      retry_enabled_(retry_enabled),
      retry_max_attempts_(retry_max_attempts > 0 ? retry_max_attempts : 1)
{
    if (!base_url.empty() && base_url.back() == '/') {
        base_url.pop_back();
    }
    analyze_trace_url_ = base_url + "/analyze/trace/" + TraceAiBackendToRouteSegment(backend);

    auto initial_runtime_config = std::make_shared<RuntimeRequestConfig>();
    initial_runtime_config->version = runtime_version_reader_ ? runtime_version_reader_() : 0;
    initial_runtime_config->model = std::move(model);
    initial_runtime_config->api_key = std::move(api_key);
    std::atomic_store_explicit(
        &runtime_request_config_,
        std::shared_ptr<const RuntimeRequestConfig>(std::move(initial_runtime_config)),
        std::memory_order_release);
}

TraceProxyAi::~TraceProxyAi() = default;

std::shared_ptr<const TraceProxyAi::RuntimeRequestConfig> TraceProxyAi::GetRuntimeRequestConfig()
{
    auto current_config = std::atomic_load_explicit(&runtime_request_config_, std::memory_order_acquire);
    if (!runtime_version_reader_ || !runtime_credentials_reader_) {
        return current_config;
    }

    const uint64_t latest_version = runtime_version_reader_();
    if (current_config && current_config->version == latest_version) {
        return current_config;
    }

    // 版本变了才去读新凭证。
    // 这里即使有两个 worker 同时刷新，也只是多构造一份等价小对象，不会去原地改共享字符串。
    TraceAiRuntimeCredentials refreshed_credentials = runtime_credentials_reader_();
    auto refreshed_config = std::make_shared<RuntimeRequestConfig>();
    refreshed_config->version = latest_version;
    refreshed_config->model = std::move(refreshed_credentials.model);
    refreshed_config->api_key = std::move(refreshed_credentials.api_key);
    std::atomic_store_explicit(
        &runtime_request_config_,
        std::shared_ptr<const RuntimeRequestConfig>(refreshed_config),
        std::memory_order_release);
    return refreshed_config;
}

TraceAiResponse TraceProxyAi::AnalyzeTrace(const std::string& trace_payload)
{
    const std::shared_ptr<const RuntimeRequestConfig> runtime_request_config = GetRuntimeRequestConfig();
    cpr::Session session;
    session.SetHeader(cpr::Header{{"Content-Type", "application/json"}});
    session.SetTimeout(cpr::Timeout{timeout_ms_});
    session.SetUrl(cpr::Url{analyze_trace_url_});
    // Trace 路由这里改成 JSON，不再只发裸文本。
    // 原因是 ai_language 和业务 prompt 都已经在 C++ 启动期收口成冷启动模板，
    // 只有把 prompt 显式下发给 proxy，Settings 里的 Prompt/语言配置才算真的进入 trace AI 主链。
    nlohmann::json request_json;
    request_json["trace_text"] = trace_payload;
    request_json["prompt"] = prompt_template_;
    // 这里不强行要求 model/api_key 一定非空。
    // 既然 provider 本身已经支持“优先吃请求值，没有就回退默认配置”，
    // 那 TraceProxyAi 只负责把冷启动阶段算好的值尽量透传过去。
    if (runtime_request_config && !runtime_request_config->model.empty()) {
        request_json["model"] = runtime_request_config->model;
    }
    if (runtime_request_config && !runtime_request_config->api_key.empty()) {
        request_json["api_key"] = runtime_request_config->api_key;
    }
    // 这里把 C++ 外层等待预算继续下发给 Python proxy。
    // 否则 proxy 调 GLM 时只能用自己的固定 timeout，内外两层很容易卡在同一秒同时超时，
    // 最终外层 caller 先报 HTTP 0，拿不到 proxy 已经包装好的结构化失败 JSON。
    request_json["timeout_ms"] = timeout_ms_;
    // retry 配置继续跟着请求体下发。
    // 既然 ai_timeout_ms 的语义已经收成“单个 provider 调用链总预算”，
    // 那 proxy 就必须同时知道这条链到底允不允许重试、最多能试几次，不能只知道 timeout 却不知道重试开关。
    request_json["retry_enabled"] = retry_enabled_;
    request_json["retry_max_attempts"] = retry_max_attempts_;
    session.SetBody(cpr::Body{request_json.dump()});

    cpr::Response r = session.Post();
    if (r.status_code != 200) {
        // status_code=0 代表这次根本没拿到有效 HTTP 响应，通常是连接、超时或 TLS 这类传输层问题。
        // 这里统一把 cpr 的 error.code / error.message 也带出来，避免前端和日志只剩一条空壳 HTTP 0。
        throw std::runtime_error(BuildTraceProxyTransportErrorMessage(analyze_trace_url_, r));
    }

    nlohmann::json response_json;
    try {
        response_json = nlohmann::json::parse(r.text);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Trace AI Protocol Error: invalid JSON from proxy. " + std::string(e.what()));
    }

    // proxy 现在会把 provider 失败也编码进 JSON body，而不是只靠 HTTP 500 文本。
    // 所以这里解析完 JSON 后必须继续吃一层协议语义，才能把 ok=false 转成 manager 可落库的失败信息。
    return ParseTraceProxyResponseOrThrow(response_json);
}
