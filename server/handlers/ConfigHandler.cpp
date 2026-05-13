#include "handlers/ConfigHandler.h"
#include "persistence/SqliteConfigRepository.h"
#include "threadpool/ThreadPool.h"
#include "MiniMuduo/net/EventLoop.h"
#include "notification/WebhookNotifier.h"
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

std::vector<ProviderProfile> ParseProviderProfilesPayload(const std::string& request_body)
{
    auto j = json::parse(request_body);
    if (!j.is_array())
    {
        throw std::invalid_argument("Error: provider profiles payload must be an array.");
    }

    std::vector<ProviderProfile> profiles;
    profiles.reserve(j.size());
    for (const auto& item : j)
    {
        if (!item.is_object())
        {
            throw std::invalid_argument("Error: each provider profile item must be an object.");
        }
        if (!item.contains("provider") || !item.contains("model") || !item.contains("api_key"))
        {
            throw std::invalid_argument("Error: provider profile requires 'provider', 'model' and 'api_key'.");
        }
        // provider profile 是 model/key 的新边界。
        // Handler 只做形状校验，具体允许哪些 provider 先由前端和 main.cpp 的 provider 解析共同收口。
        profiles.push_back(item.get<ProviderProfile>());
    }
    return profiles;
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

void ConfigHandler::handleUpdateProviderProfiles(const HttpRequest &req, HttpResponse *resp, const MiniMuduo::net::TcpConnectionPtr &conn)
{
    std::weak_ptr<MiniMuduo::net::TcpConnection> weakConn(conn);
    std::string requestBody = req.body_;

    auto work = [repo = repo_, weakConn, requestBody]()
    {
        try
        {
            std::vector<ProviderProfile> profiles = ParseProviderProfilesPayload(requestBody);

            repo->handleUpdateProviderProfiles(profiles);

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
            std::cerr << "[Worker Error] updateProviderProfiles: " << e.what() << '\n';
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

void ConfigHandler::handleProbeChannel(const HttpRequest &req, HttpResponse *resp, const MiniMuduo::net::TcpConnectionPtr &conn)
{
    std::weak_ptr<MiniMuduo::net::TcpConnection> weakConn(conn);
    std::string requestBody = req.body_;

    auto work = [weakConn, requestBody]()
    {
        try
        {
            auto j = json::parse(requestBody);
            
            WebhookChannel channel;
            channel.provider = j.value("provider", "feishu");
            channel.webhook_url = j.value("webhookUrl", ""); // Frontend sends webhookUrl
            if (channel.webhook_url.empty()) {
                channel.webhook_url = j.value("webhook_url", "");
            }
            channel.secret = j.value("secret", "");
            channel.enabled = true; // Probe ignores actual enabled state
            channel.threshold = "info"; // Probe forces low threshold to bypass filtering
            
            if (channel.webhook_url.empty()) {
                throw std::invalid_argument("webhook URL is required for probe");
            }
            
            std::vector<WebhookChannel> channels = {channel};
            WebhookNotifier notifier(std::move(channels));
            
            TraceAlertEvent event;
            event.trace_id = "mock-probe-trace-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
            event.start_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            event.duration_ms = 123;
            event.span_count = 5;
            event.token_count = 100;
            event.risk_level = "error";
            event.summary = "这是一条来自 LogSentinel 前端设置页的测试告警消息。";
            event.root_cause = "由于用户在 Webhook 设置页点击了【发送测试消息】按钮，系统触发了此探针请求。";
            event.solution = "如果您能看到这条消息，说明您的 Webhook URL 与签名 Secret 配置完全正确。";
            
            notifier.notifyTraceAlert(event);
            
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
            std::cerr << "[Worker Error] probeChannel: " << e.what() << '\n';
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
