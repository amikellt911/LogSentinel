#pragma once
#include<sqlite3.h>
#include<string>
#include<mutex>
#include<optional>
#include "ai/AiTypes.h"

struct AlertInfo{
    std::string trace_id;
    std::string summary;
    std::string time;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AlertInfo, trace_id, summary, time);
};
struct DashboardStats{
    int total_logs=0;
    int high_risk=0;
    int medium_risk=0;
    int low_risk=0;
    double avg_response_time=0.0;
    std::vector<AlertInfo> recent_alerts;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(DashboardStats, total_logs, high_risk, medium_risk, low_risk, avg_response_time, recent_alerts);
};
class SqliteLogRepository
{
public:
    SqliteLogRepository(const std::string& db_path);
    ~SqliteLogRepository();
    void SaveRawLog(const std::string& trace_id,const std::string& raw_log);
    // 未来要用的方法，可以先写个空实现
    void saveAnalysisResult(const std::string& trace_id, const LogAnalysisResult& result,int response_time_ms,const std::string& status);
    void saveAnalysisResult(const std::string &trace_id, const std::string &result, const std::string &status);
    void saveAnalysisResultError(const std::string& trace_id, const std::string& error_msg);
    // std::string queryResultByTraceId(const std::string& trace_id);
    std::optional<std::string> queryResultByTraceId(const std::string& trace_id);
    std::optional<LogAnalysisResult> queryStructResultByTraceId(const std::string& trace_id);
    DashboardStats getDashboardStats();
private:
    sqlite3* db_=nullptr;
    std::mutex mutex_;
    friend class SqliteLogRepository_test;
};


