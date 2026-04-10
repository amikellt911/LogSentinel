#include "WebhookNotifier.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
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

std::string toLowerCopy(std::string value)
{
    for (char& ch : value)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

bool isFeishuProvider(const std::string& provider)
{
    const std::string lowered = toLowerCopy(provider);
    return lowered == "feishu" || lowered == "lark";
}

int riskLevelRank(const std::string& risk_level)
{
    const std::string lowered = toLowerCopy(risk_level);
    if (lowered == "critical")
    {
        return 5;
    }
    if (lowered == "error")
    {
        return 4;
    }
    if (lowered == "warning")
    {
        return 3;
    }
    if (lowered == "info")
    {
        return 2;
    }
    if (lowered == "safe")
    {
        return 1;
    }
    if (lowered == "unknown")
    {
        return 0;
    }
    return -1;
}

bool shouldSendTraceAlertToChannel(const WebhookChannel& channel, const TraceAlertEvent& event)
{
    // threshold 的语义是“从哪个风险等级开始发消息”，不是展示标签。
    // 所以这里要在真正序列化并发请求之前先做过滤，避免 critical-only 渠道继续收到 warning/error。
    const int event_rank = riskLevelRank(event.risk_level);
    const int threshold_rank = riskLevelRank(channel.threshold.empty() ? "critical" : channel.threshold);
    if (event_rank < 0 || threshold_rank < 0)
    {
        return false;
    }
    return event_rank >= threshold_rank;
}

std::string base64Encode(const unsigned char* data, size_t size)
{
    if (size == 0)
    {
        return "";
    }

    std::string encoded;
    encoded.resize(4 * ((static_cast<int>(size) + 2) / 3));
    const int written = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()),
                                        data,
                                        static_cast<int>(size));
    if (written < 0)
    {
        throw std::runtime_error("EVP_EncodeBlock failed");
    }
    encoded.resize(static_cast<size_t>(written));
    return encoded;
}

std::string computeFeishuSign(int64_t timestamp_seconds, const std::string& secret)
{
    const std::string string_to_sign = std::to_string(timestamp_seconds) + "\n" + secret;
    unsigned int digest_length = 0;
    unsigned char digest[EVP_MAX_MD_SIZE];
    if (HMAC(EVP_sha256(),
             string_to_sign.data(),
             static_cast<int>(string_to_sign.size()),
             reinterpret_cast<const unsigned char*>(""),
             0,
             digest,
             &digest_length) == nullptr)
    {
        throw std::runtime_error("HMAC(EVP_sha256) failed");
    }
    return base64Encode(digest, digest_length);
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
        if (!shouldSendTraceAlertToChannel(channel, event))
        {
            continue;
        }

        const std::shared_ptr<const IWebhookFormatter> formatter =
            ResolveWebhookFormatter(channel.provider);
        const nlohmann::json payload = formatter->FormatTraceAlert(event);
        postJson(channel, payload.dump(), "Webhook TraceAlert Error");
    }
}

WebhookNotifier::WebhookNotifier(std::vector<WebhookChannel> channels,
                                 PostJsonFn post_json_fn,
                                 NowSecondsFn now_seconds_fn)
    : channels_(std::move(channels)),
      post_json_fn_(std::move(post_json_fn)),
      now_seconds_fn_(std::move(now_seconds_fn))
{
}

WebhookNotifier::WebhookNotifier(std::vector<std::string> webhook_urls)
{
    channels_.reserve(webhook_urls.size());
    for (auto& webhook_url : webhook_urls)
    {
        // 兼容旧主链：原来只有 URL 时，默认都按 generic 渠道处理，
        // 这样现有 mock webhook 和历史入口不用跟着这一刀一起重写。
        channels_.push_back(WebhookChannel{"generic", std::move(webhook_url), true, "", "critical"});
    }
}

std::string WebhookNotifier::maybeWrapFeishuSignedPayload(const WebhookChannel& channel,
                                                          const std::string& body) const
{
    // 签名是发送前的协议包装，不是业务内容模板的一部分：
    // formatter 只负责产出 msg_type/content，真正要不要补 timestamp/sign，
    // 取决于这个渠道有没有 secret，以及 provider 是不是飞书。
    if (!isFeishuProvider(channel.provider) || channel.secret.empty())
    {
        return body;
    }

    const int64_t timestamp_seconds = now_seconds_fn_
                                          ? now_seconds_fn_()
                                          : std::chrono::duration_cast<std::chrono::seconds>(
                                                std::chrono::system_clock::now().time_since_epoch())
                                                .count();

    nlohmann::json payload = nlohmann::json::parse(body);
    payload["timestamp"] = std::to_string(timestamp_seconds);
    payload["sign"] = computeFeishuSign(timestamp_seconds, channel.secret);
    return payload.dump();
}

void WebhookNotifier::postJson(const WebhookChannel& channel,
                               const std::string& body,
                               const std::string& log_prefix)
{
    const std::string wrapped_body = maybeWrapFeishuSignedPayload(channel, body);

    if (post_json_fn_)
    {
        post_json_fn_(channel, wrapped_body, log_prefix);
        return;
    }

    cpr::Session& session = getTlsSession();
    session.SetUrl(cpr::Url{channel.webhook_url});
    session.SetBody(cpr::Body{wrapped_body});
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
