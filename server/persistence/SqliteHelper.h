#pragma once
#include<memory>
#include<sqlite3.h>
#include<string>
#include<stdexcept>

namespace persistence { 
    struct  SqliteStmtDeleter
    {
        void operator()(sqlite3_stmt* stmt) const{
            if(stmt){
                sqlite3_finalize(stmt);
            }
        }
    };
    using StmtPtr=std::unique_ptr<sqlite3_stmt,SqliteStmtDeleter>;

    // (可选) 一个辅助函数，帮你在出错时抛出带错误信息的异常
    // 这样你就不用每次都写 std::string(sqlite3_errmsg(db)) 了
    inline void checkSqliteError(sqlite3* db, int rc, const std::string& context) {
        if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
            std::string err_msg = context + ": " + sqlite3_errmsg(db);
            throw std::runtime_error(err_msg);
        }
    }
}//namespace persistence

// 【新增历史日志结构体】
struct HistoricalLogItem {
    std::string trace_id;
    std::string risk_level;
    std::string summary;
    std::string processed_at; // 日志分析完成的时间戳

    // 必须有，用于 nlohmann::json 自动序列化给前端
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(HistoricalLogItem, trace_id, risk_level, summary, processed_at);
};

// 【新增历史日志分页结果的结构体】
struct HistoryPage {
    std::vector<HistoricalLogItem> logs;
    int total_count = 0; // 总记录数，用于前端分页组件

    // 必须有，用于 nlohmann::json 自动序列化给前端
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(HistoryPage, logs, total_count);
};