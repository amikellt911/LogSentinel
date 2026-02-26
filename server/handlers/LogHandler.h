#pragma once
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include <MiniMuduo/net/TcpConnection.h>
#include <memory>

// 前向声明，减少头文件依赖
class ThreadPool;
class SqliteLogRepository;
class LogBatcher;
class TraceSessionManager;

class LogHandler {
public:
    // 构造函数：一次性注入所有依赖
    explicit LogHandler(ThreadPool* tpool, 
               std::shared_ptr<SqliteLogRepository> repo,
               std::shared_ptr<LogBatcher> batcher,
               TraceSessionManager* trace_session_manager = nullptr);
    // 具体的业务处理函数
    void handlePost(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);
    void handleTracePost(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);
    void handleGetResult(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);

private:
    ThreadPool* tpool_;
    std::shared_ptr<LogBatcher> batcher_;
    std::shared_ptr<SqliteLogRepository> repo_;
    TraceSessionManager* trace_session_manager_ = nullptr;
};
