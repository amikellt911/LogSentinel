#include "SqliteConfigRepository.h"
#include <filesystem>
#include <iostream>
#include "persistence/SqliteHelper.h"

using namespace persistence;

// 辅助函数：将 DB 字符串值应用到 AppConfig 结构体
static void ApplyConfigValue(AppConfig &config, const std::string &key, const std::string &val)
{
    try
    {
        if (key == "ai_provider") config.ai_provider = val;
        else if (key == "ai_model") config.ai_model = val;
        else if (key == "ai_api_key") config.ai_api_key = val;
        else if (key == "ai_language") config.ai_language = val;
        else if (key == "app_language") config.app_language = val;
        else if (key == "kernel_io_buffer") config.kernel_io_buffer = val;
        else if (key == "kernel_worker_threads") config.kernel_worker_threads = std::stoi(val);
        else if (key == "kernel_max_batch") config.kernel_max_batch = std::stoi(val);
        else if (key == "kernel_refresh_interval") config.kernel_refresh_interval = std::stoi(val);
        else if (key == "log_retention_days") config.log_retention_days = std::stoi(val);
        else if (key == "max_disk_usage_gb") config.max_disk_usage_gb = std::stoi(val);
        else if (key == "http_port") config.http_port = std::stoi(val);
        else if (key == "ai_failure_threshold") config.ai_failure_threshold = std::stoi(val);
        else if (key == "ai_cooldown_seconds") config.ai_cooldown_seconds = std::stoi(val);
        else if (key == "active_map_prompt_id") config.active_map_prompt_id = std::stoi(val);
        else if (key == "active_reduce_prompt_id") config.active_reduce_prompt_id = std::stoi(val);
        else if (key == "ai_fallback_model") config.ai_fallback_model = val;
        else if (key == "kernel_adaptive_mode") config.kernel_adaptive_mode = (val == "1" || val == "true" || val == "TRUE");
        else if (key == "ai_auto_degrade") config.ai_auto_degrade = (val == "1" || val == "true" || val == "TRUE");
        else if (key == "ai_circuit_breaker") config.ai_circuit_breaker = (val == "1" || val == "true" || val == "TRUE");
    }
    catch (const std::exception &e)
    {
        std::cerr << "[ConfigHelper] Parse Error: Key [" << key << "] Value [" << val << "] - " << e.what() << std::endl;
    }
}

SqliteConfigRepository::SqliteConfigRepository(const std::string &db_path)
{
    // 1. 路径处理
    std::string final_path;
    if (db_path.find('/') != std::string::npos || db_path.find('\\') != std::string::npos)
    {
        final_path = db_path;
    }
    else
    {
        std::string data_path = "../persistence/data/";
        if (!std::filesystem::exists(data_path))
        {
            std::error_code ec;
            std::filesystem::create_directories(data_path, ec);
            if (ec) throw std::runtime_error("Failed to create directory: " + data_path);
        }
        final_path = data_path + db_path;
    }

    // 2. 打开数据库
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc = sqlite3_open_v2(final_path.c_str(), &db_, flags, nullptr);
    if (rc != SQLITE_OK)
    {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_close_v2(db_);
        db_ = nullptr;
        throw std::runtime_error("Cannot open database: " + err);
    }

    try
    {
        char *errmsg = nullptr;
        // 启用 WAL 模式以提高并发性能
        const char *sql_wal = "PRAGMA journal_mode=WAL;";
        rc = sqlite3_exec(db_, sql_wal, nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK) {
            std::cerr << "[WARN] Cannot set WAL mode: " << (errmsg ? errmsg : "unknown") << std::endl;
            sqlite3_free(errmsg);
        }

        rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) throw std::runtime_error("Failed to begin transaction");

        // 初始化 SQL：重命名 ai_prompts -> map_prompts, 新增 reduce_prompts
        // 注意：SQLite 不支持 DROP COLUMN，所以我们只需确保表名正确
        // 如果旧表 ai_prompts 存在，我们将在迁移步骤重命名它
        const char *init_sql = R"(
            CREATE TABLE IF NOT EXISTS app_config (
                config_key TEXT PRIMARY KEY,
                config_value TEXT,
                description TEXT,
                updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );
            /* 将在迁移逻辑中重命名 ai_prompts -> map_prompts，这里定义 map_prompts 以防是全新库 */
            CREATE TABLE IF NOT EXISTS map_prompts (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL UNIQUE,
                content TEXT NOT NULL,
                is_active INTEGER DEFAULT 1,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );
            CREATE TABLE IF NOT EXISTS reduce_prompts (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL UNIQUE,
                content TEXT NOT NULL,
                is_active INTEGER DEFAULT 1,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );
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
            INSERT OR IGNORE INTO app_config (config_key, config_value, description) VALUES 
            ('ai_provider', 'openai', 'AI服务商类型'),
            ('ai_model', 'gpt-4-turbo', '模型名称'),
            ('ai_api_key', '', 'API密钥'),
            ('ai_language', 'English', '解析语言'),
            ('app_language', 'en', '界面语言'),
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
        if (rc != SQLITE_OK) {
            std::string err_str = errmsg ? errmsg : "Unknown error";
            sqlite3_free(errmsg);
            throw std::runtime_error("Failed to Create Tables: " + err_str);
        }

        // 移除了 ai_prompts 迁移逻辑，由用户手动清理数据库文件

        loadFromDbInternal();
        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) throw std::runtime_error("Failed to commit transaction");
    }
    catch (...)
    {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_close_v2(db_);
        db_ = nullptr;
        throw;
    }
}

