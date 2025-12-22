#include "persistence/SqliteLogRepository.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include "SqliteLogRepository.h"
using namespace persistence;
SqliteLogRepository::SqliteLogRepository(const std::string &db_path)
{
    std::string final_path;
    // 特殊处理 :memory:
    if (db_path == ":memory:") {
        final_path = db_path;
    } 
    else if (db_path.find('/') != std::string::npos || db_path.find('\\') != std::string::npos) {
        final_path = db_path;
    } else {
        // 只有纯文件名才加上默认目录
        std::string data_path = "../persistence/data/";
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

    // 0. 主表: batch_summaries
    const char *sql_create_batch = R"(
    CREATE TABLE IF NOT EXISTS batch_summaries (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        global_summary TEXT,
        global_risk_level TEXT,
        key_patterns TEXT,
        total_logs INTEGER DEFAULT 0,
        cnt_critical INTEGER DEFAULT 0,
        cnt_error    INTEGER DEFAULT 0,
        cnt_warning  INTEGER DEFAULT 0,
        cnt_info     INTEGER DEFAULT 0,
        cnt_safe     INTEGER DEFAULT 0,
        cnt_unknown  INTEGER DEFAULT 0,
        processing_time_ms INTEGER DEFAULT 0,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    );
    )";

    errmsg = nullptr;
    rc = sqlite3_exec(db_, sql_create_batch, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
         std::cerr << "Cannot create batch_summaries: " << errmsg << std::endl;
         sqlite3_free(errmsg); errmsg = nullptr;
    }

    // 以前是 analysis_content TEXT, 现在拆开了
    // 更新：关联 batch_id，去掉 global_summary (移到主表)
    const char *sql_create_results = R"(
    CREATE TABLE IF NOT EXISTS analysis_results (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        trace_id TEXT NOT NULL UNIQUE,
        batch_id INTEGER,  -- 新增外键
        status TEXT NOT NULL,
        
        -- 核心字段拆分
        risk_level TEXT,  -- critical, error, warning, info, safe
        summary TEXT,
        root_cause TEXT,
        solution TEXT,
        
        -- 新增：响应耗时 (用于计算平均值,微秒)
        response_time_ms INTEGER DEFAULT 0,

        processed_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        FOREIGN KEY (trace_id) REFERENCES raw_logs(trace_id),
        FOREIGN KEY (batch_id) REFERENCES batch_summaries(id)
    )
)";
    errmsg = nullptr; // 重置 errmsg
    rc = sqlite3_exec(db_, sql_create_results, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK)
    {
        std::cerr << "Cannot create analysis_results table: " << errmsg << std::endl;
        sqlite3_free(errmsg);
        errmsg = nullptr;
        sqlite3_close_v2(db_);
        db_ = nullptr;
        throw std::runtime_error("Failed to create analysis_results table");
    }
    //权衡：虽然我们读写比大约会：1:100，写肯定更多，但是为了读历史日志是我们的核心功能，所以还是加上了。
    const char *sql_create_index = "CREATE INDEX IF NOT EXISTS idx_analysis_results_processed_at ON analysis_results(processed_at);";
    errmsg = nullptr;
    rc = sqlite3_exec(db_, sql_create_index, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK)
    {
        std::cerr << "Cannot create index on processed_at: " << errmsg << std::endl;
        sqlite3_free(errmsg);
        // 索引创建失败通常不是致命错误，可以记录日志但让程序继续运行
    }

    // 启动时构建内存快照
    rebuildStatsFromDb();
}

SqliteLogRepository::~SqliteLogRepository()
{
    if (db_)
        sqlite3_close_v2(db_);
}
void SqliteLogRepository::SaveRawLog(const std::string &trace_id, const std::string &raw_log)
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
            throw std::runtime_error("Failed to execute statement for insert raw_log " + std::string(sqlite3_errmsg(db_)));
        }
    }
    else
    {
        // 如果prepare失败，仍然是Nullptr
        // sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare stmt for raw_log" + std::string(sqlite3_errmsg(db_)));
    }
    sqlite3_finalize(stmt);
}

void SqliteLogRepository::saveAnalysisResult(const std::string &trace_id, const LogAnalysisResult &result, int response_time_ms, const std::string &status)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const char *sql = R"(
        INSERT INTO analysis_results 
        (trace_id, status, risk_level, summary, root_cause, solution, response_time_ms) 
        VALUES (?, ?, ?, ?, ?, ?, ?);
    )";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK)
    {
        // 转换 Enum 为 String
        nlohmann::json j_risk = result.risk_level;
        std::string risk_str = j_risk.get<std::string>();

        sqlite3_bind_text(stmt, 1, trace_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, risk_str.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, result.summary.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, result.root_cause.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, result.solution.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 7, response_time_ms);
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            sqlite3_finalize(stmt);
            throw std::runtime_error("Failed to execute statement for insert analysis_result " + std::string(sqlite3_errmsg(db_)));
        }
    }
    else
    {
        throw std::runtime_error("Failed to prepare stmt for analysis_result" + std::string(sqlite3_errmsg(db_)));
    }
    sqlite3_finalize(stmt);
}

