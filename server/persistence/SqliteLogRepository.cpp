#include "persistence/SqliteLogRepository.h"
#include <filesystem>
#include <iostream>
#include "persistence/SqliteLogRepository.h"
#include "SqliteLogRepository.h"
using namespace persistence;
SqliteLogRepository::SqliteLogRepository(const std::string &db_path)
{
    std::string final_path;
    if (db_path.find('/') != std::string::npos || db_path.find('\\') != std::string::npos) {
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

    // 以前是 analysis_content TEXT, 现在拆开了
    const char *sql_create_results = R"(
    CREATE TABLE IF NOT EXISTS analysis_results (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        trace_id TEXT NOT NULL UNIQUE,
        status TEXT NOT NULL,
        
        -- 核心字段拆分
        risk_level TEXT,  -- critical, error, warning, info, safe
        summary TEXT,
        root_cause TEXT,
        solution TEXT,
        
        -- 新增：响应耗时 (用于计算平均值,微秒)
        response_time_ms INTEGER DEFAULT 0,
        -- 新增：批次的总结内容
        global_summary TEXT,
        processed_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        FOREIGN KEY (trace_id) REFERENCES raw_logs(trace_id)
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

DashboardStats SqliteLogRepository::getDashboardStats()
{
    std::lock_guard<std::mutex> lock(mutex_);
    DashboardStats stats;
    // 查询1：平均耗时
    const char *sql = "SELECT AVG(response_time_ms) FROM analysis_results";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        throw std::runtime_error("Failed to prepare statement for select avg response_time_ms " + std::string(sqlite3_errmsg(db_)));
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        stats.avg_response_time = sqlite3_column_double(stmt, 0);
        sqlite3_finalize(stmt);
    }
    else
    {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to execute select avg response_time_ms" + std::string(sqlite3_errmsg(db_)));
    }
    // 查询2： 查询总日志(raw_logs)数量
    sql = "SELECT COUNT(*) FROM raw_logs;";
    rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        throw std::runtime_error("Failed to prepare statement for select total logs" + std::string(sqlite3_errmsg(db_)));
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        stats.total_logs = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    else
    {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to execute select total logs" + std::string(sqlite3_errmsg(db_)));
    }
    // 查询3：查询log_analysis中risk_level分别为high，medium,low的日志数量
    sql = "select risk_level,count(*) from analysis_results group by risk_level";
    rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        throw std::runtime_error("Failed to prepare statement for select risk level counts" + std::string(sqlite3_errmsg(db_)));
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        const char *risk = (const char *)sqlite3_column_text(stmt, 0);
        int count = sqlite3_column_int(stmt, 1);
        if (risk != nullptr)
        {
            std::string r = std::string(risk);
            if (r == "critical" || r == "high")
            {
                stats.critical_risk += count;
            }
            else if (r == "error" || r == "medium")
            {
                stats.error_risk += count;
            }
            else if (r == "warning" || r == "low")
            {
                stats.warning_risk += count;
            }
            else if (r == "info") {
                stats.info_risk += count;
            }
            else if (std::string(risk) == "safe") {
                stats.safe_risk = count;
            }
            else {
                // 其他所有未识别的字符串（如 "unknown", "", null）都算作 Unknown
                stats.unknown_risk += count;
            }
        }else {
            // risk 字段本身是 NULL
            stats.unknown_risk += count;
        }
    }
    if (rc != SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to execute select risk_level count statement");
    }
    sqlite3_finalize(stmt);
    // 查询4：查询5条风险日志
    sql = R"(
        SELECT trace_id, summary, processed_at 
        FROM analysis_results 
        WHERE risk_level IN ('critical', 'high')
        ORDER BY id DESC 
        LIMIT 5;
    )";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK)
    {
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            AlertInfo alert;
            alert.trace_id = (const char *)sqlite3_column_text(stmt, 0);

            const char *sum = (const char *)sqlite3_column_text(stmt, 1);
            alert.summary = sum ? sum : "No Summary";

            const char *time = (const char *)sqlite3_column_text(stmt, 2);
            alert.time = time ? time : "";

            stats.recent_alerts.push_back(alert);
        }
        if (rc != SQLITE_DONE)
        {
            sqlite3_finalize(stmt);
            throw std::runtime_error("queryRecentAlerts failed: " + std::string(sqlite3_errmsg(db_)));
        }
    }
    else
    {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement for select 5 recent alerts" + std::string(sqlite3_errmsg(db_)));
    }
    sqlite3_finalize(stmt);
    return stats;
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
void SqliteLogRepository::saveAnalysisResultBatch(const std::vector<AnalysisResultItem> &items, const std::string &global_summary)
{
    std::lock_guard<std::mutex> lock_(mutex_);

    // 1. 开启事务 (利用 helper 函数检查错误)
    int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    checkSqliteError(db_, rc, "Failed to begin transaction");

    // 2. 准备语句
    const char *sql = R"(
        INSERT INTO analysis_results 
        (trace_id, status, risk_level, summary, root_cause, solution, response_time_ms,global_summary) 
        VALUES (?, ?, ?, ?, ?, ?, ?,?);
    )";
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
        for (const auto &item : items)
        {
            // 转换 Enum 为 String
            nlohmann::json j_risk = item.result.risk_level;
            std::string risk_str = j_risk.get<std::string>();

            sqlite3_bind_text(stmt.get(), 1, item.trace_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt.get(), 2, item.status.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt.get(), 3, risk_str.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt.get(), 4, item.result.summary.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt.get(), 5, item.result.root_cause.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt.get(), 6, item.result.solution.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt.get(), 7, item.response_time_ms);
            sqlite3_bind_text(stmt.get(), 8, global_summary.c_str(), -1, SQLITE_STATIC);
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

HistoryPage SqliteLogRepository::getHistoricalLogs(int page, int pageSize)
{
    // 1. 锁保护：确保线程安全
    std::lock_guard<std::mutex> lock_(mutex_);
    HistoryPage res;

    // --- (1) 总数查询：获取总记录数 (Total Count)，用于计算总共页数 ---
    {
        const char *sql_count = "SELECT COUNT(*) FROM analysis_results;";
        sqlite3_stmt *raw_stmt_count = nullptr;

        int rc = sqlite3_prepare_v2(db_, sql_count, -1, &raw_stmt_count, nullptr);
        if (rc != SQLITE_OK)
        {
            checkSqliteError(db_, rc, "Failed to prepare COUNT statement");
        }
        StmtPtr stmt_count(raw_stmt_count);

        rc = sqlite3_step(stmt_count.get());
        if (rc == SQLITE_ROW)
        {
            res.total_count = sqlite3_column_int(stmt_count.get(), 0);
        }
        else
        {
            // 即使查询失败，也应设置 total_count=0，并记录错误日志，而不是抛出致命异常。
            // 保持健壮性：这里我们简单地抛出异常，因为查询总数是核心功能。
            checkSqliteError(db_, rc, "Failed to execute COUNT statement");
        }
    }

    // --- (2) 分页查询：获取当前页的日志记录 ---
    const int offset = std::max(0, (page - 1) * pageSize);

    // 【关键修正】：添加 ORDER BY processed_at DESC
    // 必须要排序，数据库内部不会自动排序
    // processed_at 要不要加个索引啊？
    const char *sql_page = R"(
        SELECT trace_id, risk_level, summary, processed_at 
        FROM analysis_results 
        ORDER BY processed_at DESC  --必须要写，因为厂商和文档没有承诺，所以可能会朝令夕改，导致无序
        LIMIT ? OFFSET ?;
    )";

    sqlite3_stmt *raw_stmt_page = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql_page, -1, &raw_stmt_page, nullptr);

    if (rc != SQLITE_OK)
    {
        checkSqliteError(db_, rc, "Failed to prepare SELECT/LIMIT/OFFSET statement");
    }

    StmtPtr stmt_page(raw_stmt_page);
    try
    {
        // 绑定 LIMIT = pageSize (参数1)
        sqlite3_bind_int(stmt_page.get(), 1, pageSize);
        // 绑定 OFFSET = offset (参数2)
        sqlite3_bind_int(stmt_page.get(), 2, offset);

        // 辅助函数：简化列提取
        auto get_col = [&](int col) -> std::string
        {
            const char *txt = (const char *)sqlite3_column_text(stmt_page.get(), col);
                return txt ? std::string(txt) : "safe";
        };

        // 迭代获取结果
        while ((rc = sqlite3_step(stmt_page.get())) == SQLITE_ROW)
        {
            // 顺序：trace_id(0), risk_level(1), summary(2), processed_at(3)
            // 从 DB 获取 string
            std::string risk_str = get_col(1);

            // 手动转换 String -> Enum (利用 nlohmann::json 的转换能力)
            nlohmann::json j_risk = risk_str;
            RiskLevel risk_enum = RiskLevel::SAFE; // Default
            try {
                risk_enum = j_risk.get<RiskLevel>();
            } catch (...) {
                // Fallback if DB content is weird
                risk_enum = RiskLevel::UNKNOWN;
            }

            res.logs.emplace_back(HistoricalLogItem{
                get_col(0),
                risk_enum,
                get_col(2),
                get_col(3)});
            // 【修正】：删除了 res.total_count++
        }

        // 检查最终状态：必须是 SQLITE_DONE
        if (rc != SQLITE_DONE)
        {
            checkSqliteError(db_, rc, "Step execution failed during pagination");
        }

        return res;
    }
    catch (...)
    {
        // 智能指针 (StmtPtr) 自动处理 finalize
        throw;
    }
}