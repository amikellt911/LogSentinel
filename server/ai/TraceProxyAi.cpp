#include "ai/TraceProxyAi.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

TraceProxyAi::TraceProxyAi(std::string base_url, TraceAiBackend backend, int timeout_ms)
    : timeout_ms_(timeout_ms > 0 ? timeout_ms : 10000)
{
    if (!base_url.empty() && base_url.back() == '/') {
        base_url.pop_back();
    }
    analyze_trace_url_ = base_url + "/analyze/trace/" + TraceAiBackendToRouteSegment(backend);
}

TraceProxyAi::~TraceProxyAi() = default;

LogAnalysisResult TraceProxyAi::AnalyzeTrace(const std::string& trace_payload)
{
    cpr::Session session;
    session.SetHeader(cpr::Header{{"Content-Type", "text/plain"}});
    session.SetTimeout(cpr::Timeout{timeout_ms_});
    session.SetUrl(cpr::Url{analyze_trace_url_});
    session.SetBody(cpr::Body{trace_payload});

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

    return analysis_json.get<LogAnalysisResult>();
}
