#pragma once

#include <algorithm>
#include <cctype>
#include <string>

// Trace AI 后端类型：只描述 C++ 到 Python proxy 的路由段。
// 具体厂商 HTTP 协议、鉴权和返回体差异都留在 proxy provider 内处理。
enum class TraceAiBackend
{
    Mock,
    Gemini,
    Glm,
    DeepSeek,
};

inline std::string TraceAiBackendToRouteSegment(TraceAiBackend backend)
{
    switch (backend) {
        case TraceAiBackend::Mock:
            return "mock";
        case TraceAiBackend::Gemini:
            return "gemini";
        case TraceAiBackend::Glm:
            return "glm";
        case TraceAiBackend::DeepSeek:
            return "deepseek";
    }
    return "mock";
}

inline bool TryParseTraceAiBackend(const std::string& value, TraceAiBackend* out)
{
    if (!out) {
        return false;
    }

    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lower == "mock") {
        *out = TraceAiBackend::Mock;
        return true;
    }
    if (lower == "gemini") {
        *out = TraceAiBackend::Gemini;
        return true;
    }
    if (lower == "glm" || lower == "zhipu") {
        *out = TraceAiBackend::Glm;
        return true;
    }
    if (lower == "deepseek" || lower == "deep-seek") {
        // provider 选择仍是冷启动语义。
        // 这里解析到 DeepSeek 后只会固定路由到 /analyze/trace/deepseek，运行中热更新只覆盖 model/api_key。
        *out = TraceAiBackend::DeepSeek;
        return true;
    }
    return false;
}
