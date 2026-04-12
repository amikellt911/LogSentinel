#pragma once

#include <algorithm>
#include <cctype>
#include <string>

// Trace AI 后端类型：当前支持 mock / gemini / glm。
enum class TraceAiBackend
{
    Mock,
    Gemini,
    Glm,
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
    return false;
}
