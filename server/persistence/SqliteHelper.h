#pragma once
#include<memory>
#include<sqlite3.h>
#include<string>
#include<stdexcept>
#include<nlohmann/json.hpp>
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

