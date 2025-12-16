#include "SqliteConfigRepository.h"
#include <filesystem>
#include <iostream>
#include "persistence/SqliteHelper.h"
using namespace persistence;

SqliteConfigRepository::SqliteConfigRepository(const std::string &db_path)
{
    // 1. 路径处理
    std::string final_path;
    // 简单判断是否包含路径分隔符
    if (db_path.find('/') != std::string::npos || db_path.find('\\') != std::string::npos) {
        final_path = db_path;
    } else {
        // TODO: 生产环境请改为获取可执行文件所在路径，避免相对路径地雷
        std::string data_path = "../persistence/data/";
        if (!std::filesystem::exists(data_path)) {
            std::error_code ec;
            std::filesystem::create_directories(data_path, ec);
            if (ec) {
                throw std::runtime_error("Failed to create directory: " + data_path);
            }
        }
        final_path = data_path + db_path;
    }

    // 2. 打开数据库
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc = sqlite3_open_v2(final_path.c_str(), &db_, flags, nullptr);
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_); // 必须在 close 前获取
        sqlite3_close_v2(db_);
        db_ = nullptr;
        throw std::runtime_error("Cannot open database: " + err);
    }

    // 使用 try-catch 块确保构造函数内的异常能正确关闭 db
    try {
        char *errmsg = nullptr;

        // 3. 设置 WAL 模式 (必须在事务外)
        //    // pragma:指令
        // journal:日志
        // wal和以前的区别
        // 以前的：undo，会将这一片修改区域给备份起来，然后用户去修改主页面，如果遇到问题就用备份的去恢复，但是问题是修改主页面那就要加锁
        // wal:redo，追加写，有一个wal文件，记录了你的每一次操作，你的每次操作不是在主页面进行，只有等你提交或页数满了之后，主页面才会加锁更新，这样就导致可以有多个读者来读wal和db
        const char *sql_wal = "PRAGMA journal_mode=WAL;";
        rc = sqlite3_exec(db_, sql_wal, nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK) {
            std::cerr << "[WARN] Cannot set WAL mode: " << (errmsg ? errmsg : "unknown") << std::endl;
            sqlite3_free(errmsg);
            // WAL 开启失败虽然不是致命错误，但严重影响高并发性能
        }

        // 4. 开启事务初始化表
        // 注意：这里手动管理事务，如果 checkSqliteError 会抛出异常，必须确保 db_ 能被释放
        // 但由于 db_ 是成员变量，构造函数抛出异常析构函数不执行，所以 catch 块很重要
        
        rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) throw std::runtime_error("Failed to begin transaction");

        const char *init_sql = R"(
            -- 1. 通用配置表
            CREATE TABLE IF NOT EXISTS app_config (
                config_key TEXT PRIMARY KEY,
                config_value TEXT,
                description TEXT,
                updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );

            -- 2. AI 提示词表
            CREATE TABLE IF NOT EXISTS ai_prompts (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL UNIQUE,
                content TEXT NOT NULL,
                is_active INTEGER DEFAULT 1,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );

            -- 3. 报警通知渠道表
            CREATE TABLE IF NOT EXISTS alert_channels (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                provider TEXT NOT NULL,
                webhook_url TEXT NOT NULL,
                alert_threshold TEXT NOT NULL,
                msg_template TEXT,
                is_active INTEGER DEFAULT 0,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );

            -- 初始化默认配置数据
            -- 修复了这里的语法错误：去掉了末尾逗号，加了分号
            INSERT OR IGNORE INTO app_config (config_key, config_value, description) VALUES 
            ('ai_provider', 'openai', 'AI服务商类型'),
            ('ai_model', 'gpt-4-turbo', '模型名称'),
            ('ai_api_key', '', 'API密钥'),
            ('ai_language', 'English', '解析语言'),
            ('kernel_adaptive_mode', '1', '自适应微批模式开关 1/0'),
            ('kernel_max_batch', '50', '自适应最大阈值'),
            ('kernel_refresh_interval', '200', '刷新间隔ms'),
            ('kernel_worker_threads', '4', '工作线程数'),
            ('kernel_io_buffer', '256MB', 'IO缓冲区大小'); 
        )";

        rc = sqlite3_exec(db_, init_sql, nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK) {
            std::string err_str = errmsg ? errmsg : "Unknown error";
            sqlite3_free(errmsg);
            throw std::runtime_error("Failed to Create Tables: " + err_str);
        }

        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) throw std::runtime_error("Failed to commit transaction");

    } catch (const std::exception &e) {
        // 构造函数失败时的资源清理（非常重要！）
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_close_v2(db_);
        db_ = nullptr;
        // 重新抛出异常给上层
        throw;
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
    if (rc != SQLITE_OK)
    {
        throw std::runtime_error("Failed to prepare getActiveWebhookUrls statement");
    }
    while (rc = sqlite3_step(stmt))
    {
        if (rc != SQLITE_ROW)
            break;
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        if (text)
        {
            urls.emplace_back(reinterpret_cast<const char *>(text));
        }
    }
    if (rc != SQLITE_DONE)
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
    if (rc != SQLITE_OK)
    {
        throw std::runtime_error("Failed to prepare addWebhookUrl statement");
    }
    rc = sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK)
    {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to bind addWebhookUrl statement");
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
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
    if (rc != SQLITE_OK)
    {
        throw std::runtime_error("Failed to prepare deleteWebhookUrl statement");
    }
    rc = sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK)
    {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to bind deleteWebhookUrl statement");
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to execute deleteWebhookUrl statement");
    }
    sqlite3_finalize(stmt);
    return;
}