SqliteConfigRepository::~SqliteConfigRepository()
{
    if (db_) sqlite3_close_v2(db_);
}

SystemConfigPtr SqliteConfigRepository::getSnapshot()
{
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    return current_snapshot_;
}

AppConfig SqliteConfigRepository::getAppConfig()
{
    // 需要对 API 返回的 Config 做调整：Reduce ID 加上偏移量
    AppConfig cfg = getSnapshot()->app_config;
    if (cfg.active_reduce_prompt_id > 0) {
        cfg.active_reduce_prompt_id += REDUCE_ID_OFFSET;
    }
    return cfg;
}

std::vector<PromptConfig> SqliteConfigRepository::getAllPrompts()
{
    // 合并 Map 和 Reduce 的 Prompt 列表，Reduce 的 ID 加上偏移量
    auto snap = getSnapshot();
    std::vector<PromptConfig> merged;
    merged.reserve(snap->map_prompts.size() + snap->reduce_prompts.size());

    for (const auto& p : snap->map_prompts) {
        PromptConfig cp = p;
        cp.type = "map"; // 显式标记
        merged.push_back(cp);
    }

    for (const auto& p : snap->reduce_prompts) {
        PromptConfig cp = p;
        cp.id += REDUCE_ID_OFFSET; // 加上偏移量
        cp.type = "reduce"; // 显式标记
        merged.push_back(cp);
    }

    return merged;
}

std::vector<AlertChannel> SqliteConfigRepository::getAllChannels()
{
    return getSnapshot()->channels;
}

AllSettings SqliteConfigRepository::getAllSettings()
{
    // 组装 AllSettings，注意调用 getAllPrompts 获取合并后的列表
    return AllSettings{getAppConfig(), getAllPrompts(), getAllChannels()};
}

