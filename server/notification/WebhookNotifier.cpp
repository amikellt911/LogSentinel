#include "WebhookNotifier.h"
#include <cpr/cpr.h>
#include <chrono>
#include <iostream>
namespace
{
    int64_t currentUnixMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    bool isHttpSuccess(long status_code)
    {
        return status_code >= 200 && status_code < 300;
    }

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

    void postJsonToWebhookUrls(const std::vector<std::string>& urls, const std::string& body, const std::string& log_prefix)
    {
        cpr::Session &session = getTlsSession();
        for (const auto &url : urls)
        {
            session.SetUrl(cpr::Url{url});
            session.SetBody(cpr::Body{body});
            cpr::Response r = session.Post();
            if (!isHttpSuccess(r.status_code))
            {
                // 这里不抛异常是为了保证多目标发送时“尽力而为”，一个失败不影响其他目标。
                std::cerr << "[" << log_prefix << "] Target: " << url
                          << " | Status: " << r.status_code
                          << " | Msg: " << r.text << std::endl;
            }
        }
    }

    nlohmann::json buildTraceAlertPayload(const TraceAlertEvent& event)
    {
        const int64_t sent_at_ms = currentUnixMs();

        nlohmann::json payload;
        payload["schema_version"] = "1.0";
        payload["event_type"] = "trace_alert";
        payload["event_id"] = "trace_alert:" + event.trace_id + ":" + std::to_string(sent_at_ms);
        payload["sent_at_ms"] = sent_at_ms;
        payload["source"] = "LogSentinel";
        payload["data"] = {
            {"trace_id", event.trace_id},
            {"service_name", event.service_name},
            {"start_time_ms", event.start_time_ms},
            {"duration_ms", event.duration_ms},
            {"span_count", event.span_count},
            {"token_count", event.token_count},
            {"risk_level", event.risk_level},
            {"summary", event.summary},
            {"root_cause", event.root_cause},
            {"solution", event.solution},
            {"confidence", event.confidence}
        };
        return payload;
    }
} // namespace

void WebhookNotifier::notify(const std::string &trace_id, const nlohmann::json &content)
{
    nlohmann::json notify_content;
    notify_content["trace_id"] = trace_id;
    notify_content["content"] = content;
    notify_content["type"] = "Single_Alert";
    std::string body = notify_content.dump();
    postJsonToWebhookUrls(urls_, body, "Webhook Error");
}

void WebhookNotifier::notifyTraceAlert(const TraceAlertEvent &event)
{
    nlohmann::json payload = buildTraceAlertPayload(event);
    postJsonToWebhookUrls(urls_, payload.dump(), "Webhook TraceAlert Error");
}

WebhookNotifier::WebhookNotifier(std::vector<std::string> webhook_urls)
    : urls_(std::move(webhook_urls))
{
    // 可能会有多线程问题，所以不如直接局部变量或者tls
    //  session_=std::make_unique<cpr::Session>();
    //  session_->SetHeader(cpr::Header({"Content-Type","application/json"}));
    //  session_->SetTimeout(std::chrono::seconds());
}
