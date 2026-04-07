#include "WebhookFormatters.h"

#include <algorithm>
#include <cctype>
#include <chrono>

namespace
{
int64_t currentUnixMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string normalizeProvider(std::string provider)
{
    std::transform(provider.begin(), provider.end(), provider.begin(), [](unsigned char ch)
                   {
                       return static_cast<char>(std::tolower(ch));
                   });
    return provider;
}
} // namespace

nlohmann::json GenericWebhookFormatter::FormatTraceAlert(const TraceAlertEvent& event) const
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

nlohmann::json FeishuWebhookFormatter::FormatTraceAlert(const TraceAlertEvent& event) const
{
    nlohmann::json payload;
    payload["msg_type"] = "post";
    payload["content"]["post"]["zh_cn"]["title"] =
        "[LogSentinel][" + event.risk_level + "] Trace 告警";

    // 飞书第一版先收口成最小 post 模板：
    // 能看清级别、trace_id、摘要、根因和基本排障字段就够，不把消息卡片复杂度提前引进来。
    payload["content"]["post"]["zh_cn"]["content"] = nlohmann::json::array({
        nlohmann::json::array({
            nlohmann::json{{"tag", "text"},
                           {"text",
                            "告警级别：" + event.risk_level + "\n"
                            "Trace ID：" + event.trace_id + "\n"
                            "开始时间：" + std::to_string(event.start_time_ms) + "\n"
                            "持续时间：" + std::to_string(event.duration_ms) + " ms\n"
                            "Span 数：" + std::to_string(event.span_count) + "\n"
                            "Token 数：" + std::to_string(event.token_count) + "\n"}}}),
        nlohmann::json::array({
            nlohmann::json{{"tag", "text"},
                           {"text", "摘要：" + event.summary + "\n"}}}),
        nlohmann::json::array({
            nlohmann::json{{"tag", "text"},
                           {"text", "根因：" + event.root_cause + "\n"}}}),
        nlohmann::json::array({
            nlohmann::json{{"tag", "text"},
                           {"text", "建议：" + event.solution + "\n"}}})
    });

    return payload;
}

std::shared_ptr<const IWebhookFormatter> ResolveWebhookFormatter(const std::string& provider)
{
    static const std::shared_ptr<const IWebhookFormatter> generic_formatter =
        std::make_shared<GenericWebhookFormatter>();
    static const std::shared_ptr<const IWebhookFormatter> feishu_formatter =
        std::make_shared<FeishuWebhookFormatter>();

    const std::string normalized = normalizeProvider(provider);
    if (normalized == "feishu" || normalized == "lark")
    {
        return feishu_formatter;
    }
    return generic_formatter;
}