void SqliteConfigRepository::handleUpdateAppConfig(const std::map<std::string, std::string> &mp)
{
    std::lock_guard<std::mutex> db_lock(db_write_mutex_);

    // 获取旧快照
    SystemConfigPtr old_snap;
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        old_snap = current_snapshot_;
    }

    // 基于旧配置准备新配置
    AppConfig configClone = old_snap->app_config;

    const char *sql = "update app_config set config_value=? where config_key=?;";
    sqlite3_stmt *stmt_ = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    checkSqliteError(db_, rc, "Failed to begin transaction");

    rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        checkSqliteError(db_, rc, "Failed to prepare statement");
    }
    StmtPtr stmt(stmt_);

    try
    {
        for (auto &[key, val] : mp)
        {
            std::string effective_val = val;
            // 特殊处理：如果是 active_reduce_prompt_id，需要减去偏移量
            if (key == "active_reduce_prompt_id") {
                try {
                    int id_val = std::stoi(val);
                    if (id_val >= REDUCE_ID_OFFSET) {
                        id_val -= REDUCE_ID_OFFSET;
                        effective_val = std::to_string(id_val);
                    }
                } catch (...) {
                    // ignore parse error, let ApplyConfigValue handle or fail
                }
            }

            sqlite3_bind_text(stmt.get(), 1, effective_val.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt.get(), 2, key.c_str(), -1, SQLITE_STATIC);

            if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
                checkSqliteError(db_, rc, "Failed to execute updateAppConfig");
            }

            ApplyConfigValue(configClone, key, effective_val);
            sqlite3_reset(stmt.get());
        }

        // 更新快照
        auto new_snap = std::make_shared<SystemConfig>(
            std::move(configClone),
            old_snap->map_prompts,
            old_snap->reduce_prompts,
            old_snap->channels
        );

        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            current_snapshot_ = new_snap;
        }

        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    }
    catch (...)
    {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }
}

