#include "SqliteConfigRepository.h"
#include <filesystem>
#include <iostream>
#include <sstream>
#include "persistence/SqliteHelper.h"

using namespace persistence;

// 配置表里的布尔值目前既可能来自旧库里的 0/1，也可能来自接口层传来的 true/false 字符串。
// 这里统一做最小宽容解析，避免存储层因为表现形式差异把同一个开关读成两种语义。
static bool IsTruthyConfigValue(const std::string& val)
{
    return val == "1" || val == "true" || val == "TRUE";
}

static std::vector<std::string> ParseTraceEndAliasConfigValue(const std::string& raw_json)
{
    std::vector<std::string> aliases;
    const nlohmann::json alias_json = nlohmann::json::parse(raw_json, nullptr, false);
    // 别名字符串现在只出现在“控制面写配置”这条低频路径。
    // 这里解析失败直接回空数组，避免旧前端、半迁移数据或手工测试值把整次设置保存直接打断。
    if (alias_json.is_discarded() || !alias_json.is_array()) {
        return aliases;
    }
    for (const auto& item : alias_json) {
        if (!item.is_string()) {
            continue;
        }
        aliases.push_back(item.get<std::string>());
    }
    return aliases;
}

static std::vector<std::string> FilterTraceEndAliases(const std::vector<std::string>& raw_aliases,
                                                      const std::string& primary_field)
{
    std::vector<std::string> filtered;
    for (const std::string& alias : raw_aliases) {
        if (alias.empty()) {
            continue;
        }
        // 去重交给前端控件做，这里只保留最小语义处理：
        // 别名如果和主字段同名，就属于自相矛盾配置，直接在 Repository 边界过滤掉。
        if (!primary_field.empty() && alias == primary_field) {
            continue;
        }
        filtered.push_back(alias);
    }
    return filtered;
}