void SqliteLogRepository::saveAnalysisResult(const std::string &trace_id, const std::string &result, const std::string &status)
{
    std::lock_guard<std::mutex> lock_(mutex_);
    const char *sql_insert_template = "insert into analysis_results(trace_id,analysis_content,status) values(?,?,?);";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql_insert_template, -1, &stmt, 0);
    if (rc == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, trace_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, result.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, status.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            sqlite3_finalize(stmt);
            throw std::runtime_error("Failed to execute statement for insert analysis_result " + std::string(sqlite3_errmsg(db_)));
        }
    }
    else
    {
        throw std::runtime_error("Failed to prepare stmt for analysis_result" + std::string(sqlite3_errmsg(db_)));
    }
    sqlite3_finalize(stmt);
}

void SqliteLogRepository::saveAnalysisResultError(const std::string &trace_id, const std::string &error_msg)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const char *sql = "INSERT INTO analysis_results (trace_id, status, summary) VALUES (?, 'FAILURE', ?);";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        throw std::runtime_error("Failed to prepare stmt for Error analysis result" + std::string(sqlite3_errmsg(db_)));
    }
    sqlite3_bind_text(stmt, 1, trace_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, error_msg.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to execute statement for insert analysis_result " + std::string(sqlite3_errmsg(db_)));
    }
    sqlite3_finalize(stmt);
}

std::optional<std::string> SqliteLogRepository::queryResultByTraceId(const std::string &trace_id)
{
    std::lock_guard<std::mutex> lock_(mutex_);
    const char *sql_select_template = "select analysis_content from analysis_results where trace_id=?;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql_select_template, -1, &stmt, 0);
    if (rc == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, trace_id.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW)
        {
            const unsigned char *result = sqlite3_column_text(stmt, 0);
            std::string result_str(reinterpret_cast<const char *>(result));
            sqlite3_finalize(stmt);
            return std::make_optional(result_str);
        }
        else if (rc == SQLITE_DONE)
        {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }
        else
        {
            sqlite3_finalize(stmt);
            throw std::runtime_error("Failed to execute statement for select analysis_result " + std::string(sqlite3_errmsg(db_)));
        }
    }
    else
    {
        throw std::runtime_error("Failed to prepare stmt for analysis_result" + std::string(sqlite3_errmsg(db_)));
    }
    return std::optional<std::string>();
}

std::optional<LogAnalysisResult> SqliteLogRepository::queryStructResultByTraceId(const std::string &trace_id)
{
    std::lock_guard<std::mutex> lock_(mutex_);
    const char *sql_select_template = "select risk_level,root_cause,solution,summary from analysis_results where trace_id = ?";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql_select_template, -1, &stmt, 0);
    if (rc == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, trace_id.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW)
        {
            LogAnalysisResult result;
            auto get_col = [&](int col)
            {
                const char *txt = (const char *)sqlite3_column_text(stmt, col);
                return txt ? std::string(txt) : "unknown";
            };

            // 反序列化 String -> Enum
            std::string risk_str = get_col(0);
            nlohmann::json j = risk_str;
            result.risk_level = j.get<RiskLevel>();

            result.root_cause = get_col(1);
            result.solution = get_col(2);
            result.summary = get_col(3);
            sqlite3_finalize(stmt);
            return std::make_optional(result);
        }
        else if (rc == SQLITE_DONE)
        {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }
        else
        {
            sqlite3_finalize(stmt);
            throw std::runtime_error("Failed to execute statement for select analysis_result " + std::string(sqlite3_errmsg(db_)));
        }
    }
    else
    {
        throw std::runtime_error("Failed to prepare stmt for analysis_result" + std::string(sqlite3_errmsg(db_)));
    }
    return std::optional<LogAnalysisResult>();
}

