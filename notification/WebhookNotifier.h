#pragma once
#include "INotifier.h"
#include <memory>
namespace cpr{
    class Session;
}
class WebhookNotifier:public INotifier
{
    public:
        void notify(const std::string& trace_id,const nlohmann::json& content) override;
        //使用string，而非sqlite初始化，因为这样会构成强依赖，notifier强依赖于persistence，
        //导致如果想要更改依赖变得困难，我之后也许会通过config.yaml来初始化，这样会造成麻烦，
        //不如都转成string初始化，解耦
        //explicit WebhookNotifier(SqliteConfigRepository& config_repository);
        explicit WebhookNotifier(std::vector<std::string> webhook_urls);
        void notifyReport(const std::string& global_summary,std::vector<AnalysisResultItem>& items) override;
    private:
        std::vector<std::string> urls_;
        //std::unique_ptr<cpr::Session> session_;
};  