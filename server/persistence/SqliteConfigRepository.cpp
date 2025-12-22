#include "SqliteConfigRepository.h"
#include <filesystem>
#include <iostream>
#include "persistence/SqliteHelper.h"
using namespace persistence;
static void ApplyConfigValue(AppConfig &config, const std::string &key, const std::string &val)
{
    try
    {
        // --- 字符串类型 ---
        if (key == "ai_provider")
            config.ai_provider = val;
        else if (key == "ai_model")
            config.ai_model = val;
        else if (key == "ai_api_key")
            config.ai_api_key = val;
        else if (key == "ai_language")
            config.ai_language = val;
        else if (key == "app_language")
            config.app_language = val;
        else if (key == "kernel_io_buffer")
            config.kernel_io_buffer = val;

        // --- 数值类型 ---
        else if (key == "kernel_worker_threads")
            config.kernel_worker_threads = std::stoi(val);
        else if (key == "kernel_max_batch")
            config.kernel_max_batch = std::stoi(val);
        else if (key == "kernel_refresh_interval")
            config.kernel_refresh_interval = std::stoi(val);
        
        // --- 新增配置映射 ---
        else if (key == "log_retention_days")
            config.log_retention_days = std::stoi(val);
        else if (key == "max_disk_usage_gb")
            config.max_disk_usage_gb = std::stoi(val);
        else if (key == "http_port")
            config.http_port = std::stoi(val);
        else if (key == "ai_failure_threshold")
            config.ai_failure_threshold = std::stoi(val);
        else if (key == "ai_cooldown_seconds")
            config.ai_cooldown_seconds = std::stoi(val);

        // Updated Active IDs
        else if (key == "active_map_prompt_id")
            config.active_map_prompt_id = std::stoi(val);
        else if (key == "active_reduce_prompt_id")
            config.active_reduce_prompt_id = std::stoi(val);
        
        else if (key == "ai_fallback_model")
            config.ai_fallback_model = val;

        // --- 布尔类型 (兼容 1/true/TRUE) ---
        else if (key == "kernel_adaptive_mode")
        {
            config.kernel_adaptive_mode = (val == "1" || val == "true" || val == "TRUE");
        }
        else if (key == "ai_auto_degrade")
        {
            config.ai_auto_degrade = (val == "1" || val == "true" || val == "TRUE");
        }
        else if (key == "ai_circuit_breaker")
        {
            config.ai_circuit_breaker = (val == "1" || val == "true" || val == "TRUE");
        }
    }
    catch (const std::exception &e)
    {
        // 解析失败只打印日志，不中断流程
        std::cerr << "[ConfigHelper] Parse Error: Key [" << key << "] Value [" << val << "] - " << e.what() << std::endl;
    }
}
SqliteConfigRepository::SqliteConfigRepository(const std::string &db_path)
{
    // 1. 路径处理
    std::string final_path;
    // 简单判断是否包含路径分隔符
    if (db_path.find('/') != std::string::npos || db_path.find('\\') != std::string::npos)
    {
        final_path = db_path;
    }
    else
    {
        // TODO: 生产环境请改为获取可执行文件所在路径，避免相对路径地雷
        std::string data_path = "../persistence/data/";
        
        if (!std::filesystem::exists(data_path))
        {
            std::error_code ec;
            std::filesystem::create_directories(data_path, ec);
            if (ec)
            {
                throw std::runtime_error("Failed to create directory: " + data_path);
            }
        }
        final_path = data_path + db_path;
    }

    // 2. 打开数据库
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc = sqlite3_open_v2(final_path.c_str(), &db_, flags, nullptr);
    if (rc != SQLITE_OK)
    {
        std::string err = sqlite3_errmsg(db_); // 必须在 close 前获取
        sqlite3_close_v2(db_);
        db_ = nullptr;
        throw std::runtime_error("Cannot open database: " + err);
    }

    // 使用 try-catch 块确保构造函数内的异常能正确关闭 db
    try
    {
        char *errmsg = nullptr;

        // 3. 设置 WAL 模式
        const char *sql_wal = "PRAGMA journal_mode=WAL;";
        rc = sqlite3_exec(db_, sql_wal, nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK)
        {
            std::cerr << "[WARN] Cannot set WAL mode: " << (errmsg ? errmsg : "unknown") << std::endl;
            sqlite3_free(errmsg);
        }

        // 4. 开启事务初始化表
        rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK)
            throw std::runtime_error("Failed to begin transaction");

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
                prompt_type TEXT DEFAULT 'map',
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
            INSERT OR IGNORE INTO app_config (config_key, config_value, description) VALUES 
            ('ai_provider', 'openai', 'AI服务商类型'),
            ('ai_model', 'gpt-4-turbo', '模型名称'),
            ('ai_api_key', '', 'API密钥'),
            ('ai_language', 'English', '解析语言'),
            ('app_language', 'en', '界面语言'),
            
            -- 新增字段
            ('log_retention_days', '7', '日志保留天数'),
            ('max_disk_usage_gb', '1', '最大磁盘占用GB'),
            ('http_port', '8080', 'HTTP服务端口'),
            ('ai_auto_degrade', '0', '自动降级开关'),
            ('ai_fallback_model', 'local-mock', '降级模型名称'),
            ('ai_circuit_breaker', '1', '熔断机制开关'),
            ('ai_failure_threshold', '5', '熔断触发阈值'),
            ('ai_cooldown_seconds', '60', '熔断冷却时间s'),
            ('active_map_prompt_id', '0', 'Map阶段激活的PromptID'),
            ('active_reduce_prompt_id', '0', 'Reduce阶段激活的PromptID'),

            ('kernel_adaptive_mode', '1', '自适应微批模式开关 1/0'),
            ('kernel_max_batch', '50', '自适应最大阈值'),
            ('kernel_refresh_interval', '200', '刷新间隔ms'),
            ('kernel_worker_threads', '4', '工作线程数'),
            ('kernel_io_buffer', '256MB', 'IO缓冲区大小'); 
        )";

        rc = sqlite3_exec(db_, init_sql, nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK)
        {
            std::string err_str = errmsg ? errmsg : "Unknown error";
            sqlite3_free(errmsg);
            throw std::runtime_error("Failed to Create Tables: " + err_str);
        }

        // --- Schema Migration for Existing DB ---
        // Try to add column if it doesn't exist. SQLite allows adding column even if it exists in some versions with specific syntax or just ignoring error.
        // But simplest way in SQLite is just run it and ignore error if column exists.
        // However, "duplicate column name" is the error.
        // We will catch it.
        const char *migration_sql = "ALTER TABLE ai_prompts ADD COLUMN prompt_type TEXT DEFAULT 'map';";
        char *mig_errmsg = nullptr;
        rc = sqlite3_exec(db_, migration_sql, nullptr, nullptr, &mig_errmsg);
        if (rc != SQLITE_OK) {
             // Ignore error (likely column already exists), just log debug
             // std::cerr << "[Info] Migration (prompt_type): " << (mig_errmsg ? mig_errmsg : "unknown") << std::endl;
             if (mig_errmsg) sqlite3_free(mig_errmsg);
        }

        loadFromDbInternal();
        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK)
            throw std::runtime_error("Failed to commit transaction");
    }
    catch (const std::exception &e)
    {
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

AppConfig SqliteConfigRepository::getAppConfig()
{
    std::shared_lock<std::shared_mutex> lock_(config_mutex_);
    return cached_app_config_;
}

std::vector<PromptConfig> SqliteConfigRepository::getAllPrompts()
{
    std::shared_lock<std::shared_mutex> lock_(config_mutex_);
    return cached_prompts_;
}

std::vector<AlertChannel> SqliteConfigRepository::getAllChannels()
{
    std::shared_lock<std::shared_mutex> lock_(config_mutex_);
    return cached_channels_;
}

AllSettings SqliteConfigRepository::getAllSettings()
{
    std::shared_lock<std::shared_mutex> lock_(config_mutex_);
    return AllSettings{cached_app_config_, cached_prompts_, cached_channels_};
}

void SqliteConfigRepository::handleUpdateAppConfig(const std::map<std::string, std::string> &mp)
{
    std::unique_lock<std::shared_mutex> lock_(config_mutex_);
    AppConfig configClone(cached_app_config_);
    const char *sql = "update app_config set config_value=? where config_key=?;";
    sqlite3_stmt *stmt_ = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    checkSqliteError(db_, rc, "Failed to begin transaction");
    rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
    if (rc != SQLITE_OK)
    {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        checkSqliteError(db_, rc, "Failed to prepare statement for updateAppConfig");
    }
    StmtPtr stmt(stmt_);
    try
    {
        for (auto &[key, val] : mp)
        {
            // sqlite绑定是从1开始的
            rc = sqlite3_bind_text(stmt.get(), 1, val.c_str(), -1, SQLITE_STATIC);
            if (rc != SQLITE_OK)
            {
                checkSqliteError(db_, rc, "Failed to bind val for updateAppConfig");
            }
            rc = sqlite3_bind_text(stmt.get(), 2, key.c_str(), -1, SQLITE_STATIC);
            if (rc != SQLITE_OK)
            {
                checkSqliteError(db_, rc, "Failed to bind key for updateAppConfig");
            }
            rc = sqlite3_step(stmt.get());
            if (rc != SQLITE_DONE)
            {
                checkSqliteError(db_, rc, "Failed to execute updateAppConfig");
            }
            try
            {
                ApplyConfigValue(configClone, key, val);
            }
            catch (const std::exception &e)
            {
                std::cerr << "[Config] Error parsing key [" << key << "] with value [" << val << "]: " << e.what() << std::endl;
                // 捕获异常后继续循环，不要中断后续配置的加载
            }
            sqlite3_reset(stmt.get());
        }
        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
        checkSqliteError(db_, rc, "Failed to commit");
        std::swap(cached_app_config_, configClone);
    }
    catch (...)
    {
        // 异常发生时：
        // 1. 回滚事务
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        // 2. stmt 自动析构 (finalize)
        // 3. 继续抛出让上层处理
        throw;
    }
}

void SqliteConfigRepository::handleUpdatePrompt(const std::vector<PromptConfig>& prompts_input)
{
    // 1. 上写锁 (互斥)
    std::unique_lock<std::shared_mutex> lock_(config_mutex_);

    // 2. 创建本地副本 (我们需要修改其中的 ID，如果是新插入的话)
    auto new_prompts_cache = prompts_input;

    // 3. 开启事务
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    checkSqliteError(db_, rc, "Failed to begin transaction");

    // 准备语句
    sqlite3_stmt* stmt_insert = nullptr;
    sqlite3_stmt* stmt_update = nullptr;
    
    // SQL: 注意索引从1开始
    // Added prompt_type
    const char* sql_insert = "INSERT INTO ai_prompts (name, content, is_active, prompt_type) VALUES (?, ?, ?, ?);";
    const char* sql_update = "UPDATE ai_prompts SET name=?, content=?, is_active=?, prompt_type=? WHERE id=?;";

    // 资源管理
    try {
        rc = sqlite3_prepare_v2(db_, sql_insert, -1, &stmt_insert, nullptr);
        if (rc != SQLITE_OK) throw std::runtime_error("Prep Insert Failed");
        
        rc = sqlite3_prepare_v2(db_, sql_update, -1, &stmt_update, nullptr);
        if (rc != SQLITE_OK) throw std::runtime_error("Prep Update Failed");

        StmtPtr ptr_insert(stmt_insert);
        StmtPtr ptr_update(stmt_update);

        std::vector<int> active_ids;

        // 4. 遍历处理 (Upsert)
        // 注意：我们要修改 new_prompts_cache 里的 ID，所以用引用遍历
        for (auto& item : new_prompts_cache) {
            sqlite3_stmt* curr_stmt = nullptr;

            if (item.id > 0) {
                // --- UPDATE ---
                curr_stmt = stmt_update;
                sqlite3_bind_text(curr_stmt, 1, item.name.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(curr_stmt, 2, item.content.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int(curr_stmt, 3, item.is_active ? 1 : 0);
                sqlite3_bind_text(curr_stmt, 4, item.type.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int(curr_stmt, 5, item.id);
                
                active_ids.push_back(item.id);
            } else {
                // --- INSERT ---
                curr_stmt = stmt_insert;
                sqlite3_bind_text(curr_stmt, 1, item.name.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(curr_stmt, 2, item.content.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int(curr_stmt, 3, item.is_active ? 1 : 0);
                sqlite3_bind_text(curr_stmt, 4, item.type.c_str(), -1, SQLITE_STATIC);
            }

            if (sqlite3_step(curr_stmt) != SQLITE_DONE) {
                throw std::runtime_error("Upsert step failed");
            }

            // 关键点：如果是插入，必须回填 ID 到缓存副本和 active_ids
            if (item.id <= 0) {
                int new_id = (int)sqlite3_last_insert_rowid(db_);
                item.id = new_id; // 更新副本中的 ID
                active_ids.push_back(new_id);
            }

            sqlite3_reset(curr_stmt);
            sqlite3_clear_bindings(curr_stmt);
        }

        // 5. 修剪 (Delete Pruning)
        if (active_ids.empty()) {
            sqlite3_exec(db_, "DELETE FROM ai_prompts;", nullptr, nullptr, nullptr);
        } else {
            std::stringstream ss;
            ss << "DELETE FROM ai_prompts WHERE id NOT IN (";
            for (size_t i = 0; i < active_ids.size(); ++i) {
                ss << active_ids[i];
                if (i < active_ids.size() - 1) ss << ",";
            }
            ss << ");";
            rc = sqlite3_exec(db_, ss.str().c_str(), nullptr, nullptr, nullptr);
            checkSqliteError(db_, rc, "Prune failed");
        }

        // 6. 提交事务
        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
        checkSqliteError(db_, rc, "Failed to commit");
        // 7. 更新内存缓存 (Swap)
        // 此时 new_prompts_cache 里的 ID 已经是完整的了（包含了新生成的 ID）
        // 使用 swap 保证异常安全，如果前面失败了，cached_prompts_ 不会被污染
        swap(cached_prompts_, new_prompts_cache);
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }
}
void SqliteConfigRepository::handleUpdateChannel(const std::vector<AlertChannel>& channels_input)
{
    std::unique_lock<std::shared_mutex> lock_(config_mutex_);
    auto new_channels_cache = channels_input; // 副本

    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    sqlite3_stmt* stmt_insert = nullptr;
    sqlite3_stmt* stmt_update = nullptr;

    // SQL 准备
    const char* sql_insert = "INSERT INTO alert_channels (name, provider, webhook_url, alert_threshold, msg_template, is_active) VALUES (?, ?, ?, ?, ?, ?);";
    const char* sql_update = "UPDATE alert_channels SET name=?, provider=?, webhook_url=?, alert_threshold=?, msg_template=?, is_active=? WHERE id=?;";

    try {
        int rc = sqlite3_prepare_v2(db_, sql_insert, -1, &stmt_insert, nullptr);
        if(rc != SQLITE_OK) throw std::runtime_error("Prep Insert Failed");
        
        rc = sqlite3_prepare_v2(db_, sql_update, -1, &stmt_update, nullptr);
        if(rc != SQLITE_OK) throw std::runtime_error("Prep Update Failed");

        StmtPtr ptr_insert(stmt_insert);
        StmtPtr ptr_update(stmt_update);

        std::vector<int> active_ids;

        for (auto& item : new_channels_cache) {
            sqlite3_stmt* curr_stmt = nullptr;
            int col_idx = 1; // 绑定索引计数器

            if (item.id > 0) {
                curr_stmt = stmt_update; // Update
            } else {
                curr_stmt = stmt_insert; // Insert
            }

            // 统一绑定逻辑
            sqlite3_bind_text(curr_stmt, col_idx++, item.name.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(curr_stmt, col_idx++, item.provider.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(curr_stmt, col_idx++, item.webhook_url.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(curr_stmt, col_idx++, item.alert_threshold.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(curr_stmt, col_idx++, item.msg_template.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(curr_stmt, col_idx++, item.is_active ? 1 : 0);

            if (item.id > 0) {
                sqlite3_bind_int(curr_stmt, col_idx++, item.id); // Update 最后的 Where id=?
                active_ids.push_back(item.id);
            }

            if (sqlite3_step(curr_stmt) != SQLITE_DONE) {
                throw std::runtime_error("Upsert step failed");
            }

            // 回填 ID
            if (item.id <= 0) {
                int new_id = (int)sqlite3_last_insert_rowid(db_);
                item.id = new_id;
                active_ids.push_back(new_id);
            }

            sqlite3_reset(curr_stmt);
            sqlite3_clear_bindings(curr_stmt);
        }

        // Prune 删除
        if (active_ids.empty()) {
            sqlite3_exec(db_, "DELETE FROM alert_channels;", nullptr, nullptr, nullptr);
        } else {
            std::stringstream ss;
            ss << "DELETE FROM alert_channels WHERE id NOT IN (";
            for (size_t i = 0; i < active_ids.size(); ++i) {
                ss << active_ids[i];
                if (i < active_ids.size() - 1) ss << ",";
            }
            ss << ");";
            rc = sqlite3_exec(db_, ss.str().c_str(), nullptr, nullptr, nullptr);
            if(rc != SQLITE_OK) throw std::runtime_error("Prune failed");
        }

        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
        checkSqliteError(db_, rc, "Failed to commit");
        // 更新缓存
        std::swap(cached_channels_, new_channels_cache);

    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }
}
AppConfig SqliteConfigRepository::getAppConfigInternal()
{
    AppConfig config;
    const char *sql = "select config_key,config_value from app_config;";
    sqlite3_stmt *stmt_ = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
    if (rc != SQLITE_OK)
    {
        checkSqliteError(db_, rc, "Failed to prepare statement");
    }
    StmtPtr stmt(stmt_);
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
    {
        const char *key_ptr = (const char *)sqlite3_column_text(stmt.get(), 0);
        const char *val_ptr = (const char *)sqlite3_column_text(stmt.get(), 1);

        if (!key_ptr || !val_ptr)
            continue;

        std::string key = key_ptr;
        std::string val = val_ptr;
        // 更加优雅的做法是map+lambda，但是if else更加实在，因为你的配置就几项，cpu速度计算更快
        try
        {
            ApplyConfigValue(config, key, val);
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Config] Error parsing key [" << key << "] with value [" << val << "]: " << e.what() << std::endl;
            // 捕获异常后继续循环，不要中断后续配置的加载
        }
        //
    }
    if (rc != SQLITE_DONE)
    {
        checkSqliteError(db_, rc, "Step execution failed during pagination");
    }
    return config;
}

std::vector<PromptConfig> SqliteConfigRepository::getAllPromptsInternal()
{
    std::vector<PromptConfig> prompts;
    // Updated SQL to include prompt_type
    const char *sql = "select id,name,content,is_active,prompt_type from ai_prompts;";
    sqlite3_stmt *stmt_ = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
    if (rc != SQLITE_OK)
    {
        checkSqliteError(db_, rc, "Failed to prepare statement for prompts");
    }
    StmtPtr stmt(stmt_);
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
    {
        int id = sqlite3_column_int(stmt.get(), 0);
        const char *name_ptr = (const char *)sqlite3_column_text(stmt.get(), 1);
        const char *content_ptr = (const char *)sqlite3_column_text(stmt.get(), 2);
        int is_active = sqlite3_column_int(stmt.get(), 3);
        const char *type_ptr = (const char *)sqlite3_column_text(stmt.get(), 4);

        bool active_flag = (is_active != 0);
        if (!name_ptr || !content_ptr)
            continue;
        std::string name = name_ptr;
        std::string content = content_ptr;
        std::string type = type_ptr ? type_ptr : "map"; // Default to map
        prompts.emplace_back(id, name, content, active_flag, type);
    }
    if (rc != SQLITE_DONE)
    {
        checkSqliteError(db_, rc, "Step execution failed during prompt loading");
    }
    return prompts;
}

std::vector<AlertChannel> SqliteConfigRepository::getAllChannelsInternal()
{
    std::vector<AlertChannel> alerts;
    const char *sql = "select id,name,provider,webhook_url,alert_threshold,msg_template,is_active from alert_channels;";
    sqlite3_stmt *stmt_ = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
    if (rc != SQLITE_OK)
    {
        checkSqliteError(db_, rc, "Failed to prepare statement for channels");
    }
    StmtPtr stmt(stmt_);
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
    {
        int id = sqlite3_column_int(stmt.get(), 0);
        const char *name_ptr = (const char *)sqlite3_column_text(stmt.get(), 1);
        const char *provider_ptr = (const char *)sqlite3_column_text(stmt.get(), 2);
        const char *webhook_url_ptr = (const char *)sqlite3_column_text(stmt.get(), 3);
        const char *alert_threshold_ptr = (const char *)sqlite3_column_text(stmt.get(), 4);
        const char *msg_template_ptr = (const char *)sqlite3_column_text(stmt.get(), 5);
        int is_active = sqlite3_column_int(stmt.get(), 6);
        bool active_flag = (is_active != 0);
        if (!name_ptr || !provider_ptr || !webhook_url_ptr || !alert_threshold_ptr)
            continue;
        std::string name = name_ptr;
        std::string provider = provider_ptr;
        std::string webhook_url = webhook_url_ptr;
        std::string alert_threshold = alert_threshold_ptr;
        // 模板可能是空的（允许为 NULL 或空字符串），视业务逻辑而定，这里假设也必须有值
        std::string msg_template = msg_template_ptr ? msg_template_ptr : "";
        alerts.emplace_back(id, name, provider, webhook_url, alert_threshold, msg_template, active_flag);
    }
    if (rc != SQLITE_DONE)
    {
        checkSqliteError(db_, rc, "Step execution failed during channels loading");
    }
    return alerts;
}

void SqliteConfigRepository::loadFromDbInternal()
{
    cached_app_config_ = std::move(getAppConfigInternal());
    cached_prompts_ = std::move(getAllPromptsInternal());
    cached_channels_ = std::move(getAllChannelsInternal());
    is_initialized_ = true;
}