void SqliteConfigRepository::handleUpdatePrompt(const std::vector<PromptConfig>& prompts_input)
{
    std::lock_guard<std::mutex> db_lock(db_write_mutex_);

    SystemConfigPtr old_snap;
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        old_snap = current_snapshot_;
    }

    // 分离 Map 和 Reduce
    std::vector<PromptConfig> new_map_prompts;
    std::vector<PromptConfig> new_reduce_prompts;

    for (const auto& item : prompts_input) {
        if (item.type == "reduce") {
            PromptConfig p = item;
            // 如果是更新，减去偏移量还原 ID
            if (p.id >= REDUCE_ID_OFFSET) {
                p.id -= REDUCE_ID_OFFSET;
            }
            new_reduce_prompts.push_back(p);
        } else {
            // map or default
            new_map_prompts.push_back(item);
        }
    }

    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    // 通用处理 lambda
    auto processTable = [&](sqlite3* db, const std::string& tableName, std::vector<PromptConfig>& items) {
        std::string sql_ins_str = "INSERT INTO " + tableName + " (name, content, is_active) VALUES (?, ?, ?);";
        std::string sql_upd_str = "UPDATE " + tableName + " SET name=?, content=?, is_active=? WHERE id=?;";
        
        sqlite3_stmt* stmt_insert = nullptr;
        sqlite3_stmt* stmt_update = nullptr;

        int rc = sqlite3_prepare_v2(db, sql_ins_str.c_str(), -1, &stmt_insert, nullptr);
        if (rc != SQLITE_OK) throw std::runtime_error("Prep Insert Failed for " + tableName);

        rc = sqlite3_prepare_v2(db, sql_upd_str.c_str(), -1, &stmt_update, nullptr);
        if (rc != SQLITE_OK) throw std::runtime_error("Prep Update Failed for " + tableName);

        StmtPtr ptr_insert(stmt_insert);
        StmtPtr ptr_update(stmt_update);

        std::vector<int> active_ids;

        for (auto& item : items) {
            sqlite3_stmt* curr_stmt = nullptr;

            if (item.id > 0) {
                curr_stmt = stmt_update;
                sqlite3_bind_text(curr_stmt, 1, item.name.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(curr_stmt, 2, item.content.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int(curr_stmt, 3, item.is_active ? 1 : 0);
                sqlite3_bind_int(curr_stmt, 4, item.id);
                active_ids.push_back(item.id);
            } else {
                curr_stmt = stmt_insert;
                sqlite3_bind_text(curr_stmt, 1, item.name.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(curr_stmt, 2, item.content.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int(curr_stmt, 3, item.is_active ? 1 : 0);
            }

            if (sqlite3_step(curr_stmt) != SQLITE_DONE) {
                throw std::runtime_error("Upsert step failed for " + tableName);
            }

            if (item.id <= 0) {
                int new_id = (int)sqlite3_last_insert_rowid(db);
                item.id = new_id;
                active_ids.push_back(new_id);
            }

            sqlite3_reset(curr_stmt);
            sqlite3_clear_bindings(curr_stmt);
        }

        // 删除不在列表中的项
        if (active_ids.empty()) {
            std::string del_all = "DELETE FROM " + tableName + ";";
            sqlite3_exec(db, del_all.c_str(), nullptr, nullptr, nullptr);
        } else {
            std::stringstream ss;
            ss << "DELETE FROM " + tableName + " WHERE id NOT IN (";
            for (size_t i = 0; i < active_ids.size(); ++i) {
                ss << active_ids[i];
                if (i < active_ids.size() - 1) ss << ",";
            }
            ss << ");";
            int rc = sqlite3_exec(db, ss.str().c_str(), nullptr, nullptr, nullptr);
            checkSqliteError(db, rc, "Prune failed for " + tableName);
        }
    };

    try {
        processTable(db_, "map_prompts", new_map_prompts);
        processTable(db_, "reduce_prompts", new_reduce_prompts);

        // 更新快照
        auto new_snap = std::make_shared<SystemConfig>(
            old_snap->app_config,
            std::move(new_map_prompts),
            std::move(new_reduce_prompts),
            old_snap->channels
        );

        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            current_snapshot_ = new_snap;
        }

        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }
}

void SqliteConfigRepository::handleUpdateChannel(const std::vector<AlertChannel>& channels_input)
{
    std::lock_guard<std::mutex> db_lock(db_write_mutex_);

    SystemConfigPtr old_snap;
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        old_snap = current_snapshot_;
    }

    auto new_channels_cache = channels_input;

    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    sqlite3_stmt* stmt_insert = nullptr;
    sqlite3_stmt* stmt_update = nullptr;

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
            int col_idx = 1;

            if (item.id > 0) curr_stmt = stmt_update;
            else curr_stmt = stmt_insert;

            sqlite3_bind_text(curr_stmt, col_idx++, item.name.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(curr_stmt, col_idx++, item.provider.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(curr_stmt, col_idx++, item.webhook_url.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(curr_stmt, col_idx++, item.alert_threshold.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(curr_stmt, col_idx++, item.msg_template.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(curr_stmt, col_idx++, item.is_active ? 1 : 0);

            if (item.id > 0) {
                sqlite3_bind_int(curr_stmt, col_idx++, item.id);
                active_ids.push_back(item.id);
            }

            if (sqlite3_step(curr_stmt) != SQLITE_DONE) throw std::runtime_error("Upsert step failed");

            if (item.id <= 0) {
                int new_id = (int)sqlite3_last_insert_rowid(db_);
                item.id = new_id;
                active_ids.push_back(new_id);
            }

            sqlite3_reset(curr_stmt);
            sqlite3_clear_bindings(curr_stmt);
        }

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

        // 更新快照
        auto new_snap = std::make_shared<SystemConfig>(
            old_snap->app_config,
            old_snap->map_prompts,
            old_snap->reduce_prompts,
            std::move(new_channels_cache)
        );

        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            current_snapshot_ = new_snap;
        }

        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }
}

// 内部加载器 (返回 pair<MapPrompts, ReducePrompts>)
std::pair<std::vector<PromptConfig>, std::vector<PromptConfig>> SqliteConfigRepository::getAllPromptsInternal()
{
    std::vector<PromptConfig> map_prompts;
    std::vector<PromptConfig> reduce_prompts;

    // 加载 Map Prompts
    {
        const char *sql = "select id,name,content,is_active from map_prompts;";
        sqlite3_stmt *stmt_ = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
        if (rc == SQLITE_OK) {
            StmtPtr stmt(stmt_);
            while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
            {
                int id = sqlite3_column_int(stmt.get(), 0);
                const char *name_ptr = (const char *)sqlite3_column_text(stmt.get(), 1);
                const char *content_ptr = (const char *)sqlite3_column_text(stmt.get(), 2);
                int is_active = sqlite3_column_int(stmt.get(), 3);

                bool active_flag = (is_active != 0);
                if (!name_ptr || !content_ptr) continue;
                std::string name = name_ptr;
                std::string content = content_ptr;
                map_prompts.emplace_back(id, name, content, active_flag, "map");
            }
        }
    }

    // 加载 Reduce Prompts
    {
        const char *sql = "select id,name,content,is_active from reduce_prompts;";
        sqlite3_stmt *stmt_ = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
        if (rc == SQLITE_OK) {
            StmtPtr stmt(stmt_);
            while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
            {
                int id = sqlite3_column_int(stmt.get(), 0);
                const char *name_ptr = (const char *)sqlite3_column_text(stmt.get(), 1);
                const char *content_ptr = (const char *)sqlite3_column_text(stmt.get(), 2);
                int is_active = sqlite3_column_int(stmt.get(), 3);

                bool active_flag = (is_active != 0);
                if (!name_ptr || !content_ptr) continue;
                std::string name = name_ptr;
                std::string content = content_ptr;
                reduce_prompts.emplace_back(id, name, content, active_flag, "reduce");
            }
        }
    }

    return {map_prompts, reduce_prompts};
}

// 内部加载器 (保持原样)
AppConfig SqliteConfigRepository::getAppConfigInternal()
{
    AppConfig config;
    const char *sql = "select config_key,config_value from app_config;";
    sqlite3_stmt *stmt_ = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
    if (rc != SQLITE_OK) checkSqliteError(db_, rc, "Failed to prepare statement");
    StmtPtr stmt(stmt_);
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
    {
        const char *key_ptr = (const char *)sqlite3_column_text(stmt.get(), 0);
        const char *val_ptr = (const char *)sqlite3_column_text(stmt.get(), 1);
        if (!key_ptr || !val_ptr) continue;
        ApplyConfigValue(config, key_ptr, val_ptr);
    }
    return config;
}

std::vector<AlertChannel> SqliteConfigRepository::getAllChannelsInternal()
{
    std::vector<AlertChannel> alerts;
    const char *sql = "select id,name,provider,webhook_url,alert_threshold,msg_template,is_active from alert_channels;";
    sqlite3_stmt *stmt_ = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
    if (rc != SQLITE_OK) checkSqliteError(db_, rc, "Failed to prepare statement for channels");
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
        if (!name_ptr || !provider_ptr || !webhook_url_ptr || !alert_threshold_ptr) continue;
        std::string name = name_ptr;
        std::string provider = provider_ptr;
        std::string webhook_url = webhook_url_ptr;
        std::string alert_threshold = alert_threshold_ptr;
        std::string msg_template = msg_template_ptr ? msg_template_ptr : "";
        alerts.emplace_back(id, name, provider, webhook_url, alert_threshold, msg_template, active_flag);
    }
    return alerts;
}

void SqliteConfigRepository::loadFromDbInternal()
{
    auto app = getAppConfigInternal();
    auto [map_prompts, reduce_prompts] = getAllPromptsInternal();
    auto channels = getAllChannelsInternal();

    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    current_snapshot_ = std::make_shared<SystemConfig>(
        std::move(app),
        std::move(map_prompts),
        std::move(reduce_prompts),
        std::move(channels)
    );
}

// 已弃用 API
std::vector<std::string> SqliteConfigRepository::getActiveWebhookUrls()
{
    std::lock_guard<std::mutex> lock_(db_write_mutex_);
    std::vector<std::string> urls;
    // 简单实现，不再使用 webhook_configs 表，改用 channels
    // 但为了兼容，如果 webhook_configs 存在可以读取
    return urls;
}

void SqliteConfigRepository::addWebhookUrl(const std::string &url)
{
    // Deprecated, no-op or implement if needed for backward compat
}

void SqliteConfigRepository::deleteWebhookUrl(const std::string &url)
{
    // Deprecated
}
