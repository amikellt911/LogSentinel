#include "ai/TraceProxyProtocol.h"
#include "ai/TraceProxyAi.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <stdexcept>

TraceProxyAi::TraceProxyAi(std::string base_url,
                           TraceAiBackend backend,
                           int timeout_ms,
                           std::string prompt_template,
                           std::string model,
                           std::string api_key)
    : timeout_ms_(timeout_ms > 0 ? timeout_ms : 10000),
      prompt_template_(std::move(prompt_template)),
      model_(std::move(model)),
      api_key_(std::move(api_key))
{
    if (!base_url.empty() && base_url.back() == '/') {
        base_url.pop_back();
    }
    analyze_trace_url_ = base_url + "/analyze/trace/" + TraceAiBackendToRouteSegment(backend);
}

TraceProxyAi::~TraceProxyAi() = default;

TraceAiResponse TraceProxyAi::AnalyzeTrace(const std::string& trace_payload)
{
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
    if (!model_.empty()) {
        request_json["model"] = model_;
    }
    if (!api_key_.empty()) {
        request_json["api_key"] = api_key_;
    }
    session.SetBody(cpr::Body{request_json.dump()});

    cpr::Response r = session.Post();
    if (r.status_code != 200) {
        throw std::runtime_error("Trace AI Proxy Error: HTTP " + std::to_string(r.status_code) +
                                 ", Body: " + r.text);
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