void SqliteLogRepository::rebuildStatsFromDb() {
    std::lock_guard<std::mutex> lock(mutex_); // 锁 DB

    auto stats = std::make_shared<DashboardStats>();

    // 1. 统计累加计数 (从 batch_summaries 聚合，极快)
    // 如果没有 batch_summaries 数据，会返回 NULL，coalesce 转为 0
    const char* sql = R"(
        SELECT
            COALESCE(SUM(total_logs), 0),
            COALESCE(SUM(cnt_critical), 0),
            COALESCE(SUM(cnt_error), 0),
            COALESCE(SUM(cnt_warning), 0),
            COALESCE(SUM(cnt_info), 0),
            COALESCE(SUM(cnt_safe), 0),
            COALESCE(SUM(cnt_unknown), 0)
        FROM batch_summaries;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats->total_logs = sqlite3_column_int64(stmt, 0);
            stats->critical_risk = sqlite3_column_int64(stmt, 1);
            stats->error_risk = sqlite3_column_int64(stmt, 2);
            stats->warning_risk = sqlite3_column_int64(stmt, 3);
            stats->info_risk = sqlite3_column_int64(stmt, 4);
            stats->safe_risk = sqlite3_column_int64(stmt, 5);
            stats->unknown_risk = sqlite3_column_int64(stmt, 6);
        }
    }
    sqlite3_finalize(stmt);

    // 2. 加载最近警报 (查 analysis_results)
    // 注意：这里还是得查一次 DB
    const char* sql_alerts = R"(
        SELECT trace_id, summary, processed_at 
        FROM analysis_results 
        WHERE risk_level IN ('critical', 'high')
        ORDER BY id DESC 
        LIMIT 5;
    )";
    if (sqlite3_prepare_v2(db_, sql_alerts, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AlertInfo alert;
            alert.trace_id = (const char*)sqlite3_column_text(stmt, 0);
            alert.summary = (const char*)sqlite3_column_text(stmt, 1) ? (const char*)sqlite3_column_text(stmt, 1) : "";
            alert.time = (const char*)sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "";
            stats->recent_alerts.push_back(alert);
        }
    }
    sqlite3_finalize(stmt);

    // 更新原子指针
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    cached_stats_ = stats;
}

DashboardStats SqliteLogRepository::getDashboardStats()
{
    // 直接返回内存快照，O(1), 无 DB IO
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (!cached_stats_) {
        return DashboardStats();
    }
    return *cached_stats_;
}

void SqliteLogRepository::updateRealtimeMetrics(double qps, double backpressure) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (!cached_stats_) return;

    // COW: Copy On Write
    auto new_stats = std::make_shared<DashboardStats>(*cached_stats_);
    new_stats->current_qps = qps;
    new_stats->backpressure = backpressure; // 注意 DashboardStats 里的字段名

    cached_stats_ = new_stats;
}

int64_t SqliteLogRepository::saveBatchSummary(
    const std::string& global_summary,
    const std::string& global_risk_level,
    const std::string& key_patterns,
    const DashboardStats& batch_stats,
    int processing_time_ms
) {
    std::lock_guard<std::mutex> lock_(mutex_);

    const char* sql = R"(
        INSERT INTO batch_summaries
        (global_summary, global_risk_level, key_patterns,
         total_logs, cnt_critical, cnt_error, cnt_warning, cnt_info, cnt_safe, cnt_unknown,
         processing_time_ms)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        checkSqliteError(db_, rc, "Failed to prepare saveBatchSummary");
    }

    StmtPtr stmt_ptr(stmt);

    // 优化：参数是 const std::string&，在函数执行期间有效，所以可以用 SQLITE_STATIC 避免 SQLite 内部拷贝
    sqlite3_bind_text(stmt, 1, global_summary.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, global_risk_level.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, key_patterns.c_str(), -1, SQLITE_STATIC);

    sqlite3_bind_int64(stmt, 4, batch_stats.total_logs);
    sqlite3_bind_int64(stmt, 5, batch_stats.critical_risk);
    sqlite3_bind_int64(stmt, 6, batch_stats.error_risk);
    sqlite3_bind_int64(stmt, 7, batch_stats.warning_risk);
    sqlite3_bind_int64(stmt, 8, batch_stats.info_risk);
    sqlite3_bind_int64(stmt, 9, batch_stats.safe_risk);
    sqlite3_bind_int64(stmt, 10, batch_stats.unknown_risk);

    sqlite3_bind_int(stmt, 11, processing_time_ms);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        checkSqliteError(db_, SQLITE_ERROR, "Failed to insert batch summary");
    }

    return sqlite3_last_insert_rowid(db_);
}

