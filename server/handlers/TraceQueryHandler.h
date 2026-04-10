#pragma once

#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include <MiniMuduo/net/TcpConnection.h>
#include <memory>

class ThreadPool;
class SqliteTraceRepository;

// TraceQueryHandler 只负责 Trace 读侧 HTTP 入口：
// 1) 解析请求参数
// 2) 把阻塞查询丢到 query_tpool
// 3) 再切回 IO 线程发响应
// 它不碰 TraceSessionManager，也不负责写路径和 AI 调度。
class TraceQueryHandler
{
public:
    explicit TraceQueryHandler(std::shared_ptr<SqliteTraceRepository> repo,
                               ThreadPool* query_tpool);

    void handleSearchTraces(const HttpRequest& req,
                            HttpResponse* resp,
                            const MiniMuduo::net::TcpConnectionPtr& conn);

    void handleGetTraceDetail(const HttpRequest& req,
                              HttpResponse* resp,
                              const MiniMuduo::net::TcpConnectionPtr& conn);

private:
    std::shared_ptr<SqliteTraceRepository> repo_;
    ThreadPool* query_tpool_ = nullptr;
};
