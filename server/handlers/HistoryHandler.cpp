#include "handlers/HistoryHandler.h"
#include "persistence/SqliteLogRepository.h"
#include "threadpool/ThreadPool.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <sstream>
#include "MiniMuduo/net/EventLoop.h"

// 辅助函数：简单的 Query 参数解析器
// 将 "/history?page=2&pageSize=20" 解析为一个 map
static std::unordered_map<std::string, std::string> parseQuery(const std::string &path)
{
    std::unordered_map<std::string, std::string> query;
    auto pos = path.find('?');
    if (pos == std::string::npos)
        return query; // 没有参数

    std::string queryStr = path.substr(pos + 1);
    std::stringstream ss(queryStr);
    std::string segment;

    while (std::getline(ss, segment, '&'))
    {
        auto eqPos = segment.find('=');
        if (eqPos != std::string::npos)
        {
            // 可能要trim一下？
            std::string key = segment.substr(0, eqPos);
            std::string val = segment.substr(eqPos + 1);
            query[key] = val;
        }
    }
    return query;
}

HistoryHandler::HistoryHandler(std::shared_ptr<SqliteLogRepository> repo, ThreadPool *tpool)
    : repo_(repo), tpool_(tpool)
{
}

void HistoryHandler::handleGetHistory(const HttpRequest &req, HttpResponse *resp, const MiniMuduo::net::TcpConnectionPtr &conn)
{
    // 1. 解析参数
    auto queryParams = parseQuery(req.path());
    
    // 默认值策略
    int page = 1;
    int pageSize = 10;

    // 尝试从参数中读取
    if (queryParams.count("page")) {
        try {
            page = std::stoi(queryParams["page"]);
        } catch (...) { page = 1; }
    }
    if (queryParams.count("pageSize")) {
        try {
            pageSize = std::stoi(queryParams["pageSize"]);
        } catch (...) { pageSize = 10; }
    }

    // 2. 防御性检查 (Validation)
    if (page < 1) page = 1;
    if (pageSize < 1) pageSize = 10;
    if (pageSize > 100) pageSize = 100; // 硬限制

    // 3. 异步任务封装
    // 使用 weak_ptr 防止连接断开后仍然尝试发送
    std::weak_ptr<MiniMuduo::net::TcpConnection> weakConn = conn;

    auto work = [repo = repo_, weakConn, page, pageSize]()
    {
        try
        {
            // 执行查询 (耗时操作)
            HistoryPage result = repo->getHistoricalLogs(page, pageSize);
            
            // 序列化 JSON
            nlohmann::json j = result;
            std::string responseBody = j.dump();

            // 派发回 I/O 线程
            auto conn = weakConn.lock();
            if (conn)
            {
                conn->getLoop()->queueInLoop([conn, body = std::move(responseBody)]()
                {
                    HttpResponse response;
                    response.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
                    response.setHeader("Content-Type", "application/json");
                    response.addCorsHeaders();
                    response.setBody(body); // 即使是空列表也是 200 OK + []

                    MiniMuduo::net::Buffer buf;
                    response.appendToBuffer(&buf);
                    conn->send(std::move(buf));
                });
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "[HistoryHandler] Error: " << e.what() << std::endl;
            // 发生异常时，也应该通知前端 (500)
            auto conn = weakConn.lock();
            if (conn) {
                conn->getLoop()->queueInLoop([conn]() {
                    HttpResponse errResp;
                    errResp.setStatusCode(HttpResponse::HttpStatusCode::k500InternalServerError);
                    errResp.addCorsHeaders();
                    errResp.setBody("{\"error\": \"Internal Database Error\"}");
                    MiniMuduo::net::Buffer buf;
                    errResp.appendToBuffer(&buf);
                    conn->send(std::move(buf));
                });
            }
        }
    };

    // 4. 提交任务
    if (tpool_->submit(std::move(work)))
    {
        resp->isHandledAsync = true; // 告诉 HttpServer 不要立即发送响应
    }
    else
    {
        // 线程池满，直接同步返回 503
        // 不需要手动 conn->send，因为 HttpServer 会处理非 Async 的 resp
        resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
        resp->setBody("{\"error\": \"Server Busy\"}");
        resp->addCorsHeaders();
    }
}