void SqliteLogRepository::saveRawLogBatch(const std::vector<std::pair<std::string, std::string>> &logs)
{
    std::lock_guard<std::mutex> lock_(mutex_);

    // 1. 开启事务 (利用 helper 函数检查错误)
    int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    checkSqliteError(db_, rc, "Failed to begin transaction");

    // 2. 准备语句
    const char *sql = "INSERT INTO raw_logs(trace_id, log_content) VALUES(?, ?);";
    sqlite3_stmt *raw_stmt = nullptr;
    rc = sqlite3_prepare_v2(db_, sql, -1, &raw_stmt, nullptr);

    // 如果 prepare 失败，先回滚再抛异常
    if (rc != SQLITE_OK)
    {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        checkSqliteError(db_, rc, "Failed to prepare statement");
    }

    // 【接管】让智能指针接管，从此以后不用写 finalize
    StmtPtr stmt(raw_stmt);

    try
    {
        for (const auto &[trace_id, raw_log] : logs)
        {
            // 绑定参数
            // SQLITE_TRANSIENT会拷贝一份
            // SQLITE_STATIC确定logs生命周期更长，不会拷贝，减少开销
            sqlite3_bind_text(stmt.get(), 1, trace_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt.get(), 2, raw_log.c_str(), -1, SQLITE_STATIC);

            // 执行
            if (sqlite3_step(stmt.get()) != SQLITE_DONE)
            {
                // 主动抛出异常，让 catch 块去回滚
                checkSqliteError(db_, SQLITE_ERROR, "Step execution failed");
            }

            // 重置
            sqlite3_reset(stmt.get());
        }

        // 3. 提交事务
        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
        checkSqliteError(db_, rc, "Failed to commit");
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
void SqliteLogRepository::saveAnalysisResultBatch(const std::vector<AnalysisResultItem> &items, int64_t batch_id)
{
    // A. 准备当前批次的统计数据 (内存计算, 不持锁)
    auto current_batch_stats = std::make_shared<DashboardStats>();
    current_batch_stats->total_logs = items.size();

    for (const auto& item : items) {
        nlohmann::json j_risk = item.result.risk_level;
        std::string r = j_risk.get<std::string>();
        // 简单计数
        if (r == "critical") current_batch_stats->critical_risk++;
        else if (r == "error") current_batch_stats->error_risk++;
        else if (r == "warning") current_batch_stats->warning_risk++;
        else if (r == "info") current_batch_stats->info_risk++;
        else if (r == "safe") current_batch_stats->safe_risk++;
        else current_batch_stats->unknown_risk++;

        // 如果是高风险，加入最近警报缓存 (只存内存，不用查库)
        if (r == "critical") {
            AlertInfo alert;
            alert.trace_id = item.trace_id;
            alert.summary = item.result.summary;
            // 获取当前时间字符串
            time_t now = time(0);
            tm *ltm = localtime(&now);
            char buffer[80];
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", ltm);
            alert.time = std::string(buffer);
            current_batch_stats->recent_alerts.push_back(alert);
        }
    }

    // B. 数据库操作 (IO 操作，持 DB 锁)
    {
        std::lock_guard<std::mutex> lock_(mutex_);

        // 1. 开启事务
        int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        checkSqliteError(db_, rc, "Failed to begin transaction");

        // 2. 准备语句 (Updated columns)
        const char *sql = R"(
            INSERT INTO analysis_results
            (trace_id, batch_id, status, risk_level, summary, root_cause, solution, response_time_ms)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?);
        )";
        sqlite3_stmt *raw_stmt = nullptr;
        rc = sqlite3_prepare_v2(db_, sql, -1, &raw_stmt, nullptr);

        if (rc != SQLITE_OK) {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            checkSqliteError(db_, rc, "Failed to prepare statement");
        }

        StmtPtr stmt(raw_stmt);

        try {
            for (const auto &item : items) {
                nlohmann::json j_risk = item.result.risk_level;
                std::string risk_str = j_risk.get<std::string>();

                sqlite3_bind_text(stmt.get(), 1, item.trace_id.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int64(stmt.get(), 2, batch_id); // New: batch_id
                sqlite3_bind_text(stmt.get(), 3, item.status.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt.get(), 4, risk_str.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt.get(), 5, item.result.summary.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt.get(), 6, item.result.root_cause.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt.get(), 7, item.result.solution.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int(stmt.get(), 8, item.response_time_ms);

                if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
                    checkSqliteError(db_, SQLITE_ERROR, "Step execution failed");
                }
                sqlite3_reset(stmt.get());
            }

            rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
            checkSqliteError(db_, rc, "Failed to commit");
        }
        catch (...) {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            throw;
        }
    } // DB 锁释放

    // C. 更新内存快照 (极快, 持 Cache 锁)
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        if (cached_stats_) {
            auto new_stats = std::make_shared<DashboardStats>(*cached_stats_);
            // 累加
            new_stats->total_logs += current_batch_stats->total_logs;
            new_stats->critical_risk += current_batch_stats->critical_risk;
            new_stats->error_risk += current_batch_stats->error_risk;
            new_stats->warning_risk += current_batch_stats->warning_risk;
            new_stats->info_risk += current_batch_stats->info_risk;
            new_stats->safe_risk += current_batch_stats->safe_risk;
            new_stats->unknown_risk += current_batch_stats->unknown_risk;

            // 合并最近警报 (前插，手动管理小 vector 代替 Queue)
            if (!current_batch_stats->recent_alerts.empty()) {
                // 1. 新的警报在最前
                std::vector<AlertInfo> merged = current_batch_stats->recent_alerts;
                // 2. 追加旧的警报
                merged.insert(merged.end(), new_stats->recent_alerts.begin(), new_stats->recent_alerts.end());
                // 3. 截断只保留前5个
                if (merged.size() > 5) merged.resize(5);
                new_stats->recent_alerts = merged;
            }

            cached_stats_ = new_stats;
        }
    }
}

