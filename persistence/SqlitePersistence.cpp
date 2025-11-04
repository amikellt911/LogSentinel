#include "persistence/SqlitePersistence.h"
#include <filesystem>
#include <iostream>
#include "SqlitePersistence.h"
SqlitePersistence::SqlitePersistence(const std::string &db_path)
{
    std::string data_path = "../persistence/data/";
    if (!std::filesystem::exists(data_path))
    {
        std::filesystem::create_directories(data_path);
    }
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc = sqlite3_open_v2((data_path+db_path).c_str(), &db_, flags, nullptr);
    if (rc != SQLITE_OK)
    {
        std::cerr << "Cannot open datebase " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_close_v2(db_);
        db_ = nullptr;
        throw std::runtime_error("Failed to open database");
    }
    char *errmsg = nullptr;
    // pragma:指令
    // journal:日志
    // wal和以前的区别
    // 以前的：undo，会将这一片修改区域给备份起来，然后用户去修改主页面，如果遇到问题就用备份的去恢复，但是问题是修改主页面那就要加锁
    // wal:redo，有一个wal文件，记录了你的每一次操作，你的每次操作不是在主页面进行，只有等你提交之后，主页面才会加锁更新，这样就导致可以有多个读者来读主页面
    const char *sql_wal = "PRAGMA journal_mode=WAL;";
    rc = sqlite3_exec(db_, sql_wal, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK)
    {
        std::cerr << "Cannot set WAL mode" << errmsg << std::endl;
        sqlite3_free(errmsg);
        errmsg = nullptr;
        // 不是fatal，还可以用，就不报异常
    }
    const char *sql_create_table = R"(
CREATE TABLE IF NOT EXISTS raw_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    trace_id TEXT NOT NULL UNIQUE,
    log_content TEXT,
    received_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
)
)";
    errmsg = nullptr;
    rc = sqlite3_exec(db_, sql_create_table, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK)
    {
        std::cerr << "Cannot create raw_logs table " << errmsg << std::endl;
        sqlite3_free(errmsg);
        errmsg = nullptr;
        sqlite3_close_v2(db_);
        db_ = nullptr;
        throw std::runtime_error("Failed to create raw_logs table");
    }
}

SqlitePersistence::~SqlitePersistence()
{
    if (db_)
        sqlite3_close_v2(db_);
}
void SqlitePersistence::SaveRawLog(const std::string &trace_id, const std::string &raw_log)
{
    std::lock_guard<std::mutex> lock_(mutex_);
    const char *sql_insert_template = "insert into raw_logs(trace_id,log_content) values(?,?);";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql_insert_template, -1, &stmt, 0);
    if (rc == SQLITE_OK)
    {
        // 需要std::move吗？
        // SQLITE_TRANSIENT指它在内部复制了字符串，你在外面可以释放，不需要关注空悬指针问题？
        sqlite3_bind_text(stmt, 1, trace_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, raw_log.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            sqlite3_finalize(stmt);
            throw std::runtime_error("Failed to execute statement for insert raw_log "+std::string(sqlite3_errmsg(db_)));
        }
    }
    else
    {
        //如果prepare失败，仍然是Nullptr
        //sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare stmt for raw_log"+std::string(sqlite3_errmsg(db_)));
    }
    sqlite3_finalize(stmt);
}