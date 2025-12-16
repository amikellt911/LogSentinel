#include "SqliteConfigRepository.h"
#include <filesystem>
#include <iostream>
SqliteConfigRepository::SqliteConfigRepository(const std::string &db_path)
{
    std::string final_path;
    if (db_path.find('/') != std::string::npos || db_path.find('\\') != std::string::npos) {
        final_path = db_path;
    } else {
        std::string data_path = "../server/persistence/data/";
        if (!std::filesystem::exists(data_path)) {
            std::filesystem::create_directories(data_path);
        }
        final_path = data_path + db_path;
    }
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc = sqlite3_open_v2(final_path.c_str(), &db_, flags, nullptr);
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
CREATE TABLE IF NOT EXISTS webhook_configs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    url TEXT NOT NULL UNIQUE,          -- 核心数据：URL (加UNIQUE防止重复添加)
    is_active INTEGER DEFAULT 1,       -- 开关：软删除或暂停发送
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP -- 调试用：知道是什么时候加的
);
)";
    errmsg = nullptr;
    rc = sqlite3_exec(db_, sql_create_table, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK)
    {
        std::cerr << "Cannot create webhook_configs table " << errmsg << std::endl;
        sqlite3_free(errmsg);
        errmsg = nullptr;
        sqlite3_close_v2(db_);
        db_ = nullptr;
        throw std::runtime_error("Failed to create webhook_configs table");
    }
}
SqliteConfigRepository::~SqliteConfigRepository()
{
    if (db_)
        sqlite3_close_v2(db_);
}
std::vector<std::string> SqliteConfigRepository::getActiveWebhookUrls()
{
    std::lock_guard<std::mutex> lock_(mutex_);
    std::vector<std::string> urls;
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT url FROM webhook_configs WHERE is_active = 1;";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if(rc != SQLITE_OK)
    {
        throw std::runtime_error("Failed to prepare getActiveWebhookUrls statement");
    }
    while (rc = sqlite3_step(stmt))
    {
        if(rc!= SQLITE_ROW)
            break;
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text) {
            urls.emplace_back(reinterpret_cast<const char *>(text));
        }
    }
    if(rc != SQLITE_DONE)
    {
        sqlite3_finalize(stmt); // 清理资源
        throw std::runtime_error("Failed to execute getActiveWebhookUrls statement");
    }
    sqlite3_finalize(stmt);
    return urls;
}

void SqliteConfigRepository::addWebhookUrl(const std::string &url)
{
    std::lock_guard<std::mutex> lock_(mutex_);
    sqlite3_stmt *stmt = nullptr;
    // 4. 使用 INSERT OR IGNORE 防止重复报错
    const char *sql = "INSERT OR IGNORE INTO webhook_configs (url) VALUES (?);";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if(rc != SQLITE_OK)
    {
        throw std::runtime_error("Failed to prepare addWebhookUrl statement");
    }
    rc = sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
    if(rc != SQLITE_OK)
    {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to bind addWebhookUrl statement");
    }
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to execute addWebhookUrl statement");
    }
    sqlite3_finalize(stmt);
    return;
}

void SqliteConfigRepository::deleteWebhookUrl(const std::string &url)
{
    std::lock_guard<std::mutex> lock_(mutex_);
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "DELETE FROM webhook_configs WHERE url = ?;";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if(rc != SQLITE_OK)
    {
        throw std::runtime_error("Failed to prepare deleteWebhookUrl statement");
    }
    rc = sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
    if(rc != SQLITE_OK)
    {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to bind deleteWebhookUrl statement");
    }
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to execute deleteWebhookUrl statement");
    }
    sqlite3_finalize(stmt);
    return;
}
