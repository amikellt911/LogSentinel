#pragma once
#include<sqlite3.h>
#include<string>
#include<mutex>
#include<optional>
class SqliteLogRepository
{
public:
    SqliteLogRepository(const std::string& db_path);
    ~SqliteLogRepository();
    void SaveRawLog(const std::string& trace_id,const std::string& raw_log);
    // 未来要用的方法，可以先写个空实现
    void saveAnalysisResult(const std::string& trace_id, const std::string& result,const std::string& status);
    // std::string queryResultByTraceId(const std::string& trace_id);
    std::optional<std::string> queryResultByTraceId(const std::string& trace_id);
private:
    sqlite3* db_=nullptr;
    std::mutex mutex_;
    friend class SqliteLogRepository_test;
};


