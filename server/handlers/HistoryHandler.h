#pragma once
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include <MiniMuduo/net/TcpConnection.h>
#include <memory>

// 前向声明，减少头文件依赖
class SqliteLogRepository;
class ThreadPool;

class HistoryHandler {
public:
    // 构造函数：需要数据库和线程池来异步查询
    explicit HistoryHandler(std::shared_ptr<SqliteLogRepository> repo, ThreadPool* tpool);

    /**
     * @brief 处理 GET /history 请求，接受分页参数
     * * @param req HttpRequest，包含 page, pageSize 等查询参数
     * @param resp HttpResponse，用于设置异步响应标志
     * @param conn TcpConnectionPtr，用于派发异步响应回 IO 线程
     */
    void handleGetHistory(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);

private:
    std::shared_ptr<SqliteLogRepository> repo_;
    ThreadPool* tpool_;
};