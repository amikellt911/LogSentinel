#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "notification/NotifyTypes.h"
#include "notification/WebhookFormatters.h"
#include "notification/WebhookNotifier.h"

namespace
{
TraceAlertEvent MakeTraceAlertEvent()
{
    TraceAlertEvent event;
    event.trace_id = "trace-123";
    event.start_time_ms = 1710000000000;
    event.duration_ms = 820;
    event.span_count = 7;
    event.token_count = 168;
    event.risk_level = "critical";
    event.summary = "订单链路在支付阶段出现高风险异常。";
    event.root_cause = "payment-service 调用 bank-gateway 超时。";
    event.solution = "优先检查 payment-service 到 bank-gateway 的网络与重试配置。";
    event.confidence = 0.92;
    return event;
}
} // namespace

TEST(FeishuWebhookFormatterTest, FormatsTraceAlertAsFeishuPostMessage)
{
    FeishuWebhookFormatter formatter;
    const nlohmann::json payload = formatter.FormatTraceAlert(MakeTraceAlertEvent());

    // 这条测试先把飞书最小 post 模板的契约锁死：
    // 顶层必须是 msg_type=post，标题和正文里必须能看出级别、trace_id、摘要和根因，
    // 后面就算再补链接或签名，也不该把这条最小消息骨架改丢。
    ASSERT_EQ(payload.at("msg_type"), "post");
    const auto& zh_cn = payload.at("content").at("post").at("zh_cn");
    EXPECT_TRUE(zh_cn.at("title").get<std::string>().find("critical") != std::string::npos);

    const std::string dumped = payload.dump();
    EXPECT_TRUE(dumped.find("trace-123") != std::string::npos);
    EXPECT_TRUE(dumped.find("订单链路在支付阶段出现高风险异常") != std::string::npos);
    EXPECT_TRUE(dumped.find("payment-service 调用 bank-gateway 超时") != std::string::npos);
}

TEST(WebhookNotifierTest, NotifyTraceAlertSkipsDisabledChannelsAndUsesProviderFormatter)
{
    std::vector<WebhookChannel> channels{
        {"feishu", "https://example.test/feishu", true},
        {"generic", "https://example.test/generic", true},
        {"feishu", "https://example.test/disabled", false}
    };

    struct PostedRequest
    {
        WebhookChannel channel;
        std::string body;
        std::string log_prefix;
    };
    std::vector<PostedRequest> posted_requests;

    WebhookNotifier notifier(
        std::move(channels),
        [&](const WebhookChannel& channel, const std::string& body, const std::string& log_prefix)
        {
            posted_requests.push_back(PostedRequest{channel, body, log_prefix});
        });

    notifier.notifyTraceAlert(MakeTraceAlertEvent());

    // 这里锁三件事：
    // 1) enabled=false 的渠道必须被跳过；
    // 2) feishu 渠道必须发飞书 post，而不是旧 generic JSON；
    // 3) generic 渠道继续走现在的 trace_alert v1 结构，确保改造不会把老协议一起打坏。
    ASSERT_EQ(posted_requests.size(), 2U);

    const nlohmann::json feishu_payload = nlohmann::json::parse(posted_requests[0].body);
    const nlohmann::json generic_payload = nlohmann::json::parse(posted_requests[1].body);

    EXPECT_EQ(posted_requests[0].channel.provider, "feishu");
    EXPECT_EQ(feishu_payload.at("msg_type"), "post");

    EXPECT_EQ(posted_requests[1].channel.provider, "generic");
    EXPECT_EQ(generic_payload.at("event_type"), "trace_alert");
    EXPECT_EQ(generic_payload.at("data").at("trace_id"), "trace-123");
}
