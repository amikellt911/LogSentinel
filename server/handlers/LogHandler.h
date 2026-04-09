#pragma once
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include <MiniMuduo/net/TcpConnection.h>
#include <string>
#include <unordered_set>
#include <vector>

class TraceSessionManager;
class SystemRuntimeAccumulator;

class LogHandler {
public:
    // LogHandler 现在彻底收口成 `/logs/spans` 专用入口。
    // 既然旧 `/logs` 和 `/results/*` 已经从主链和构造链一起移除，
    // 这里就不再保留 repo/batcher/tpool 这些旧依赖，避免废弃接口继续把旧类型拖在主线里。
    // system_runtime_accumulator 仍保持可选：
    // 它只负责系统监控埋点，不参与 Trace 主链语义判断。
    // trace_end 主字段和别名现在已经收口成冷启动配置：
    // 既然它们决定的是“请求字段该怎么解释”，那就不值得在运行中热切；这里直接在构造时注入，
    // 后面每个请求都复用同一份口径，避免继续为了这两个字段在热路径读配置快照。
    explicit LogHandler(TraceSessionManager* trace_session_manager = nullptr,
               SystemRuntimeAccumulator* system_runtime_accumulator = nullptr,
               std::string trace_end_field = "trace_end",
               std::vector<std::string> trace_end_aliases = {});

    void handleTracePost(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);

private:
    TraceSessionManager* trace_session_manager_ = nullptr;
    SystemRuntimeAccumulator* system_runtime_accumulator_ = nullptr;
    // 结束字段口径在构造时固定下来，后续请求直接复用，避免再为这两个冷启动字段做按请求快照读取。
    std::string trace_end_field_;
    std::vector<std::string> trace_end_aliases_;
    // 顶层未知字段收集每次请求都会用到这组字段名；既然主字段和别名已经是冷启动配置，
    // 那就一并在构造时预展开成 set，避免每个请求重复插入。
    std::unordered_set<std::string> trace_end_known_fields_;
};
