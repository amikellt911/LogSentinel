#include "handlers/ConfigHandler.h"
#include "persistence/SqliteConfigRepository.h"
#include "threadpool/ThreadPool.h"
#include "MiniMuduo/net/EventLoop.h"
#include "ConfigHandler.h"

ConfigHandler::ConfigHandler(std::shared_ptr<SqliteConfigRepository> repo, ThreadPool *tpool)
    : repo_(repo), tpool_(tpool)
{
}

void ConfigHandler::handleGetSettings(const HttpRequest &req, HttpResponse *resp, const MiniMuduo::net::TcpConnectionPtr &conn)
{
    std::weak_ptr<MiniMuduo::net::TcpConnection> weakConn(conn);
    auto work = [repo = repo_, weakConn]()
    {
        try
        {
            AllSettings settings = repo->getAllSettings();
            nlohmann::json j = settings;
            std::string body_str = j.dump();
            if(auto conn = weakConn.lock(); conn){
                auto loop=conn->getLoop();
                loop->queueInLoop([weakConn,body=std::move(body_str)](){
                    if(auto conn = weakConn.lock(); conn){
                        HttpResponse resp;
                        resp.setBody(std::move(body));
                        resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
                        resp.addCorsHeaders();
                        resp.setHeader("Content-Type", "application/json");
                        MiniMuduo::net::Buffer buf;
                        resp.appendToBuffer(&buf);
                        conn->send(std::move(buf));
                    }
                });
            }
        }
        catch (const std::exception &e)
        {
            // 错误处理也要回 IO 线程发
            std::cerr << "[Worker Error] getAllConfig: " << e.what() << '\n';
            if (auto conn = weakConn.lock()) {
                conn->getLoop()->queueInLoop([weakConn](){
                    auto conn = weakConn.lock();
                    if(conn) {
                        HttpResponse errResp;
                        errResp.setStatusCode(HttpResponse::HttpStatusCode::k500InternalServerError);
                        errResp.addCorsHeaders();
                        errResp.setBody("{\"error\": \"Internal Config DB Error\"}");
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
        resp->body_ = "{\"error\": \"Get AllConfig Server is overloaded\"}";
    }
}



