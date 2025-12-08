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

void WebhookNotifier::notifyReport(const std::string &global_summary, std::vector<AnalysisResultItem> &items)
{
    if (urls_.empty())
        return;

    nlohmann::json report;
    report["type"] = "Batch_Report";
    report["global_summary"] = global_summary;
    report["timestamp"] = std::time(nullptr);
    report["count"] = items.size();

    nlohmann::json details_arr = nlohmann::json::array();

    int display_limit = 10;
    int count = 0;
    for (const auto &item : items)
    {
        if (count++ >= display_limit)
            break;
        nlohmann::json detail;
        detail["trace_id"] = item.trace_id;
        detail["risk"] = item.result.risk_level;
        detail["summary"] = item.result.summary;
        details_arr.push_back(std::move(detail));
    }
    report["details"] = details_arr;
    if (items.size() > display_limit)
    {
        report["more"] = "And " + std::to_string(items.size() - display_limit) + " more...";
    }
    cpr::Session &session = getTlsSession();
    std::string body = report.dump();
    for (const auto &url : urls_)
    {
        session.SetUrl(cpr::Url{url});
        session.SetBody(cpr::Body{body});

        cpr::Response r = session.Post();

        if (r.status_code != 200)
        {
            std::cerr << "[Webhook Batch Error] Target: " << url
                      << " | Status: " << r.status_code
                      << " | Msg: " << r.text << std::endl;
        }
    }
}
