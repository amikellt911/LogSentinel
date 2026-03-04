#pragma once

#include "ai/TraceAiBackend.h"
#include <memory>
#include <string>

class TraceAiProvider;

struct TraceAiFactoryOptions
{
    std::string base_url = "http://127.0.0.1:8001";
    TraceAiBackend backend = TraceAiBackend::Mock;
    int timeout_ms = 10000;
};

// 工厂职责：根据配置创建 TraceAiProvider，避免 main.cpp 堆叠选择逻辑。
std::shared_ptr<TraceAiProvider> CreateTraceAiProvider(const TraceAiFactoryOptions& options);
