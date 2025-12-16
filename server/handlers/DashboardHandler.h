#pragma once
#include<memory>
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include <MiniMuduo/net/TcpConnection.h>
class SqliteLogRepository;
class ThreadPool;
class DashboardHandler {
public:
    // 仪表盘只需要读数据库，不需要 AI 和 Notifier，所以构造函数很简单
    explicit DashboardHandler(std::shared_ptr<SqliteLogRepository> repo,ThreadPool* tpool);

    void handleGetStats(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);

private:
    std::shared_ptr<SqliteLogRepository> repo_;
    ThreadPool* tpool_;
};