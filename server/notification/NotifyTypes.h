#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// TraceAlertEvent 作为通知层的稳定数据契约，目的是把“业务结构体”与“发送协议”解耦。
// 这样调用方只传递领域字段，具体如何序列化为 webhook JSON 留在 notifier 实现层处理。
struct TraceAlertEvent
{
    std::string trace_id;
    std::string service_name;
    int64_t start_time_ms = 0;
    int64_t duration_ms = 0;
    size_t span_count = 0;
    size_t token_count = 0;

    std::string risk_level;
    std::string summary;
    std::string root_cause;
    std::string solution;
    double confidence = 0.0;
};

// WebhookChannel 表达“一个真正可发送的外部通知目标”。
// 这里的 secret 是飞书签名校验用的共享密钥，不是公私钥体系里的私钥：
// 如果为空，就按无签名 webhook 发送；如果非空，就在发送前自动补 timestamp/sign。
struct WebhookChannel
{
    std::string provider;
    std::string webhook_url;
    bool enabled = true;
    std::string secret;
    // threshold 表示“从哪个风险等级开始真正外发”。
    // 既然 Settings 已经允许用户按渠道配置 critical/error/warning，
    // 那么通知层就必须把这个阈值带下来，不能只消费地址和签名密钥。
    std::string threshold = "critical";
};
