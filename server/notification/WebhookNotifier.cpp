#include "WebhookNotifier.h"
#include <cpr/cpr.h>
#include <iostream>
#include <chrono>
namespace
{
    cpr::Session &getTlsSession()
    {
        static thread_local std::unique_ptr<cpr::Session> session = []()
        {
            auto s = std::make_unique<cpr::Session>();
            s->SetTimeout(std::chrono::seconds(5));
            s->SetHeader(cpr::Header{{"Content-Type", "application/json"}});
            return s;
        }();
        return *session;
    }
} // namespace
void WebhookNotifier::notify(const std::string &trace_id, const nlohmann::json &content)
{
    nlohmann::json notify_content;
    notify_content["trace_id"] = trace_id;
    notify_content["content"] = content;
    notify_content["type"] = "Single_Alert";
    std::string body = notify_content.dump();
    cpr::Session &session = getTlsSession();
    for (const auto &url : urls_)
    {

        session.SetUrl(cpr::Url{url});
        session.SetBody(cpr::Body{body});
        cpr::Response r = session.Post();

        if (r.status_code != 200)
        {
            // 使用 cerr 或者你的 Log 库
            // 不能抛异常，因为可能因为第一个，导致其他也不能发送
            std::cerr << "[Webhook Error] Target: " << url
                      << " | Status: " << r.status_code
                      << " | Msg: " << r.text << std::endl;
        }
    }
}
WebhookNotifier::WebhookNotifier(std::vector<std::string> webhook_urls)
    : urls_(std::move(webhook_urls))
{
    // 可能会有多线程问题，所以不如直接局部变量或者tls
    //  session_=std::make_unique<cpr::Session>();
    //  session_->SetHeader(cpr::Header({"Content-Type","application/json"}));
    //  session_->SetTimeout(std::chrono::seconds());
}
