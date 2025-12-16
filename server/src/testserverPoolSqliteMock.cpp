#include "http/HttpServer.h"
#include "MiniMuduo/net/EventLoop.h"
#include "http/HttpResponse.h"
#include "threadpool/ThreadPool.h"
#include "core/AnalysisTask.h"
#include "persistence/SqliteLogRepository.h"
#include "ai/AiProvider.h"  // 引入AI抽象接口
#include "ai/MockAI.h" // 引入Gemini AI实现
#include <MiniMuduo/net/TcpConnection.h>
#include <memory> // For std::unique_ptr
#include "notification/WebhookNotifier.h"
#include "persistence/SqliteConfigRepository.h"
class testServer : public HttpServer
{
public:
    testServer(MiniMuduo::net::EventLoop *loop,
               const MiniMuduo::net::InetAddress &listenAddr,
               const int num_io_thread)
        : HttpServer(loop, listenAddr, "testServer")
    {
        setThreadNum(num_io_thread);
    }

private:
};

int main()
{
    std::shared_ptr<SqliteLogRepository> persistence;
    try
    {
        persistence = std::make_shared<SqliteLogRepository>("test.db");
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal Error:Failed to initialize persistence layer" << e.what() << '\n';
        return -1;
    }

    // 创建AI客户端实例
    std::unique_ptr<AiProvider> ai_client = std::make_unique<MockAI>();

    const int num_cpu_cores = std::thread::hardware_concurrency();
    const int num_io_threads = 1; // 明确 I/O 线程数量
    const int num_worker_threads = num_cpu_cores > 1 ? num_cpu_cores - num_io_threads : 1;

    std::cout << "System Info: " << num_cpu_cores << " cores detected." << std::endl;
    std::cout << "Thread Model: " << num_io_threads << " I/O threads, "
              << num_worker_threads << " worker threads." << std::endl;
    MiniMuduo::net::EventLoop loop;
    MiniMuduo::net::InetAddress addr(8080);
    testServer server(&loop, addr, num_io_threads);
    ThreadPool tpool(num_worker_threads,5);
    SqliteConfigRepository config_repo = SqliteConfigRepository("test.db");
    std::vector<std::string> urls = config_repo.getActiveWebhookUrls();
    std::shared_ptr<INotifier> notifier = std::make_shared<WebhookNotifier>(urls);
    // 使用值捕获persistence，增加引用计数防止意外发生
    auto onRequest = [&tpool, persistence, &ai_client, notifier](HttpRequest &req, HttpResponse *resp, const MiniMuduo::net::TcpConnectionPtr &conn)
    {
        if (req.method() == "OPTIONS")
        {
            resp->setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
            // 允许任何来源
            resp->setHeader("Access-Control-Allow-Origin", "*");
            // 允许的方法
            resp->setHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
            // 允许的头部 (关键！因为你发的是 application/json)
            resp->setHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
            // 缓存预检结果，减少请求次数 (比如 1 天)
            resp->setHeader("Access-Control-Max-Age", "86400");

            // 直接发送响应，不需要进入后面的逻辑
            MiniMuduo::net::Buffer buf;
            resp->appendToBuffer(&buf);
            conn->send(std::move(buf));
            return;
        }
        if (req.method() == "POST" && req.path() == "/logs")
        {
            AnalysisTask task;
            task.trace_id = std::move(req.trace_id);
            task.raw_request_body = std::move(req.body_);
            auto work = [task, persistence, &ai_client, notifier]
            {
                try
                {
                    // Step 1: 保存原始日志
                    persistence->SaveRawLog(task.trace_id, task.raw_request_body);

                    // Step 2: 调用 AI
                    std::string ai_result = ai_client->analyze(task.raw_request_body);

                    // Step 3: 保存结果
                    // 只要 analyze 没抛异常，我们就认为拿到了合法的 JSON (在 GeminiApiAi 内部做过校验)
                    persistence->saveAnalysisResult(task.trace_id, ai_result, "SUCCESS");

                    // Step 4: Webhook 通知
                    // 这里再 parse 一次虽有开销，但解耦了逻辑，MVP 可以接受
                    notifier->notify(task.trace_id, nlohmann::json::parse(ai_result));
                }
                catch (const std::exception &e)
                {
                    std::cerr << "[Worker Error] TraceID: " << task.trace_id << " | " << e.what() << '\n';
                    // 记录失败状态
                    persistence->saveAnalysisResult(task.trace_id, std::string("Error: ") + e.what(), "FAILURE");
                }
            };
            if (tpool.submit(std::move(work)))
            {
                resp->setStatusCode(HttpResponse::HttpStatusCode::k202Acceptd);
                resp->setHeader("Content-Type", "application/json");
                resp->setHeader("Access-Control-Allow-Origin", "*");
                resp->setHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
                resp->setHeader("Access-Control-Allow-Headers", "Content-Type");
                resp->body_ = "{\"trace_id\": \"" + task.trace_id + "\"}";
            }
            else
            {
                resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
                resp->setHeader("Access-Control-Allow-Origin", "*");
                resp->setHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
                resp->setHeader("Access-Control-Allow-Headers", "Content-Type");
                resp->body_ = "{\"error\": \"Server is overloaded\"}";
            }
        }
        else if (req.method() == "GET" && req.path().rfind("/results/", 0) == 0)
        {
            std::string trace_id = req.path().substr(9);

            // 使用 weak_ptr 防止 conn 提前销毁
            std::weak_ptr<MiniMuduo::net::TcpConnection> weakConn = conn;
            auto loop = conn->getLoop();

            auto querywork = [trace_id, persistence, weakConn, loop]()
            {
                // --- 这部分在 Worker 线程 ---
                auto result = persistence->queryResultByTraceId(trace_id);

                // --- 把响应操作“派发”回 IO 线程 ---
                loop->queueInLoop([weakConn, trace_id, result]()
                                  {
            // --- 这部分在 IO 线程 ---
            auto conn = weakConn.lock();
            if (conn) { // 检查连接是否还存活
                HttpResponse resp;
                if (result) { // result 是 optional<string>
                    resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
                    resp.setHeader("Content-Type", "application/json");
                    resp.setHeader("Access-Control-Allow-Origin", "*");
                    // 注意：JSON 字符串中的引号需要转义
                    resp.body_ = "{\"trace_id\": \"" + trace_id + "\", \"result\": " + *result + "}";
                } else {
                    resp.setStatusCode(HttpResponse::HttpStatusCode::k404NotFound);
                    resp.setHeader("Access-Control-Allow-Origin", "*");
                    resp.body_ = "{\"error\": \"Trace ID not found\"}";
                }

                MiniMuduo::net::Buffer buf;
                resp.appendToBuffer(&buf);
                conn->send(std::move(buf));
            } });
            };

            if (tpool.submit(std::move(querywork)))
            {
                resp->isHandledAsync = true;
                // 对于 GET 请求，我们不立即返回任何东西，因为响应是异步的
                // 如果想给客户端一个即时反馈，可以考虑接受请求后立即返回一个空的 202，但这不常用
            }
            else
            {
                resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
                resp->body_ = "{\"error\": \"Server is overloaded\"}";

                // 只有在这种同步失败的情况下，才需要在这里发送响应
                MiniMuduo::net::Buffer buf;
                resp->appendToBuffer(&buf);
                conn->send(std::move(buf));
            }
        }

        // 按值捕获，意味着request返回了，task还存在，不能图那一点点速度，导致task执行时已经变成空悬引用
    };
    server.setHttpCallback(onRequest);
    server.start();
    loop.loop();
    return 0;
}
