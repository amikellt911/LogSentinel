#include "WebhookNotifier.h"

#include <cpr/cpr.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <utility>

namespace
{
bool isHttpSuccess(long status_code)
{
    return status_code >= 200 && status_code < 300;
}

cpr::Session& getTlsSession()
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

void WebhookNotifier::notify(const std::string& trace_id, const nlohmann::json& content)
{
    nlohmann::json notify_content;
    notify_content["trace_id"] = trace_id;
    notify_content["content"] = content;
    notify_content["type"] = "Single_Alert";

    const std::string body = notify_content.dump();
    for (const auto& channel : channels_)
    {
        if (!channel.enabled || channel.webhook_url.empty())
        {
            continue;
        }

        // 旧 notify 接口继续保留给 generic webhook 使用；
        // 这一步先不强行把历史单条通知语义扩成飞书模板，避免把旧接口和 trace_alert 搅在一起。
        postJson(channel, body, "Webhook Error");
    }
}

void WebhookNotifier::notifyTraceAlert(const TraceAlertEvent& event)
{
    for (const auto& channel : channels_)
    {
        if (!channel.enabled || channel.webhook_url.empty())
        {
            continue;
        }

        const std::shared_ptr<const IWebhookFormatter> formatter =
            ResolveWebhookFormatter(channel.provider);
        const nlohmann::json payload = formatter->FormatTraceAlert(event);
        postJson(channel, payload.dump(), "Webhook TraceAlert Error");
    }
}

WebhookNotifier::WebhookNotifier(std::vector<WebhookChannel> channels, PostJsonFn post_json_fn)
    : channels_(std::move(channels)),
      post_json_fn_(std::move(post_json_fn))
{
}

WebhookNotifier::WebhookNotifier(std::vector<std::string> webhook_urls)
{
    channels_.reserve(webhook_urls.size());
    for (auto& webhook_url : webhook_urls)
    {
        // 兼容旧主链：原来只有 URL 时，默认都按 generic 渠道处理，
        // 这样现有 mock webhook 和历史入口不用跟着这一刀一起重写。
        channels_.push_back(WebhookChannel{"generic", std::move(webhook_url), true});
    }
}

void WebhookNotifier::postJson(const WebhookChannel& channel,
                               const std::string& body,
                               const std::string& log_prefix)
{
    if (post_json_fn_)
    {
        post_json_fn_(channel, body, log_prefix);
        return;
    }

    cpr::Session& session = getTlsSession();
    session.SetUrl(cpr::Url{channel.webhook_url});
    session.SetBody(cpr::Body{body});
    cpr::Response r = session.Post();
    if (!isHttpSuccess(r.status_code))
    {
        // 这里不抛异常是为了保证多目标发送时“尽力而为”，一个失败不影响其他目标。
        // 但仅打印 status/text 不够，因为像 TLS/DNS/连接失败这类传输层错误通常会表现成
        // status=0，真正有价值的信息在 cpr::Error 里，所以这里要把 code/message 一起打出来。
        std::cerr << "[" << log_prefix << "] Provider: " << channel.provider
                  << " | Target: " << channel.webhook_url
                  << " | Status: " << r.status_code
                  << " | ErrorCode: " << static_cast<int>(r.error.code)
                  << " | ErrorMsg: " << r.error.message
                  << " | Msg: " << r.text << std::endl;
    }
}
