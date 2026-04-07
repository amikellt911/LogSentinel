#pragma once
#include "INotifier.h"
#include <functional>
#include <vector>
#include "WebhookFormatters.h"

class WebhookNotifier:public INotifier
{
    public:
        using PostJsonFn = std::function<void(const WebhookChannel& channel,
                                              const std::string& body,
                                              const std::string& log_prefix)>;
        using NowSecondsFn = std::function<int64_t()>;

        void notify(const std::string& trace_id,const nlohmann::json& content) override;
        void notifyTraceAlert(const TraceAlertEvent& event) override;
        //使用string，而非sqlite初始化，因为这样会构成强依赖，notifier强依赖于persistence，
        //导致如果想要更改依赖变得困难，我之后也许会通过config.yaml来初始化，这样会造成麻烦，
        //不如都转成string初始化，解耦
        //explicit WebhookNotifier(SqliteConfigRepository& config_repository);
        explicit WebhookNotifier(std::vector<WebhookChannel> channels,
                                 PostJsonFn post_json_fn = {},
                                 NowSecondsFn now_seconds_fn = {});
        // 兼容旧调用点：现有主链和历史测试里还在传纯 URL 列表，
        // 这里统一按 generic + enabled=true 映射成渠道结构，避免这一步顺手扩太多文件。
        explicit WebhookNotifier(std::vector<std::string> webhook_urls);
    private:
        void postJson(const WebhookChannel& channel, const std::string& body, const std::string& log_prefix);
        std::string maybeWrapFeishuSignedPayload(const WebhookChannel& channel, const std::string& body) const;

        std::vector<WebhookChannel> channels_;
        PostJsonFn post_json_fn_;
        NowSecondsFn now_seconds_fn_;
};  
