#include "ai/TraceProxyAi.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {
std::optional<TraceAiUsage> ParseTraceUsage(const nlohmann::json& response_json)
{
    // usage 是 proxy 外层的可选元数据。既然不是所有 provider 都会回它，
    // 那这里缺失时直接返回空，让上层继续走估算回退，不把“无 usage”当成协议错误。
    if (!response_json.contains("usage") || response_json["usage"].is_null()) {
        return std::nullopt;
    }
    if (!response_json["usage"].is_object()) {
        throw std::runtime_error("Trace AI Protocol Error: invalid 'usage' payload type");
    }

    const auto& usage_json = response_json["usage"];
    TraceAiUsage usage;
    if (usage_json.contains("input_tokens")) {
        usage.input_tokens = usage_json["input_tokens"].get<size_t>();
    }
    if (usage_json.contains("output_tokens")) {
        usage.output_tokens = usage_json["output_tokens"].get<size_t>();
    }
    if (usage_json.contains("total_tokens")) {
        usage.total_tokens = usage_json["total_tokens"].get<size_t>();
    }
    if (usage_json.contains("cached_tokens")) {
        usage.cached_tokens = usage_json["cached_tokens"].get<size_t>();
    }
    if (usage_json.contains("thoughts_tokens")) {
        usage.thoughts_tokens = usage_json["thoughts_tokens"].get<size_t>();
    }

    // total_tokens 是系统监控和告警最常用的稳定字段；如果 provider 没单独给，
    // 先按 input + output 回退，保证上层至少拿到一条可用主口径。
    if (usage.total_tokens == 0) {
        usage.total_tokens = usage.input_tokens + usage.output_tokens;
    }
    return usage;
}
} // namespace

TraceProxyAi::TraceProxyAi(std::string base_url,
                           TraceAiBackend backend,
                           int timeout_ms,
                           std::string prompt_template)
    : timeout_ms_(timeout_ms > 0 ? timeout_ms : 10000),
      prompt_template_(std::move(prompt_template))
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

    if (!response_json.contains("analysis")) {
        throw std::runtime_error("Trace AI Protocol Error: missing 'analysis' field");
    }

    const std::optional<TraceAiUsage> usage = ParseTraceUsage(response_json);

    nlohmann::json analysis_json;
    try {
        if (response_json["analysis"].is_string()) {
            analysis_json = nlohmann::json::parse(response_json["analysis"].get<std::string>());
        } else if (response_json["analysis"].is_object()) {
            analysis_json = response_json["analysis"];
        } else {
            throw std::runtime_error("unsupported analysis payload type");
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Trace AI Protocol Error: invalid 'analysis' payload. " + std::string(e.what()));
    }

    const std::vector<std::string> required_fields = {"summary", "risk_level", "root_cause", "solution"};
    for (const auto& field : required_fields) {
        if (!analysis_json.contains(field)) {
            throw std::runtime_error("Trace AI Validation Error: missing required field '" + field + "'");
        }
    }

    const std::string risk = analysis_json["risk_level"];
    if (risk != "critical" && risk != "warning" && risk != "error" && risk != "info" &&
        risk != "safe" && risk != "unknown") {
        throw std::runtime_error("Trace AI Validation Error: invalid risk_level '" + risk + "'");
    }

    TraceAiResponse response;
    response.analysis = analysis_json.get<LogAnalysisResult>();
    response.usage = usage;
    return response;
}
