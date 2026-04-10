#pragma once

#include "ai/AiTypes.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

// 这里专门收口 Python proxy -> C++ 的 Trace AI 协议解析。
// 既然 TraceProxyAi 已经只负责 HTTP 传输，那么成功/失败 body 的字段解释也应该集中在这一层，
// 避免 TraceSessionManager 再去理解 ok/code/status/message 这些跨语言协议细节。
inline std::optional<TraceAiUsage> ParseTraceProxyUsageOrThrow(const nlohmann::json& response_json)
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

inline std::string BuildTraceProxyFailureMessage(const nlohmann::json& response_json)
{
    const bool has_error_code = response_json.contains("error_code") && !response_json["error_code"].is_null();
    const bool has_error_status = response_json.contains("error_status") && !response_json["error_status"].is_null();
    const bool has_error_message = response_json.contains("error_message") && !response_json["error_message"].is_null();

    std::ostringstream oss;
    oss << "Trace AI Provider Error";
    if (has_error_code || has_error_status || has_error_message) {
        oss << ": ";
    }

    if (has_error_code || has_error_status) {
        oss << "[";
        bool appended_code = false;
        if (has_error_code) {
            if (response_json["error_code"].is_number_integer()) {
                oss << response_json["error_code"].get<int>();
            } else {
                oss << response_json["error_code"].get<std::string>();
            }
            appended_code = true;
        }
        if (has_error_status) {
            if (appended_code) {
                oss << " ";
            }
            oss << response_json["error_status"].get<std::string>();
        }
        oss << "]";
        if (has_error_message) {
            oss << " ";
        }
    }

    if (has_error_message) {
        oss << response_json["error_message"].get<std::string>();
    } else if (!(has_error_code || has_error_status)) {
        oss << "unknown provider failure";
    }

    return oss.str();
}

inline LogAnalysisResult ParseTraceProxyAnalysisOrThrow(const nlohmann::json& response_json)
{
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

inline TraceAiResponse ParseTraceProxyResponseOrThrow(const nlohmann::json& response_json)
{
    // proxy 新协议会显式给 ok=true/false；旧协议没有 ok 字段时，默认继续按成功载荷兼容。
    // 这样 Python 刚切换协议时，C++ 不会因为一个字段缺失就把所有旧 provider 全打挂。
    if (response_json.contains("ok") && response_json["ok"].is_boolean() &&
        !response_json["ok"].get<bool>()) {
        throw std::runtime_error(BuildTraceProxyFailureMessage(response_json));
    }

    TraceAiResponse response;
    response.analysis = ParseTraceProxyAnalysisOrThrow(response_json);
    response.usage = ParseTraceProxyUsageOrThrow(response_json);
    return response;
}
