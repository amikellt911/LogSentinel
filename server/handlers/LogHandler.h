#pragma once
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include <MiniMuduo/net/TcpConnection.h>
#include <memory>

// 前向声明，减少头文件依赖
class ThreadPool;
class SqliteLogRepository;
class SqliteConfigRepository;
class LogBatcher;
class TraceSessionManager;
class SystemRuntimeAccumulator;

class LogHandler {
public:
    // 构造函数：一次性注入所有依赖。
    // system_runtime_accumulator 当前只负责系统监控埋点，不参与 Trace 主链语义判断；
    // 所以这里保持可选，方便逐步把主链统计接进来，而不是一次把 handler/router 全部改穿。
    // config_repo 这一刀只给 /logs/spans 解析层读热配置用：
    // 既然 trace_end 主字段和别名要求热更新，那么这里不能在构造时把字段名写死，
    // 而是要在每次请求开始时拿一份快照，再用这一份快照完成整次请求的字段标准化。
    explicit LogHandler(ThreadPool* tpool, 
               std::shared_ptr<SqliteLogRepository> repo,
               std::shared_ptr<LogBatcher> batcher,
               TraceSessionManager* trace_session_manager = nullptr,
               SystemRuntimeAccumulator* system_runtime_accumulator = nullptr,
               std::shared_ptr<SqliteConfigRepository> config_repo = nullptr);
    // 具体的业务处理函数
    void handlePost(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);
    void handleTracePost(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);
    void handleGetResult(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);

private:
    ThreadPool* tpool_;
    std::shared_ptr<LogBatcher> batcher_;
    std::shared_ptr<SqliteLogRepository> repo_;
    std::shared_ptr<SqliteConfigRepository> config_repo_;
    TraceSessionManager* trace_session_manager_ = nullptr;
    SystemRuntimeAccumulator* system_runtime_accumulator_ = nullptr;
};
