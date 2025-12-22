#include "handlers/LogHandler.h"
#include "threadpool/ThreadPool.h"
#include "persistence/SqliteLogRepository.h"
#include "persistence/SqliteConfigRepository.h"
#include "ai/AiProvider.h"
#include "notification/INotifier.h"
#include "core/AnalysisTask.h"
#include <nlohmann/json.hpp>
#include "LogHandler.h"
#include "core/LogBatcher.h"
#include "MiniMuduo/net/EventLoop.h"

// // 辅助函数：统一设置 CORS 头
// static void addCorsHeaders(HttpResponse* resp) {
//     resp->setHeader("Access-Control-Allow-Origin", "*");
//     resp->setHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
//     resp->setHeader("Access-Control-Allow-Headers", "Content-Type");
// }

LogHandler::LogHandler(ThreadPool *tpool, std::shared_ptr<SqliteLogRepository> repo, std::shared_ptr<LogBatcher> batcher, std::shared_ptr<SqliteConfigRepository> config_repo)
    :tpool_(tpool),
    repo_(repo),
    batcher_(batcher),
    config_repo_(config_repo)
{
}

void LogHandler::handlePost(const HttpRequest &req, HttpResponse *resp, const MiniMuduo::net::TcpConnectionPtr &conn)
{
    AnalysisTask task;
    // 没用，因为const(为了通用性)，会降级为拷贝
    //  task.trace_id = std::move(req.trace_id);
    //  task.raw_request_body = std::move(req.body_);
    task.trace_id = req.trace_id;
    task.raw_request_body = req.body_;
    task.start_time = std::chrono::steady_clock::now();

    // 1. Get Config Snapshot (O(1))
    auto snap = config_repo_->getSnapshot();

    // 2. Populate Task with cached values
    task.ai_api_key = snap->app_config.ai_api_key;
    task.ai_model = snap->app_config.ai_model;
    task.map_prompt = snap->active_map_prompt;
    task.reduce_prompt = snap->active_reduce_prompt;

    if (batcher_->push(std::move(task)))
    {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k202Acceptd);
        resp->setHeader("Content-Type", "application/json");
        resp->addCorsHeaders();
        nlohmann::json j;
        j["trace_id"] = req.trace_id;
        resp->body_ = j.dump();
    }
    else
    {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
        resp->addCorsHeaders();
        resp->body_ = "{\"error\": \"Server is overloaded\"}";
    }
}
void LogHandler::handleGetResult(const HttpRequest &req, HttpResponse *resp, const MiniMuduo::net::TcpConnectionPtr &conn)
{
    std::string trace_id = req.path().substr(9);

    // 使用 weak_ptr 防止 conn 提前销毁
    std::weak_ptr<MiniMuduo::net::TcpConnection> weakConn = conn;
    auto querywork = [trace_id, repo = repo_, weakConn]()
    {
        // --- 这部分在 Worker 线程 ---
        auto result = repo->queryStructResultByTraceId(trace_id);
        auto conn = weakConn.lock();
        if (conn)
        {
            auto loop = conn->getLoop();
            loop->queueInLoop([weakConn, trace_id, result]()
                              {
            // --- 这部分在 IO 线程 ---
            auto conn = weakConn.lock();
            if (conn) { // 检查连接是否还存活
                HttpResponse resp;
                resp.addCorsHeaders();
                if (result) { // result 是 optional<string>
                    resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
                    resp.setHeader("Content-Type", "application/json");
                        // 注意：JSON 字符串中的引号需要转义
                        // 3. 修正：使用 json 库拼接，而不是手动拼字符串
                        // 注意：result 已经是 json 字符串了，不要重复序列化，
                        // 我们需要构造 {"trace_id": "...", "result": JSON_OBJECT}
                        // 这里的 result.value() 是一个字符串形式的 JSON 对象
                        // 为了避免转义问题，我们手动拼这一块是合理的，但要确保 result 本身是合法 JSON
                        // 或者更安全的做法：
                        // resp.body_ = "{\"trace_id\": \"" + trace_id + "\", \"result\": " + *result + "}"; 
                        // 鉴于 *result 来自数据库且原本就是合法 JSON，你原来的写法是可以接受的。
                        // 如果想更稳妥，可以这样：
                        try {
                             nlohmann::json root;
                             root["trace_id"] = trace_id;
                             root["result"] = *result; // 解析后再放入，保证嵌套正确
                             resp.body_ = root.dump();
                        } catch (...) {
                             // 极端情况：数据库里的 json 坏了
                             resp.body_ = "{\"trace_id\": \"" + trace_id + "\", \"result\": null, \"error\": \"Data corrupted\"}";
                        }
                } else {
                    resp.setStatusCode(HttpResponse::HttpStatusCode::k404NotFound);
                    resp.body_ = "{\"error\": \"Trace ID not found\"}";
                }

                MiniMuduo::net::Buffer buf;
                resp.appendToBuffer(&buf);
                conn->send(std::move(buf));
            } });
        }
    };

    if (tpool_->submit(std::move(querywork)))
    {
        resp->isHandledAsync = true;
        // 对于 GET 请求，我们不立即返回任何东西，因为响应是异步的
        // 如果想给客户端一个即时反馈，可以考虑接受请求后立即返回一个空的 202，但这不常用
    }
    else
    {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
        resp->addCorsHeaders();
        resp->body_ = "{\"error\": \"Server is overloaded\"}";

        // 只有在这种同步失败的情况下，才需要在这里发送响应
        // MiniMuduo::net::Buffer buf;
        // resp->appendToBuffer(&buf);
        // isHandledAsync = false会自动发送
        // conn->send(std::move(buf));
    }
}
