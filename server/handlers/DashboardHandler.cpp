#include "handlers/DashboardHandler.h"
#include "persistence/SqliteLogRepository.h"
#include "threadpool/ThreadPool.h"
#include "MiniMuduo/net/EventLoop.h"
DashboardHandler::DashboardHandler(std::shared_ptr<SqliteLogRepository> repo, ThreadPool *tpool)
    : repo_(repo),
      tpool_(tpool)
{
}

void DashboardHandler::handleGetStats(const HttpRequest &req, HttpResponse *resp, const MiniMuduo::net::TcpConnectionPtr &conn)
{
    std::weak_ptr<MiniMuduo::net::TcpConnection> weakConn = conn;
    
    auto work = [repo = repo_, weakConn]()
    {
        try 
        {
            DashboardStats stats = repo->getDashboardStats();
            nlohmann::json j = stats; // 序列化
            std::string jsonStr = j.dump();

            if (auto conn = weakConn.lock())
            {
                auto loop = conn->getLoop();
                // 这里的捕获列表要把 jsonStr 移动进去
                loop->queueInLoop([weakConn, jsonStr = std::move(jsonStr)]()
                {
                    // IO 线程只负责发送,getDashboardStats只能在Worker 线程
                    auto conn = weakConn.lock();
                    if (conn)
                    {
                        HttpResponse response;
                        response.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
                        response.addCorsHeaders(); // 记得加 CORS
                        response.setHeader("Content-Type", "application/json");
                        response.setBody(jsonStr);

                        MiniMuduo::net::Buffer buf;
                        response.appendToBuffer(&buf);
                        conn->send(std::move(buf));
                    }
                });
            }
        }
        catch (const std::exception &e)
        {
            // 错误处理也要回 IO 线程发
            std::cerr << "[Worker Error] getStats: " << e.what() << '\n';
            if (auto conn = weakConn.lock()) {
                conn->getLoop()->queueInLoop([weakConn](){
                    auto conn = weakConn.lock();
                    if(conn) {
                        HttpResponse errResp;
                        errResp.setStatusCode(HttpResponse::HttpStatusCode::k500InternalServerError);
                        errResp.addCorsHeaders();
                        errResp.setBody("{\"error\": \"Internal DB Error\"}");
                        MiniMuduo::net::Buffer buf;
                        errResp.appendToBuffer(&buf);
                        conn->send(std::move(buf));
                    }
                });
            }
        }
    };

    if (tpool_->submit(std::move(work)))
    {
        resp->isHandledAsync = true;
    }
    else
    {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
        resp->addCorsHeaders();
        resp->body_ = "{\"error\": \"Get Stats Server is overloaded\"}";
    }
}