HistoryPage SqliteLogRepository::getHistoricalLogs(int page, int pageSize, const std::string& level, const std::string& keyword)
{
    // 1. 锁保护
    std::lock_guard<std::mutex> lock_(mutex_);
    HistoryPage res;

    // --- 构建 WHERE 子句 ---
    std::string where_clause = " FROM analysis_results WHERE 1=1 ";
    std::vector<std::string> bind_params;

    if (!level.empty()) {
        std::string lower_level = level;
        std::transform(lower_level.begin(), lower_level.end(), lower_level.begin(), ::tolower);

        if (lower_level == "critical") {
            where_clause += " AND risk_level IN ('critical', 'high') ";
        } else if (lower_level == "error") {
            where_clause += " AND risk_level IN ('error', 'medium') ";
        } else if (lower_level == "warning") {
            where_clause += " AND risk_level IN ('warning', 'low') ";
        } else {
             where_clause += " AND LOWER(risk_level) = ? ";
             bind_params.push_back(lower_level);
        }
    }

    if (!keyword.empty()) {
        // 模糊搜索： summary LIKE %keyword% OR trace_id LIKE %keyword%
        where_clause += " AND (summary LIKE ? OR trace_id LIKE ?) ";
        // 我们需要手动加 %
        std::string p = "%" + keyword + "%";
        bind_params.push_back(p);
        bind_params.push_back(p);
    }

    // --- (1) 总数查询 ---
    {
        std::string sql_count = "SELECT COUNT(*) " + where_clause;
        sqlite3_stmt *stmt = nullptr;
        
        int rc = sqlite3_prepare_v2(db_, sql_count.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) checkSqliteError(db_, rc, "Failed to prepare COUNT");
        
        StmtPtr stmt_ptr(stmt);
        
        // 绑定参数
        int idx = 1;
        for(const auto& val : bind_params) {
            sqlite3_bind_text(stmt, idx++, val.c_str(), -1, SQLITE_TRANSIENT);
        }

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            res.total_count = sqlite3_column_int(stmt, 0);
        }
    }

    // --- (2) 分页查询 ---
    {
        // 排序和分页
        std::string sql_page = "SELECT trace_id, risk_level, summary, processed_at " + 
                               where_clause + 
                               " ORDER BY processed_at DESC LIMIT ? OFFSET ?;";
        
        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql_page.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) checkSqliteError(db_, rc, "Failed to prepare SELECT");

        StmtPtr stmt_ptr(stmt);

        // 绑定 WHERE 参数
        int idx = 1;
        for(const auto& val : bind_params) {
            sqlite3_bind_text(stmt, idx++, val.c_str(), -1, SQLITE_TRANSIENT);
        }
        
        // 绑定 Limit/Offset
        sqlite3_bind_int(stmt, idx++, pageSize);
        sqlite3_bind_int(stmt, idx++, std::max(0, (page - 1) * pageSize));

        auto get_col = [&](int col) -> std::string {
            const char *txt = (const char *)sqlite3_column_text(stmt, col);
            return txt ? std::string(txt) : "";
        };

        while (sqlite3_step(stmt) == SQLITE_ROW) {
             std::string risk_str = get_col(1);
             // 容错处理
             nlohmann::json j_risk = risk_str;
             RiskLevel risk_enum = RiskLevel::UNKNOWN;
             try { risk_enum = j_risk.get<RiskLevel>(); } catch(...){}

             res.logs.emplace_back(HistoricalLogItem{
                 get_col(0),
                 risk_enum,
                 get_col(2),
                 get_col(3)
             });
        }
    }

    return res;
}
