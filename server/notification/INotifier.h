#pragma once
#include<string>
#include<nlohmann/json.hpp>
#include<persistence/SqliteLogRepository.h>
class INotifier {
public:
    virtual ~INotifier() = default;
    virtual void notify(const std::string& trace_id,const nlohmann::json& content) = 0;

    virtual void notifyReport(const std::string& global_summary,std::vector<AnalysisResultItem>& items)=0;
};