static void ReplaceTraceEndAliases(sqlite3* db, const std::vector<std::string>& aliases)
{
    int rc = sqlite3_exec(db, "DELETE FROM trace_end_aliases;", nullptr, nullptr, nullptr);
    checkSqliteError(db, rc, "Failed to clear trace_end_aliases");

    const char* insert_sql = "INSERT INTO trace_end_aliases (position, alias) VALUES (?, ?);";
    sqlite3_stmt* stmt_raw = nullptr;
    rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt_raw, nullptr);
    checkSqliteError(db, rc, "Failed to prepare trace_end_aliases insert");
    StmtPtr stmt(stmt_raw);

    for (size_t i = 0; i < aliases.size(); ++i) {
        sqlite3_bind_int(stmt.get(), 1, static_cast<int>(i));
        sqlite3_bind_text(stmt.get(), 2, aliases[i].c_str(), -1, SQLITE_STATIC);
        const int step_rc = sqlite3_step(stmt.get());
        if (step_rc != SQLITE_DONE) {
            checkSqliteError(db, step_rc, "Failed to write trace_end_aliases");
        }
        sqlite3_reset(stmt.get());
        sqlite3_clear_bindings(stmt.get());
    }
}

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
        else if (key == "http_port") config.http_port = std::stoi(val);
        else if (key == "log_retention_days") config.log_retention_days = std::stoi(val);
        else if (key == "ai_retry_enabled") config.ai_retry_enabled = IsTruthyConfigValue(val);
        else if (key == "ai_retry_max_attempts") config.ai_retry_max_attempts = std::stoi(val);
        else if (key == "ai_auto_degrade") config.ai_auto_degrade = IsTruthyConfigValue(val);
        else if (key == "ai_fallback_provider") config.ai_fallback_provider = val;
        else if (key == "ai_fallback_model") config.ai_fallback_model = val;
        // fallback key 单独存，是为了允许“主模型”和“降级模型”走不同供应商或不同额度。
        // 如果这里继续偷懒复用 ai_api_key，那么真正触发降级时反而可能因为凭证不匹配再次失败。
        else if (key == "ai_fallback_api_key") config.ai_fallback_api_key = val;
        else if (key == "ai_circuit_breaker") config.ai_circuit_breaker = IsTruthyConfigValue(val);
        else if (key == "ai_failure_threshold") config.ai_failure_threshold = std::stoi(val);
        else if (key == "ai_cooldown_seconds") config.ai_cooldown_seconds = std::stoi(val);
        else if (key == "active_prompt_id") config.active_prompt_id = std::stoi(val);
        else if (key == "kernel_worker_threads") config.kernel_worker_threads = std::stoi(val);
        else if (key == "trace_end_field") config.trace_end_field = val;
        else if (key == "token_limit") config.token_limit = std::stoi(val);
        else if (key == "span_capacity") config.span_capacity = std::stoi(val);
        else if (key == "collecting_idle_timeout_ms") config.collecting_idle_timeout_ms = std::stoi(val);
        else if (key == "sealed_grace_window_ms") config.sealed_grace_window_ms = std::stoi(val);
        else if (key == "retry_base_delay_ms") config.retry_base_delay_ms = std::stoi(val);
        else if (key == "sweep_tick_ms") config.sweep_tick_ms = std::stoi(val);
        else if (key == "wm_active_sessions_overload") config.wm_active_sessions_overload = std::stoi(val);
        else if (key == "wm_active_sessions_critical") config.wm_active_sessions_critical = std::stoi(val);
        else if (key == "wm_buffered_spans_overload") config.wm_buffered_spans_overload = std::stoi(val);
        else if (key == "wm_buffered_spans_critical") config.wm_buffered_spans_critical = std::stoi(val);
        else if (key == "wm_pending_tasks_overload") config.wm_pending_tasks_overload = std::stoi(val);
        else if (key == "wm_pending_tasks_critical") config.wm_pending_tasks_critical = std::stoi(val);
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

        // 这一刀继续收口 Settings 的持久化契约，不在这里做整套配置中心重构：
        // 1. app_config 继续走 KV，便于后面继续增删 key；
        // 2. trace_end_aliases 从 app_config 里拆出来单独落表，避免 /logs/spans 热路径反复 JSON 解析；
        // 2. prompts 保持单表不扩字段，避免把还没钉死的 prompt 语义提前写死；
        // 3. alert_channels 改成最小真实外发字段集，去掉 msg_template，补上飞书签名 secret。
        // 当前仍假设这是测试阶段的新库或可重建库，所以这里不额外补旧 schema 迁移逻辑。
        const char *init_sql = R"(
            CREATE TABLE IF NOT EXISTS app_config (
                config_key TEXT PRIMARY KEY,
                config_value TEXT,
                description TEXT,
                updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );
            CREATE TABLE IF NOT EXISTS prompts (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                content TEXT NOT NULL,
                is_active INTEGER DEFAULT 1,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );
            CREATE TABLE IF NOT EXISTS trace_end_aliases (
                position INTEGER PRIMARY KEY,
                alias TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS alert_channels (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                provider TEXT NOT NULL,
                webhook_url TEXT NOT NULL,
                secret TEXT DEFAULT '',
                alert_threshold TEXT NOT NULL,
                is_active INTEGER DEFAULT 0,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );
            INSERT OR IGNORE INTO app_config (config_key, config_value, description) VALUES 
            ('ai_provider', 'mock', 'AI服务商类型'),
            ('ai_model', 'gpt-4-turbo', '模型名称'),
            ('ai_api_key', '', 'API密钥'),
            ('ai_language', 'en', '解析语言'),
            ('app_language', 'en', '界面语言'),
            ('http_port', '8080', 'HTTP服务端口'),
            ('log_retention_days', '7', '日志保留天数'),
            ('ai_retry_enabled', '0', 'AI自动重试开关'),
            ('ai_retry_max_attempts', '3', 'AI最大重试次数'),
            ('ai_auto_degrade', '0', '自动降级开关'),
            ('ai_fallback_provider', 'mock', '降级服务商类型'),
            ('ai_fallback_model', 'mock', '降级模型名称'),
            ('ai_fallback_api_key', '', '降级模型 API 密钥'),
            ('ai_circuit_breaker', '1', '熔断机制开关'),
            ('ai_failure_threshold', '5', '熔断触发阈值'),
            ('ai_cooldown_seconds', '60', '熔断冷却时间s'),
            ('active_prompt_id', '0', '当前激活的PromptID'),
            ('kernel_worker_threads', '4', '工作线程数'),
            ('trace_end_field', 'trace_end', '顶层 trace 结束标记字段名'),
            ('token_limit', '0', '单条 trace 的 token 保护阈值'),
            ('span_capacity', '100', '单条 trace 的 span 数量保护阈值'),
            ('collecting_idle_timeout_ms', '5000', '收集阶段空闲超时'),
            ('sealed_grace_window_ms', '1000', '封口后的乱序缓冲窗'),
            ('retry_base_delay_ms', '500', 'dispatch 重试的起始等待时间'),
            ('sweep_tick_ms', '500', '状态机 tick 推进频率'),
            ('wm_active_sessions_overload', '75', 'active sessions overload 百分比阈值'),
            ('wm_active_sessions_critical', '90', 'active sessions critical 百分比阈值'),
            ('wm_buffered_spans_overload', '75', 'buffered spans overload 百分比阈值'),
            ('wm_buffered_spans_critical', '90', 'buffered spans critical 百分比阈值'),
            ('wm_pending_tasks_overload', '75', 'pending tasks overload 百分比阈值'),
            ('wm_pending_tasks_critical', '90', 'pending tasks critical 百分比阈值');
            DELETE FROM app_config
            WHERE config_key IN (
                'max_disk_usage_gb',
                'kernel_adaptive_mode',
                'kernel_max_batch',
                'kernel_refresh_interval',
                'kernel_io_buffer',
                'trace_end_aliases'
            );
        )";

        rc = sqlite3_exec(db_, init_sql, nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK) {
            std::string err_str = errmsg ? errmsg : "Unknown error";
            sqlite3_free(errmsg);
            throw std::runtime_error("Failed to Create Tables: " + err_str);
        }

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
    return getSnapshot()->app_config;
}

