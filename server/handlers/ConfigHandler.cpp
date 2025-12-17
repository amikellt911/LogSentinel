#include "handlers/ConfigHandler.h"
#include "persistence/SqliteConfigRepository.h"
#include "threadpool/ThreadPool.h"
#include "MiniMuduo/net/EventLoop.h"
#include <nlohmann/json.hpp>
#include <iostream>

using json = nlohmann::json;

ConfigHandler::ConfigHandler(std::shared_ptr<SqliteConfigRepository> repo, ThreadPool *tpool)
    : repo_(repo), tpool_(tpool)
{
}

void ConfigHandler::handleGetSettings(const HttpRequest &req, HttpResponse *resp, const MiniMuduo::net::TcpConnectionPtr &conn)
{
    std::weak_ptr<MiniMuduo::net::TcpConnection> weakConn(conn);
    // 异步任务：获取所有配置
    auto work = [repo = repo_, weakConn]()
    {
        try
        {
            AllSettings settings = repo->getAllSettings();
            nlohmann::json j = settings;
            std::string body_str = j.dump();
            // 回到 IO 线程发送响应
            if (auto conn = weakConn.lock(); conn)
            {
                auto loop = conn->getLoop();
                loop->queueInLoop([weakConn, body = std::move(body_str)]()
                                  {
                    if(auto conn = weakConn.lock(); conn){
                        HttpResponse resp;
                        resp.setBody(std::move(body));
                        resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
                        resp.addCorsHeaders();
                        resp.setHeader("Content-Type", "application/json");
                        MiniMuduo::net::Buffer buf;
                        resp.appendToBuffer(&buf);
                        conn->send(std::move(buf));
                    } });
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Worker Error] getAllConfig: " << e.what() << '\n';
            if (auto conn = weakConn.lock())
            {
                conn->getLoop()->queueInLoop([weakConn]()
                                             {
                    auto conn = weakConn.lock();
                    if(conn) {
                        HttpResponse errResp;
                        errResp.setStatusCode(HttpResponse::HttpStatusCode::k500InternalServerError);
                        errResp.addCorsHeaders();
                        errResp.setBody("{\"error\": \"Internal Config DB Error\"}");
                        MiniMuduo::net::Buffer buf;
                        errResp.appendToBuffer(&buf);
                        conn->send(std::move(buf));
                    } });
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
        resp->body_ = "{\"error\": \"Server is overloaded\"}";
    }
}

void ConfigHandler::handleUpdateAppConfig(const HttpRequest &req, HttpResponse *resp, const MiniMuduo::net::TcpConnectionPtr &conn)
{
    std::weak_ptr<MiniMuduo::net::TcpConnection> weakConn(conn);
    std::string requestBody = req.body_;

    auto work = [repo = repo_, weakConn, requestBody]()
    {
        try
        {
            // 解析 JSON
            auto j = json::parse(requestBody);
            std::map<std::string, std::string> updates;
            if (!j.contains("items") || !j["items"].is_array())
            {
                throw std::invalid_argument("Error: JSON format invalid, 'items' array missing.");
            }

            // 将 JSON 对象打平成 map<string, string>
            // Repository 接口目前接受 map<string,string>，所以需要做类型转换
            for (const auto &item : j["items"])
            {
                if (item.contains("key") && item.contains("value"))
                {
                    // 1. Key 肯定是字符串，可以直接取
                    std::string key = item["key"].get<std::string>();

                    // 2. Value 不要急着 get<string>，先拿引用！
                    // const auto& 此时类型依然是 nlohmann::json
                    const auto &json_val = item["value"];

                    // 3. 现在可以对着 json 对象问类型了
                    if (json_val.is_string())
                    {
                        // 如果是字符串，直接取
                        updates[key] = json_val.get<std::string>();
                    }
                    else if (json_val.is_boolean())
                    {
                        // 如果是布尔，取出来转 "0"/"1"
                        updates[key] = json_val.get<bool>() ? "1" : "0";
                    }
                    else if (json_val.is_number())
                    {
                        // 如果是数字，统一转 string
                        if (json_val.is_number_float())
                        {
                            updates[key] = std::to_string(json_val.get<double>());
                        }
                        else
                        {
                            updates[key] = std::to_string(json_val.get<long long>());
                        }
                    }
                    else if (json_val.is_null())
                    {
                        updates[key] = ""; // 或者其他处理
                    }
                    else
                    {
                        // 数组或对象等复杂类型，直接序列化成字符串存进去
                        updates[key] = json_val.dump();
                    }
                }
            }

            repo->handleUpdateAppConfig(updates);

            if (auto conn = weakConn.lock(); conn)
            {
                auto loop = conn->getLoop();
                loop->queueInLoop([weakConn]()
                                  {
                    if(auto conn = weakConn.lock(); conn){
                        HttpResponse resp;
                        resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
                        resp.addCorsHeaders();
                        resp.setBody("{\"status\": \"success\"}");
                        MiniMuduo::net::Buffer buf;
                        resp.appendToBuffer(&buf);
                        conn->send(std::move(buf));
                    } });
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Worker Error] updateAppConfig: " << e.what() << '\n';
            if (auto conn = weakConn.lock())
            {
                conn->getLoop()->queueInLoop([weakConn, msg = std::string(e.what())]()
                                             {
                    auto conn = weakConn.lock();
                    if(conn) {
                        HttpResponse errResp;
                        errResp.setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
                        errResp.addCorsHeaders();
                        errResp.setBody("{\"error\": \"" + msg + "\"}");
                        MiniMuduo::net::Buffer buf;
                        errResp.appendToBuffer(&buf);
                        conn->send(std::move(buf));
                    } });
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
        resp->body_ = "{\"error\": \"Server overloaded\"}";
    }
}

void ConfigHandler::handleUpdatePrompts(const HttpRequest &req, HttpResponse *resp, const MiniMuduo::net::TcpConnectionPtr &conn)
{
    std::weak_ptr<MiniMuduo::net::TcpConnection> weakConn(conn);
    std::string requestBody = req.body_;

    auto work = [repo = repo_, weakConn, requestBody]()
    {
        try
        {
            json j = json::parse(requestBody);
            std::vector<PromptConfig> prompts = j.get<std::vector<PromptConfig>>();

            repo->handleUpdatePrompt(prompts);

            if (auto conn = weakConn.lock(); conn)
            {
                auto loop = conn->getLoop();
                loop->queueInLoop([weakConn]()
                                  {
                    if(auto conn = weakConn.lock(); conn){
                        HttpResponse resp;
                        resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
                        resp.addCorsHeaders();
                        resp.setBody("{\"status\": \"success\"}");
                        MiniMuduo::net::Buffer buf;
                        resp.appendToBuffer(&buf);
                        conn->send(std::move(buf));
                    } });
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Worker Error] updatePrompts: " << e.what() << '\n';
            if (auto conn = weakConn.lock())
            {
                conn->getLoop()->queueInLoop([weakConn]()
                                             {
                    auto conn = weakConn.lock();
                    if(conn) {
                        HttpResponse errResp;
                        errResp.setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
                        errResp.addCorsHeaders();
                        errResp.setBody("{\"error\": \"Invalid JSON or Data\"}");
                        MiniMuduo::net::Buffer buf;
                        errResp.appendToBuffer(&buf);
                        conn->send(std::move(buf));
                    } });
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
        resp->body_ = "{\"error\": \"Server overloaded\"}";
    }
}

void ConfigHandler::handleUpdateChannels(const HttpRequest &req, HttpResponse *resp, const MiniMuduo::net::TcpConnectionPtr &conn)
{
    std::weak_ptr<MiniMuduo::net::TcpConnection> weakConn(conn);
    std::string requestBody = req.body_;

    auto work = [repo = repo_, weakConn, requestBody]()
    {
        try
        {
            json j = json::parse(requestBody);
            std::vector<AlertChannel> channels = j.get<std::vector<AlertChannel>>();

            repo->handleUpdateChannel(channels);

            if (auto conn = weakConn.lock(); conn)
            {
                auto loop = conn->getLoop();
                loop->queueInLoop([weakConn]()
                                  {
                    if(auto conn = weakConn.lock(); conn){
                        HttpResponse resp;
                        resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
                        resp.addCorsHeaders();
                        resp.setBody("{\"status\": \"success\"}");
                        MiniMuduo::net::Buffer buf;
                        resp.appendToBuffer(&buf);
                        conn->send(std::move(buf));
                    } });
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Worker Error] updateChannels: " << e.what() << '\n';
            if (auto conn = weakConn.lock())
            {
                conn->getLoop()->queueInLoop([weakConn]()
                                             {
                    auto conn = weakConn.lock();
                    if(conn) {
                        HttpResponse errResp;
                        errResp.setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
                        errResp.addCorsHeaders();
                        errResp.setBody("{\"error\": \"Invalid JSON or Data\"}");
                        MiniMuduo::net::Buffer buf;
                        errResp.appendToBuffer(&buf);
                        conn->send(std::move(buf));
                    } });
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
        resp->body_ = "{\"error\": \"Server overloaded\"}";
    }
}
