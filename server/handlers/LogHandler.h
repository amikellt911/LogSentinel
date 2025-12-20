#pragma once
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include <MiniMuduo/net/TcpConnection.h>
#include <memory>

// 前向声明，减少头文件依赖
class ThreadPool;
class SqliteLogRepository;
class SqliteConfigRepository;
class AiProvider;
class INotifier;
class LogBatcher;
class LogHandler {
public:
    // 构造函数：一次性注入所有依赖
    explicit LogHandler(ThreadPool* tpool, 
               std::shared_ptr<SqliteLogRepository> repo,
               std::shared_ptr<LogBatcher> batcher,
               std::shared_ptr<SqliteConfigRepository> config_repo);
    // 具体的业务处理函数
    void handlePost(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);
    void handleGetResult(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);

private:
    ThreadPool* tpool_;
    std::shared_ptr<LogBatcher> batcher_;
    std::shared_ptr<SqliteLogRepository> repo_;
    std::shared_ptr<SqliteConfigRepository> config_repo_;
};