std::vector<PromptConfig> SqliteConfigRepository::getAllPrompts()
{
    return getSnapshot()->prompts;
}

std::vector<AlertChannel> SqliteConfigRepository::getAllChannels()
{
    return getSnapshot()->channels;
}

AllSettings SqliteConfigRepository::getAllSettings()
{
    // 组装 AllSettings，Prompt 列表已是统一单表
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
    const auto trace_end_field_it = mp.find("trace_end_field");
    const auto trace_end_aliases_it = mp.find("trace_end_aliases");
    const bool trace_end_field_updated = trace_end_field_it != mp.end();
    const bool trace_end_aliases_updated = trace_end_aliases_it != mp.end();
    const std::string effective_trace_end_field =
        trace_end_field_updated ? trace_end_field_it->second : configClone.trace_end_field;
    std::vector<std::string> effective_trace_end_aliases = configClone.trace_end_aliases;
    if (trace_end_aliases_updated) {
        // 别名字符串现在只允许在 Repository 入口解析一次。
        // 这样做完后，快照里拿到的就是已经过滤过主字段冲突的数组，后面的 LogHandler 热路径只读现成 vector。
        effective_trace_end_aliases = FilterTraceEndAliases(
            ParseTraceEndAliasConfigValue(trace_end_aliases_it->second),
            effective_trace_end_field);
    } else if (trace_end_field_updated) {
        // 主字段变了但别名列表没显式改时，仍然要把“和主字段撞名”的那一项剔掉，避免配置自己打自己。
        effective_trace_end_aliases = FilterTraceEndAliases(configClone.trace_end_aliases,
                                                            effective_trace_end_field);
    }

    // config 现在按“只更新改动 key”保存，所以这里直接做按 key upsert。
    // 这样即使后面新增 key、或者旧测试库里暂时缺了一条 seed，也不会因为 update 命中 0 行而静默丢配置。
    const char *sql = R"(
        INSERT INTO app_config (config_key, config_value, description, updated_at)
        VALUES (?, ?, '', CURRENT_TIMESTAMP)
        ON CONFLICT(config_key) DO UPDATE SET
            config_value = excluded.config_value,
            updated_at = CURRENT_TIMESTAMP;
    )";
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
            if (key == "trace_end_aliases") {
                continue;
            }
            const std::string& effective_val = val;
            sqlite3_bind_text(stmt.get(), 1, key.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt.get(), 2, effective_val.c_str(), -1, SQLITE_STATIC);

            const int step_rc = sqlite3_step(stmt.get());
            if (step_rc != SQLITE_DONE) {
                checkSqliteError(db_, step_rc, "Failed to execute updateAppConfig");
            }

            ApplyConfigValue(configClone, key, effective_val);
            sqlite3_reset(stmt.get());
            sqlite3_clear_bindings(stmt.get());
        }
        if (trace_end_field_updated || trace_end_aliases_updated) {
            ReplaceTraceEndAliases(db_, effective_trace_end_aliases);
            configClone.trace_end_aliases = effective_trace_end_aliases;
        }

        // 更新快照
        auto new_snap = std::make_shared<SystemConfig>(
            std::move(configClone),
            old_snap->prompts,
            old_snap->channels
        );

        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            current_snapshot_ = new_snap;
        }

        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
        checkSqliteError(db_, rc, "Failed to commit transaction");
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

    auto new_prompts = prompts_input;

    const char* sql_insert = "INSERT INTO prompts (name, content, is_active) VALUES (?, ?, ?);";
    const char* sql_update = "UPDATE prompts SET name=?, content=?, is_active=? WHERE id=?;";

    int rc = SQLITE_OK;
    try {
        rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        checkSqliteError(db_, rc, "Failed to begin transaction for prompts");

        sqlite3_stmt* stmt_insert_raw = nullptr;
        rc = sqlite3_prepare_v2(db_, sql_insert, -1, &stmt_insert_raw, nullptr);
        checkSqliteError(db_, rc, "Prep Insert Failed for prompts");
        StmtPtr stmt_insert(stmt_insert_raw);

        sqlite3_stmt* stmt_update_raw = nullptr;
        rc = sqlite3_prepare_v2(db_, sql_update, -1, &stmt_update_raw, nullptr);
        checkSqliteError(db_, rc, "Prep Update Failed for prompts");
        StmtPtr stmt_update(stmt_update_raw);

        std::vector<int> active_ids;

        for (auto& item : new_prompts) {
            sqlite3_stmt* curr_stmt = nullptr;

            if (item.id > 0) {
                curr_stmt = stmt_update.get();
                sqlite3_bind_text(curr_stmt, 1, item.name.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(curr_stmt, 2, item.content.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int(curr_stmt, 3, item.is_active ? 1 : 0);
                sqlite3_bind_int(curr_stmt, 4, item.id);
                active_ids.push_back(item.id);
            } else {
                curr_stmt = stmt_insert.get();
                sqlite3_bind_text(curr_stmt, 1, item.name.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(curr_stmt, 2, item.content.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int(curr_stmt, 3, item.is_active ? 1 : 0);
            }

            if (sqlite3_step(curr_stmt) != SQLITE_DONE) {
                throw std::runtime_error("Upsert step failed for prompts");
            }

            if (item.id <= 0) {
                int new_id = (int)sqlite3_last_insert_rowid(db_);
                item.id = new_id;
                active_ids.push_back(new_id);
            }

            sqlite3_reset(curr_stmt);
            sqlite3_clear_bindings(curr_stmt);
        }

        // 删除不在列表中的项
        if (active_ids.empty()) {
            sqlite3_exec(db_, "DELETE FROM prompts;", nullptr, nullptr, nullptr);
        } else {
            std::stringstream ss;
            ss << "DELETE FROM prompts WHERE id NOT IN (";
            for (size_t i = 0; i < active_ids.size(); ++i) {
                ss << active_ids[i];
                if (i < active_ids.size() - 1) ss << ",";
            }
            ss << ");";
            rc = sqlite3_exec(db_, ss.str().c_str(), nullptr, nullptr, nullptr);
            checkSqliteError(db_, rc, "Prune failed for prompts");
        }

        // 更新快照
        auto new_snap = std::make_shared<SystemConfig>(
            old_snap->app_config,
            std::move(new_prompts),
            old_snap->channels
        );

        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            current_snapshot_ = new_snap;
        }

        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
        checkSqliteError(db_, rc, "Failed to commit transaction for prompts");
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

    sqlite3_stmt* stmt_insert = nullptr;
    sqlite3_stmt* stmt_update = nullptr;

    const char* sql_insert = "INSERT INTO alert_channels (name, provider, webhook_url, secret, alert_threshold, is_active) VALUES (?, ?, ?, ?, ?, ?);";
    const char* sql_update = "UPDATE alert_channels SET name=?, provider=?, webhook_url=?, secret=?, alert_threshold=?, is_active=? WHERE id=?;";

    int rc = SQLITE_OK;
    try {
        rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        checkSqliteError(db_, rc, "Failed to begin transaction for channels");

        rc = sqlite3_prepare_v2(db_, sql_insert, -1, &stmt_insert, nullptr);
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
            sqlite3_bind_text(curr_stmt, col_idx++, item.secret.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(curr_stmt, col_idx++, item.alert_threshold.c_str(), -1, SQLITE_STATIC);
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
            old_snap->prompts,
            std::move(new_channels_cache)
        );

        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            current_snapshot_ = new_snap;
        }

        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
        checkSqliteError(db_, rc, "Failed to commit transaction for channels");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }
}

// 内部加载器 (单表 prompts)
std::vector<PromptConfig> SqliteConfigRepository::getAllPromptsInternal()
{
    std::vector<PromptConfig> prompts;

    const char *sql = "select id,name,content,is_active from prompts;";
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
            prompts.emplace_back(id, name, content, active_flag);
        }
    }

    return prompts;
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

std::vector<std::string> SqliteConfigRepository::getTraceEndAliasesInternal()
{
    std::vector<std::string> aliases;
    // 单独按 position 读回是为了保住用户在 Settings 里设置的别名优先级顺序。
    // 解析层虽然现在是“命中第一个别名就停”，但这个顺序仍然属于配置语义，不能在存储层丢掉。
    const char* sql = "select alias from trace_end_aliases order by position asc;";
    sqlite3_stmt* stmt_ = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
    if (rc != SQLITE_OK) checkSqliteError(db_, rc, "Failed to prepare statement for trace_end_aliases");
    StmtPtr stmt(stmt_);
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
    {
        const char* alias_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
        if (!alias_ptr) {
            continue;
        }
        aliases.emplace_back(alias_ptr);
    }
    return aliases;
}

std::vector<AlertChannel> SqliteConfigRepository::getAllChannelsInternal()
{
    std::vector<AlertChannel> alerts;
    const char *sql = "select id,name,provider,webhook_url,secret,alert_threshold,is_active from alert_channels;";
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
        const char *secret_ptr = (const char *)sqlite3_column_text(stmt.get(), 4);
        const char *alert_threshold_ptr = (const char *)sqlite3_column_text(stmt.get(), 5);
        int is_active = sqlite3_column_int(stmt.get(), 6);
        bool active_flag = (is_active != 0);
        if (!name_ptr || !provider_ptr || !webhook_url_ptr || !alert_threshold_ptr) continue;
        std::string name = name_ptr;
        std::string provider = provider_ptr;
        std::string webhook_url = webhook_url_ptr;
        std::string secret = secret_ptr ? secret_ptr : "";
        std::string alert_threshold = alert_threshold_ptr;
        alerts.emplace_back(id, name, provider, webhook_url, secret, alert_threshold, active_flag);
    }
    return alerts;
}

void SqliteConfigRepository::loadFromDbInternal()
{
    auto app = getAppConfigInternal();
    // DB 里读回来的别名也只做最小过滤，不再在后端兜底去重。
    // 这一轮我们明确把“避免重复别名”收回到前端控件状态处理。
    app.trace_end_aliases = FilterTraceEndAliases(getTraceEndAliasesInternal(), app.trace_end_field);
    auto prompts = getAllPromptsInternal();
    auto channels = getAllChannelsInternal();

    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    current_snapshot_ = std::make_shared<SystemConfig>(
        std::move(app),
        std::move(prompts),
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
