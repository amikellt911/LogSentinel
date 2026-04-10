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
    // Trace 分析的最终 prompt 模板由 C++ 冷启动配置渲染后传进来，
    // 工厂只负责把它跟 backend/base_url 一起交给具体 provider，避免 main.cpp 自己管理太多细节。
    std::string prompt_template;
    // model/api_key 现在也跟 trace prompt 一样走冷启动透传。
    // 这样 trace AI 就不会再出现“语言和 prompt 吃的是 Settings，新模型和新密钥还停留在另一条旧链路”这种语义分裂。
    std::string model;
    std::string api_key;
};

// 工厂职责：根据配置创建 TraceAiProvider，避免 main.cpp 堆叠选择逻辑。
std::shared_ptr<TraceAiProvider> CreateTraceAiProvider(const TraceAiFactoryOptions& options);
