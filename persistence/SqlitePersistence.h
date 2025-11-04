#pragma once
#include<sqlite3.h>
#include<string>
#include<mutex>
class SqlitePersistence
{
public:
    SqlitePersistence(const std::string& db_path);
    ~SqlitePersistence();
    void SaveRawLog(const std::string& trace_id,const std::string& raw_log);
    // 未来要用的方法，可以先写个空实现
    // bool saveAnalysisResult(const std::string& trace_id, const std::string& result);
    // std::string queryResultByTraceId(const std::string& trace_id);
    
private:
    sqlite3* db_=nullptr;
    std::mutex mutex_;
};


