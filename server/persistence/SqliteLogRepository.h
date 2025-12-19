#pragma once
#include<sqlite3.h>
#include<string>
#include<mutex>
#include<optional>
#include "persistence/SqliteHelper.h"
#include <persistence/ConfigTypes.h>
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

    // 批量保存原始日志 (BEGIN TRANSACTION ... COMMIT)
    void saveRawLogBatch(const std::vector<std::pair<std::string, std::string>>& logs);

    // 批量保存分析结果
    void saveAnalysisResultBatch(const std::vector<AnalysisResultItem>& items,const std::string& global_summary);

    HistoryPage getHistoricalLogs(int page, int pageSize, const std::string& level = "", const std::string& keyword = "");
private:
    sqlite3* db_=nullptr;
    std::mutex mutex_;
    friend class SqliteLogRepository_test;
};


