#pragma once

// 该类先作为 Trace 会话聚合的占位入口，便于在不打断现有逻辑的情况下逐步迁移实现，降低一次性重构的风险。
class TraceSessionManager
{
public:
    TraceSessionManager();
    ~TraceSessionManager();
};
