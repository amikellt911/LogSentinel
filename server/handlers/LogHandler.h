#pragma once
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include <MiniMuduo/net/TcpConnection.h>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

// 前向声明，减少头文件依赖
class ThreadPool;
class SqliteLogRepository;
class LogBatcher;
class TraceSessionManager;
class SystemRuntimeAccumulator;

class LogHandler {
public:
    // 构造函数：一次性注入所有依赖。
    // system_runtime_accumulator 当前只负责系统监控埋点，不参与 Trace 主链语义判断；
    // 所以这里保持可选，方便逐步把主链统计接进来，而不是一次把 handler/router 全部改穿。
    // repo/batcher 现在只服务旧 `/logs`、`/results/*` 链路：
    // 既然 main.cpp 已经把主路由收口到 `/logs/spans` 新链，那么这两个旧依赖允许为空；
    // 真有旧接口误调用时，再由 handlePost/handleGetResult 明确返回 503，而不是继续空指针崩溃。
    // trace_end 主字段和别名现在已经收口成冷启动配置：
    // 既然它们决定的是“请求字段该怎么解释”，那就不值得在运行中热切；这里直接在构造时注入，
    // 后面每个请求都复用同一份口径，避免继续为了这两个字段在热路径读配置快照。
    explicit LogHandler(ThreadPool* tpool, 
               std::shared_ptr<SqliteLogRepository> repo,
               std::shared_ptr<LogBatcher> batcher,
               TraceSessionManager* trace_session_manager = nullptr,
               SystemRuntimeAccumulator* system_runtime_accumulator = nullptr,
               std::string trace_end_field = "trace_end",
               std::vector<std::string> trace_end_aliases = {});
    // 具体的业务处理函数
    void handlePost(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);
    void handleTracePost(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);
    void handleGetResult(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);

private:
    ThreadPool* tpool_;
    // 下面这两项只给旧日志分析链留兼容口子；主程序的新 Trace 入口不再依赖它们。
    std::shared_ptr<LogBatcher> batcher_;
    std::shared_ptr<SqliteLogRepository> repo_;
    TraceSessionManager* trace_session_manager_ = nullptr;
    SystemRuntimeAccumulator* system_runtime_accumulator_ = nullptr;
    // 结束字段口径在构造时固定下来，后续请求直接复用，避免再为这两个冷启动字段做按请求快照读取。
    std::string trace_end_field_;
    std::vector<std::string> trace_end_aliases_;
    // 顶层未知字段收集每次请求都会用到这组字段名；既然主字段和别名已经是冷启动配置，
    // 那就一并在构造时预展开成 set，避免每个请求重复插入。
    std::unordered_set<std::string> trace_end_known_fields_;
};
