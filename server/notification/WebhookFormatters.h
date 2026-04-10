#pragma once

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "NotifyTypes.h"

class IWebhookFormatter
{
public:
    virtual ~IWebhookFormatter() = default;

    // formatter 只负责把领域事件转成目标平台 payload，
    // 不关心 URL、HTTP 发送和重试策略。
    virtual nlohmann::json FormatTraceAlert(const TraceAlertEvent& event) const = 0;
};

class GenericWebhookFormatter final : public IWebhookFormatter
{
public:
    nlohmann::json FormatTraceAlert(const TraceAlertEvent& event) const override;
};

class FeishuWebhookFormatter final : public IWebhookFormatter
{
public:
    nlohmann::json FormatTraceAlert(const TraceAlertEvent& event) const override;
};

// provider -> formatter 的映射先集中放在这里，避免 WebhookNotifier 里堆 if/else 细节。
// 第一版只正式支持 generic / feishu(lark)，未知 provider 统一回退到 generic。
std::shared_ptr<const IWebhookFormatter> ResolveWebhookFormatter(const std::string& provider);
