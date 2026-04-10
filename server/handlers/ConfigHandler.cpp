#include "handlers/ConfigHandler.h"
#include "persistence/SqliteConfigRepository.h"
#include "threadpool/ThreadPool.h"
#include "MiniMuduo/net/EventLoop.h"
#include <nlohmann/json.hpp>
#include <iostream>

using json = nlohmann::json;

namespace {

// config 接口只负责标量配置 patch。
// 一旦这里放对象/数组进去，存储层虽然也能 dump 成字符串，但语义已经脏了：
// 前端以为自己在存结构化配置，Repository 以为自己在存简单 key-value，后面谁都说不清。
std::string ConvertConfigValueToString(const json& value, const std::string& key)
{
    if (value.is_string())
    {
        return value.get<std::string>();
    }
    if (value.is_boolean())
    {
        return value.get<bool>() ? "1" : "0";
    }
    if (value.is_number_integer() || value.is_number_unsigned())
    {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_float())
    {
        return std::to_string(value.get<double>());
    }
    throw std::invalid_argument("Error: config key requires scalar value: " + key);
}

std::map<std::string, std::string> ParseConfigUpdatesPayload(const std::string& request_body)
{
    auto j = json::parse(request_body);
    if (!j.contains("items") || !j["items"].is_array())
    {
        throw std::invalid_argument("Error: JSON format invalid, 'items' array missing.");
    }

    std::map<std::string, std::string> updates;
    for (const auto& item : j["items"])
    {
        if (!item.is_object())
        {
            throw std::invalid_argument("Error: each config item must be an object.");
        }
        if (!item.contains("key") || !item.contains("value"))
        {
            throw std::invalid_argument("Error: each config item must contain 'key' and 'value'.");
        }

        const std::string key = item.at("key").get<std::string>();
        updates[key] = ConvertConfigValueToString(item.at("value"), key);
    }
    return updates;
}

std::vector<AlertChannel> ParseChannelPayload(const std::string& request_body)
{
    auto j = json::parse(request_body);
    if (!j.is_array())
    {
        throw std::invalid_argument("Error: channels payload must be an array.");
    }

    std::vector<AlertChannel> channels;
    channels.reserve(j.size());

    for (size_t i = 0; i < j.size(); ++i)
    {
        const auto& item = j.at(i);
        if (!item.is_object())
        {
            throw std::invalid_argument("Error: each channel item must be an object.");
        }

        if (!item.contains("name") || !item.contains("webhook_url") || !item.contains("alert_threshold"))
        {
            throw std::invalid_argument("Error: channel requires 'name', 'webhook_url' and 'alert_threshold'.");
        }

        channels.push_back(item.get<AlertChannel>());
    }

    return channels;
}

} // namespace

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
            // 这里退回到最小职责：只校验请求形状和标量值，字段契约不在 Handler 再维护第二份。
            // 既然之后只会接新设置页，就没必要在这一层继续背一份重复白名单。
            std::map<std::string, std::string> updates = ParseConfigUpdatesPayload(requestBody);

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
            // channels 同样只保留最小格式校验：必须是数组、元素必须成形。
            // 更细的字段契约后面统一交给新前端和存储层口径，不在这里重复维护。
            std::vector<AlertChannel> channels = ParseChannelPayload(requestBody);

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
