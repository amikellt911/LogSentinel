#include "persistence/SqliteTraceRepository.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <sqlite3.h>
#include "persistence/SqliteHelper.h"

namespace
{
constexpr size_t kDefaultTraceSearchPage = 1;
constexpr size_t kDefaultTraceSearchPageSize = 20;
constexpr size_t kMaxTraceSearchPageSize = 100;
constexpr size_t kMaxAiErrorLength = 1024;

std::string BuildInClausePlaceholders(size_t count)
{
    std::string placeholders = "(";
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            placeholders += ", ";
        }
        placeholders += "?";
    }
    placeholders += ")";
    return placeholders;
}

std::string TruncateAiError(const std::string& ai_error)
{
    if (ai_error.size() <= kMaxAiErrorLength) {
        return ai_error;
    }
    return ai_error.substr(0, kMaxAiErrorLength);
}

void UpdateSummaryAiState(sqlite3* db,
                          const std::string& trace_id,
                          const std::string& ai_status,
                          const std::string& ai_error)
{
    const char* sql_update_summary_ai_state = R"(
        UPDATE trace_summary
        SET ai_status = ?, ai_error = ?
        WHERE trace_id = ?;
    )";
    persistence::StmtPtr update_stmt;
    sqlite3_stmt* raw_stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db, sql_update_summary_ai_state, -1, &raw_stmt, nullptr);
    persistence::checkSqliteError(db, rc, "Prepare trace_summary ai_state update");
    update_stmt.reset(raw_stmt);

    const std::string truncated_ai_error = TruncateAiError(ai_error);
    sqlite3_bind_text(update_stmt.get(), 1, ai_status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(update_stmt.get(), 2, truncated_ai_error.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(update_stmt.get(), 3, trace_id.c_str(), -1, SQLITE_STATIC);

    const int step_rc = sqlite3_step(update_stmt.get());
    persistence::checkSqliteError(db, step_rc, "Update trace_summary ai_state");
}

void UpdateSummaryAnalysisOutcome(sqlite3* db,
                                  const std::string& trace_id,
                                  const std::string& risk_level)
{
    const char* sql_update_summary_outcome = R"(
        UPDATE trace_summary
        SET risk_level = ?, ai_status = 'completed', ai_error = ''
        WHERE trace_id = ?;
    )";
    persistence::StmtPtr update_stmt;
    sqlite3_stmt* raw_stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db, sql_update_summary_outcome, -1, &raw_stmt, nullptr);
    persistence::checkSqliteError(db, rc, "Prepare trace_summary analysis outcome update");
    update_stmt.reset(raw_stmt);

    sqlite3_bind_text(update_stmt.get(), 1, risk_level.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(update_stmt.get(), 2, trace_id.c_str(), -1, SQLITE_STATIC);

    const int step_rc = sqlite3_step(update_stmt.get());
    persistence::checkSqliteError(db, step_rc, "Update trace_summary analysis outcome");
}
} // namespace

SqliteTraceRepository::SqliteTraceRepository(const std::string& db_path)
    : db_path_(db_path)
{
    std::string final_path = db_path_;
    // Trace 仓储也跟配置仓储保持同一条边界：
    // main.cpp 负责决定最终 DB 路径，这里只负责确保父目录存在并打开那份文件。
    if (final_path != ":memory:") {
        const std::filesystem::path final_db_path(final_path);
        const std::filesystem::path parent_directory = final_db_path.parent_path();
        if (!parent_directory.empty() && !std::filesystem::exists(parent_directory)) {
            std::filesystem::create_directories(parent_directory);
        }
    }

    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc = sqlite3_open_v2(final_path.c_str(), &db_, flags, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open trace database: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_close_v2(db_);
        db_ = nullptr;
        throw std::runtime_error("Failed to open trace database");
    }

    char* errmsg = nullptr;
    const char* sql_fk = "PRAGMA foreign_keys = ON;";
    rc = sqlite3_exec(db_, sql_fk, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot enable foreign keys: " << errmsg << std::endl;
        sqlite3_free(errmsg);
        errmsg = nullptr;
    }

    const char* sql_wal = "PRAGMA journal_mode=WAL;";
    rc = sqlite3_exec(db_, sql_wal, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot set WAL mode: " << errmsg << std::endl;
        sqlite3_free(errmsg);
        errmsg = nullptr;
    }

    const char* sql_create_tables = R"(
CREATE TABLE IF NOT EXISTS trace_summary (
  trace_id TEXT PRIMARY KEY,
  service_name TEXT NOT NULL,
  start_time_ms INTEGER NOT NULL,
  end_time_ms INTEGER,
  duration_ms INTEGER NOT NULL,
  span_count INTEGER NOT NULL,
  token_count INTEGER NOT NULL,
  risk_level TEXT NOT NULL,
  ai_status TEXT NOT NULL DEFAULT 'pending',
  ai_error TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS trace_span (
  trace_id TEXT NOT NULL,
  span_id TEXT NOT NULL,
  parent_id TEXT,
  service_name TEXT NOT NULL,
  operation TEXT NOT NULL,
  start_time_ms INTEGER NOT NULL,
  duration_ms INTEGER NOT NULL,
  status TEXT NOT NULL,
  attributes_json TEXT NOT NULL,
  PRIMARY KEY (trace_id, span_id),
  FOREIGN KEY (trace_id) REFERENCES trace_summary(trace_id)
);

CREATE TABLE IF NOT EXISTS trace_analysis (
  trace_id TEXT PRIMARY KEY,
  risk_level TEXT NOT NULL,
  summary TEXT NOT NULL,
  root_cause TEXT NOT NULL,
  solution TEXT NOT NULL,
  confidence REAL NOT NULL,
  FOREIGN KEY (trace_id) REFERENCES trace_summary(trace_id)
);
)";

    errmsg = nullptr;
    rc = sqlite3_exec(db_, sql_create_tables, nullptr, nullptr, &errmsg);
    if (errmsg) {
        sqlite3_free(errmsg);
        errmsg = nullptr;
    }
    persistence::checkSqliteError(db_, rc, "Failed to create trace tables");

    const char* sql_create_index = R"(
CREATE INDEX IF NOT EXISTS idx_trace_summary_start_time_trace_id
ON trace_summary(start_time_ms DESC, trace_id DESC);
CREATE INDEX IF NOT EXISTS idx_trace_summary_service_start_time_trace_id
ON trace_summary(service_name, start_time_ms DESC, trace_id DESC);
CREATE INDEX IF NOT EXISTS idx_trace_span_trace_start_time_span_id
ON trace_span(trace_id, start_time_ms ASC, span_id ASC);
)";
    errmsg = nullptr;
    rc = sqlite3_exec(db_, sql_create_index, nullptr, nullptr, &errmsg);
    if (errmsg) {
        sqlite3_free(errmsg);
        errmsg = nullptr;
    }
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create trace indexes: " << sqlite3_errmsg(db_) << std::endl;
    }
}

SqliteTraceRepository::~SqliteTraceRepository()
{
    if (db_) {
        sqlite3_close_v2(db_);
    }
}

TraceSearchResult SqliteTraceRepository::SearchTraces(const TraceSearchRequest& request)
{
    // 这里给读侧也串一个 repo 内部互斥，目的不是“把查询做成单线程”，
    // 而是保证同一个 SQLite 连接上的多步读流程不要和别的查询在连接级别交叉执行。
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        throw std::runtime_error("Trace database is not available");
    }

    TraceSearchResult result;
    const size_t page = request.page < 1 ? kDefaultTraceSearchPage : request.page;
    size_t page_size = request.page_size;
    if (page_size < 1) {
        page_size = kDefaultTraceSearchPageSize;
    }
    if (page_size > kMaxTraceSearchPageSize) {
        page_size = kMaxTraceSearchPageSize;
    }

    if (request.trace_id.has_value() && !request.trace_id->empty()) {
        const char* sql_exact = R"(
            SELECT trace_id,
                   service_name,
                   start_time_ms,
                   end_time_ms,
                   duration_ms,
                   span_count,
                   token_count,
                   risk_level,
                   ai_status
            FROM trace_summary
            WHERE trace_id = ?;
        )";
        persistence::StmtPtr exact_stmt;
        {
            sqlite3_stmt* raw_stmt = nullptr;
            const int rc = sqlite3_prepare_v2(db_, sql_exact, -1, &raw_stmt, nullptr);
            persistence::checkSqliteError(db_, rc, "Prepare exact trace_summary search");
            exact_stmt.reset(raw_stmt);
        }
        sqlite3_bind_text(exact_stmt.get(), 1, request.trace_id->c_str(), -1, SQLITE_TRANSIENT);

        const int step_rc = sqlite3_step(exact_stmt.get());
        if (step_rc == SQLITE_ROW) {
            TraceListItem item;
            item.trace_id = reinterpret_cast<const char*>(sqlite3_column_text(exact_stmt.get(), 0));
            item.service_name = reinterpret_cast<const char*>(sqlite3_column_text(exact_stmt.get(), 1));
            item.start_time_ms = sqlite3_column_int64(exact_stmt.get(), 2);
            if (sqlite3_column_type(exact_stmt.get(), 3) != SQLITE_NULL) {
                item.end_time_ms = sqlite3_column_int64(exact_stmt.get(), 3);
            }
            item.duration_ms = sqlite3_column_int64(exact_stmt.get(), 4);
            item.span_count = static_cast<size_t>(sqlite3_column_int64(exact_stmt.get(), 5));
            item.token_count = static_cast<size_t>(sqlite3_column_int64(exact_stmt.get(), 6));
            item.risk_level = reinterpret_cast<const char*>(sqlite3_column_text(exact_stmt.get(), 7));
            item.ai_status = reinterpret_cast<const char*>(sqlite3_column_text(exact_stmt.get(), 8));
            result.total = 1;
            result.items.push_back(std::move(item));
        } else if (step_rc != SQLITE_DONE) {
            persistence::checkSqliteError(db_, step_rc, "Step exact trace_summary search");
        }
        return result;
    }

    if (request.start_time_ms.has_value() &&
        request.end_time_ms.has_value() &&
        request.start_time_ms.value() >= request.end_time_ms.value()) {
        return result;
    }

    std::string where_sql = " WHERE 1 = 1";

    if (request.service_name.has_value() && !request.service_name->empty()) {
        where_sql += " AND service_name = ?";
    }

    // 这里很容易混：请求里的 start_time_ms / end_time_ms 是“搜索时间窗边界”，
    // 不是让 SQL 去比较 trace_summary.end_time_ms 这一列。
    // 当前列表搜索统一钉在 trace_summary.start_time_ms 上做范围过滤，也就是：
    // request.start_time_ms <= trace_summary.start_time_ms < request.end_time_ms
    // 这样时间筛选语义和列表排序字段保持一致，索引也更容易命中。
    if (request.start_time_ms.has_value()) {
        where_sql += " AND start_time_ms >= ?";
    }
    if (request.end_time_ms.has_value()) {
        where_sql += " AND start_time_ms < ?";
    }

    if (!request.risk_levels.empty()) {
        where_sql += " AND risk_level IN " + BuildInClausePlaceholders(request.risk_levels.size());
    }

    auto bind_common_filters = [&](sqlite3_stmt* stmt, int start_index) -> int {
        int bind_index = start_index;
        if (request.service_name.has_value() && !request.service_name->empty()) {
            sqlite3_bind_text(stmt, bind_index++, request.service_name->c_str(), -1, SQLITE_TRANSIENT);
        }
        // bind 顺序必须和上面 where_sql 追加占位符的顺序完全一致；
        // 既然 end_time_ms 只在“搜索时间窗上界”语义下参与过滤，
        // 那么这里绑定的也是“上界参数”，不是 trace_summary.end_time_ms 列值。
        if (request.start_time_ms.has_value()) {
            sqlite3_bind_int64(stmt, bind_index++, static_cast<sqlite3_int64>(request.start_time_ms.value()));
        }
        if (request.end_time_ms.has_value()) {
            sqlite3_bind_int64(stmt, bind_index++, static_cast<sqlite3_int64>(request.end_time_ms.value()));
        }
        for (const auto& risk_level : request.risk_levels) {
            sqlite3_bind_text(stmt, bind_index++, risk_level.c_str(), -1, SQLITE_TRANSIENT);
        }
        return bind_index;
    };

    const std::string count_sql = "SELECT COUNT(*) FROM trace_summary" + where_sql + ";";
    persistence::StmtPtr count_stmt;
    {
        sqlite3_stmt* raw_stmt = nullptr;
        const int rc = sqlite3_prepare_v2(db_, count_sql.c_str(), -1, &raw_stmt, nullptr);
        persistence::checkSqliteError(db_, rc, "Prepare trace_summary count query");
        count_stmt.reset(raw_stmt);
    }
    bind_common_filters(count_stmt.get(), 1);
    const int count_step_rc = sqlite3_step(count_stmt.get());
    if (count_step_rc == SQLITE_ROW) {
        result.total = static_cast<size_t>(sqlite3_column_int64(count_stmt.get(), 0));
    } else {
        persistence::checkSqliteError(db_, count_step_rc, "Step trace_summary count query");
    }
    if (result.total == 0) {
        return result;
    }

    const size_t offset = (page - 1) * page_size;
    const std::string select_sql = R"(
        SELECT trace_id,
               service_name,
               start_time_ms,
               end_time_ms,
               duration_ms,
               span_count,
               token_count,
               risk_level,
               ai_status
        FROM trace_summary
    )" + where_sql + R"(
        ORDER BY start_time_ms DESC, trace_id DESC
        LIMIT ? OFFSET ?;
    )";

    persistence::StmtPtr select_stmt;
    {
        sqlite3_stmt* raw_stmt = nullptr;
        const int rc = sqlite3_prepare_v2(db_, select_sql.c_str(), -1, &raw_stmt, nullptr);
        persistence::checkSqliteError(db_, rc, "Prepare trace_summary search query");
        select_stmt.reset(raw_stmt);
    }
    int bind_index = bind_common_filters(select_stmt.get(), 1);
    sqlite3_bind_int64(select_stmt.get(), bind_index++, static_cast<sqlite3_int64>(page_size));
    sqlite3_bind_int64(select_stmt.get(), bind_index++, static_cast<sqlite3_int64>(offset));

    while (true) {
        const int step_rc = sqlite3_step(select_stmt.get());
        if (step_rc == SQLITE_DONE) {
            break;
        }
        persistence::checkSqliteError(db_, step_rc, "Step trace_summary search query");

        TraceListItem item;
        item.trace_id = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt.get(), 0));
        item.service_name = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt.get(), 1));
        item.start_time_ms = sqlite3_column_int64(select_stmt.get(), 2);
        if (sqlite3_column_type(select_stmt.get(), 3) != SQLITE_NULL) {
            item.end_time_ms = sqlite3_column_int64(select_stmt.get(), 3);
        }
        item.duration_ms = sqlite3_column_int64(select_stmt.get(), 4);
        item.span_count = static_cast<size_t>(sqlite3_column_int64(select_stmt.get(), 5));
        item.token_count = static_cast<size_t>(sqlite3_column_int64(select_stmt.get(), 6));
        item.risk_level = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt.get(), 7));
        item.ai_status = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt.get(), 8));
        result.items.push_back(std::move(item));
    }

    return result;
}

std::optional<TraceDetailRecord> SqliteTraceRepository::GetTraceDetail(const std::string& trace_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        throw std::runtime_error("Trace database is not available");
    }

    char* errmsg = nullptr;
    auto rollback = [this]() {
        char* rollback_err = nullptr;
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &rollback_err);
        if (rollback_err) {
            sqlite3_free(rollback_err);
        }
    };

    try {
        // 详情页不是一条 SQL，而是“先查 summary/analysis，再查 spans”。
        // 这里显式开一个读事务，是为了把这两次读取固定到同一份 WAL 快照上，
        // 避免 flush 线程恰好在两次 SELECT 之间写入，导致顶部摘要和 spans 数量对不上。
        int rc = sqlite3_exec(db_, "BEGIN;", nullptr, nullptr, &errmsg);
        if (errmsg) {
            sqlite3_free(errmsg);
            errmsg = nullptr;
        }
        persistence::checkSqliteError(db_, rc, "Begin trace detail read transaction");

        const char* sql_detail = R"(
            SELECT s.trace_id,
                   s.service_name,
                   s.start_time_ms,
                   s.end_time_ms,
                   s.duration_ms,
                   s.span_count,
                   s.token_count,
                   COALESCE(a.risk_level, s.risk_level) AS display_risk_level,
                   s.ai_status,
                   s.ai_error,
                   a.risk_level,
                   a.summary,
                   a.root_cause,
                   a.solution,
                   a.confidence
            FROM trace_summary s
            LEFT JOIN trace_analysis a
              ON a.trace_id = s.trace_id
            WHERE s.trace_id = ?;
        )";
        persistence::StmtPtr detail_stmt;
        sqlite3_stmt* detail_raw_stmt = nullptr;
        rc = sqlite3_prepare_v2(db_, sql_detail, -1, &detail_raw_stmt, nullptr);
        persistence::checkSqliteError(db_, rc, "Prepare trace detail summary query");
        detail_stmt.reset(detail_raw_stmt);
        sqlite3_bind_text(detail_stmt.get(), 1, trace_id.c_str(), -1, SQLITE_TRANSIENT);

        const int detail_step_rc = sqlite3_step(detail_stmt.get());
        if (detail_step_rc == SQLITE_DONE) {
            rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errmsg);
            if (errmsg) {
                sqlite3_free(errmsg);
                errmsg = nullptr;
            }
            persistence::checkSqliteError(db_, rc, "Commit empty trace detail read transaction");
            return std::nullopt;
        }
        persistence::checkSqliteError(db_, detail_step_rc, "Step trace detail summary query");

        TraceDetailRecord detail;
        detail.trace_id = reinterpret_cast<const char*>(sqlite3_column_text(detail_stmt.get(), 0));
        detail.service_name = reinterpret_cast<const char*>(sqlite3_column_text(detail_stmt.get(), 1));
        detail.start_time_ms = sqlite3_column_int64(detail_stmt.get(), 2);
        if (sqlite3_column_type(detail_stmt.get(), 3) != SQLITE_NULL) {
            detail.end_time_ms = sqlite3_column_int64(detail_stmt.get(), 3);
        }
        detail.duration_ms = sqlite3_column_int64(detail_stmt.get(), 4);
        detail.span_count = static_cast<size_t>(sqlite3_column_int64(detail_stmt.get(), 5));
        detail.token_count = static_cast<size_t>(sqlite3_column_int64(detail_stmt.get(), 6));
        detail.risk_level = reinterpret_cast<const char*>(sqlite3_column_text(detail_stmt.get(), 7));
        detail.ai_status = reinterpret_cast<const char*>(sqlite3_column_text(detail_stmt.get(), 8));
        detail.ai_error = reinterpret_cast<const char*>(sqlite3_column_text(detail_stmt.get(), 9));
        // tags 当前后端主链路还没有真实产出，这里先稳定返回空数组，
        // 等 AI proxy / analysis 存储链真的接上 tags 后再补真实读取。
        detail.tags.clear();

        if (sqlite3_column_type(detail_stmt.get(), 10) != SQLITE_NULL) {
            TraceAnalysisDetail analysis;
            analysis.risk_level = reinterpret_cast<const char*>(sqlite3_column_text(detail_stmt.get(), 10));
            analysis.summary = reinterpret_cast<const char*>(sqlite3_column_text(detail_stmt.get(), 11));
            analysis.root_cause = reinterpret_cast<const char*>(sqlite3_column_text(detail_stmt.get(), 12));
            analysis.solution = reinterpret_cast<const char*>(sqlite3_column_text(detail_stmt.get(), 13));
            analysis.confidence = sqlite3_column_double(detail_stmt.get(), 14);
            detail.analysis = std::move(analysis);
        }

        const char* sql_spans = R"(
            SELECT span_id,
                   parent_id,
                   service_name,
                   operation,
                   start_time_ms,
                   duration_ms,
                   status
            FROM trace_span
            WHERE trace_id = ?
            ORDER BY start_time_ms ASC, span_id ASC;
        )";
        persistence::StmtPtr spans_stmt;
        sqlite3_stmt* spans_raw_stmt = nullptr;
        rc = sqlite3_prepare_v2(db_, sql_spans, -1, &spans_raw_stmt, nullptr);
        persistence::checkSqliteError(db_, rc, "Prepare trace detail spans query");
        spans_stmt.reset(spans_raw_stmt);
        sqlite3_bind_text(spans_stmt.get(), 1, trace_id.c_str(), -1, SQLITE_TRANSIENT);

        while (true) {
            const int spans_step_rc = sqlite3_step(spans_stmt.get());
            if (spans_step_rc == SQLITE_DONE) {
                break;
            }
            persistence::checkSqliteError(db_, spans_step_rc, "Step trace detail spans query");

            TraceSpanDetail span;
            span.span_id = reinterpret_cast<const char*>(sqlite3_column_text(spans_stmt.get(), 0));
            if (sqlite3_column_type(spans_stmt.get(), 1) != SQLITE_NULL) {
                span.parent_id = std::string(reinterpret_cast<const char*>(sqlite3_column_text(spans_stmt.get(), 1)));
            }
            span.service_name = reinterpret_cast<const char*>(sqlite3_column_text(spans_stmt.get(), 2));
            span.operation = reinterpret_cast<const char*>(sqlite3_column_text(spans_stmt.get(), 3));
            span.start_time_ms = sqlite3_column_int64(spans_stmt.get(), 4);
            span.duration_ms = sqlite3_column_int64(spans_stmt.get(), 5);
            span.raw_status = reinterpret_cast<const char*>(sqlite3_column_text(spans_stmt.get(), 6));
            detail.spans.push_back(std::move(span));
        }

        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errmsg);
        if (errmsg) {
            sqlite3_free(errmsg);
            errmsg = nullptr;
        }
        persistence::checkSqliteError(db_, rc, "Commit trace detail read transaction");
        return detail;
    } catch (const std::exception&) {
        rollback();
        throw;
    }
}

bool SqliteTraceRepository::DeleteTraceById(const std::string& trace_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || trace_id.empty()) {
        return false;
    }
    return DeleteTracesByIdsAtomic({trace_id});
}

size_t SqliteTraceRepository::DeleteExpiredTracesBatch(int64_t cutoff_ms, size_t limit)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || cutoff_ms <= 0 || limit == 0) {
        return 0;
    }

    std::vector<std::string> expired_trace_ids;
    const char* sql_select_expired = R"(
        SELECT trace_id
        FROM trace_summary
        WHERE COALESCE(end_time_ms, start_time_ms) < ?
        ORDER BY COALESCE(end_time_ms, start_time_ms) ASC, trace_id ASC
        LIMIT ?;
    )";
    persistence::StmtPtr select_stmt;
    {
        sqlite3_stmt* raw_stmt = nullptr;
        const int rc = sqlite3_prepare_v2(db_, sql_select_expired, -1, &raw_stmt, nullptr);
        persistence::checkSqliteError(db_, rc, "Prepare expired trace_id selection");
        select_stmt.reset(raw_stmt);
    }
    sqlite3_bind_int64(select_stmt.get(), 1, cutoff_ms);
    sqlite3_bind_int64(select_stmt.get(), 2, static_cast<sqlite3_int64>(limit));

    while (true) {
        const int step_rc = sqlite3_step(select_stmt.get());
        if (step_rc == SQLITE_ROW) {
            const unsigned char* text = sqlite3_column_text(select_stmt.get(), 0);
            if (text) {
                expired_trace_ids.emplace_back(reinterpret_cast<const char*>(text));
            }
            continue;
        }
        if (step_rc == SQLITE_DONE) {
            break;
        }
        persistence::checkSqliteError(db_, step_rc, "Step expired trace_id selection");
    }

    if (expired_trace_ids.empty()) {
        return 0;
    }

    // retention 只在 summary 表统一判断“哪条 trace 已经过期”，
    // 然后再按 trace_id 整条删，避免把一条调用树删成 summary 还在、span 只剩一半的脏状态。
    if (!DeleteTracesByIdsAtomic(expired_trace_ids)) {
        return 0;
    }
    return expired_trace_ids.size();
}

bool SqliteTraceRepository::DeleteTracesByIdsAtomic(const std::vector<std::string>& trace_ids)
{
    if (!db_ || trace_ids.empty()) {
        return false;
    }

    char* errmsg = nullptr;
    auto rollback = [this]() {
        char* rollback_err = nullptr;
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &rollback_err);
        if (rollback_err) {
            sqlite3_free(rollback_err);
        }
    };

    try {
        int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errmsg);
        if (errmsg) {
            sqlite3_free(errmsg);
            errmsg = nullptr;
        }
        persistence::checkSqliteError(db_, rc, "Failed to begin trace delete transaction");

        const std::string placeholders = BuildInClausePlaceholders(trace_ids.size());
        const std::string delete_analysis_sql =
            "DELETE FROM trace_analysis WHERE trace_id IN " + placeholders + ";";
        const std::string delete_span_sql =
            "DELETE FROM trace_span WHERE trace_id IN " + placeholders + ";";
        const std::string delete_summary_sql =
            "DELETE FROM trace_summary WHERE trace_id IN " + placeholders + ";";

        auto delete_by_ids = [this, &trace_ids](const std::string& sql, const char* error_context) -> size_t {
            persistence::StmtPtr stmt;
            sqlite3_stmt* raw_stmt = nullptr;
            int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &raw_stmt, nullptr);
            persistence::checkSqliteError(db_, rc, error_context);
            stmt.reset(raw_stmt);

            for (size_t index = 0; index < trace_ids.size(); ++index) {
                sqlite3_bind_text(stmt.get(),
                                  static_cast<int>(index + 1),
                                  trace_ids[index].c_str(),
                                  -1,
                                  SQLITE_TRANSIENT);
            }

            rc = sqlite3_step(stmt.get());
            persistence::checkSqliteError(db_, rc, error_context);
            return static_cast<size_t>(sqlite3_changes(db_));
        };

        // 当前外键没有配置 ON DELETE CASCADE，所以删除顺序必须是“从表 -> 父表”。
        // 也就是先清 trace_analysis、trace_span，最后再删 trace_summary。
        delete_by_ids(delete_analysis_sql, "Delete trace_analysis by trace_id");
        delete_by_ids(delete_span_sql, "Delete trace_span by trace_id");
        const size_t deleted_summary_rows =
            delete_by_ids(delete_summary_sql, "Delete trace_summary by trace_id");

        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errmsg);
        if (errmsg) {
            sqlite3_free(errmsg);
            errmsg = nullptr;
        }
        persistence::checkSqliteError(db_, rc, "Commit trace delete transaction");
        return deleted_summary_rows == trace_ids.size();
    } catch (const std::exception&) {
        rollback();
        return false;
    }
}

bool SqliteTraceRepository::SaveSingleTraceSummary(const TraceSummary& summary)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return false;
    }

    try {
        const char* sql_insert_summary = R"(
            INSERT INTO trace_summary
            (trace_id, service_name, start_time_ms, end_time_ms, duration_ms, span_count, token_count, risk_level, ai_status, ai_error)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";
        persistence::StmtPtr summary_stmt;
        {
            sqlite3_stmt* raw_stmt = nullptr;
            const int rc = sqlite3_prepare_v2(db_, sql_insert_summary, -1, &raw_stmt, nullptr);
            persistence::checkSqliteError(db_, rc, "Prepare trace_summary insert");
            summary_stmt.reset(raw_stmt);
        }
        // 这里沿用 SQLITE_STATIC：入参对象在 sqlite3_step 前保持存活，可以避免 SQLite 再做一次字符串拷贝。
        sqlite3_bind_text(summary_stmt.get(), 1, summary.trace_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(summary_stmt.get(), 2, summary.service_name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(summary_stmt.get(), 3, summary.start_time_ms);
        if (summary.end_time_ms.has_value()) {
            sqlite3_bind_int64(summary_stmt.get(), 4, summary.end_time_ms.value());
        } else {
            sqlite3_bind_null(summary_stmt.get(), 4);
        }
        sqlite3_bind_int64(summary_stmt.get(), 5, summary.duration_ms);
        sqlite3_bind_int64(summary_stmt.get(), 6, static_cast<sqlite3_int64>(summary.span_count));
        sqlite3_bind_int64(summary_stmt.get(), 7, static_cast<sqlite3_int64>(summary.token_count));
        sqlite3_bind_text(summary_stmt.get(), 8, summary.risk_level.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(summary_stmt.get(), 9, summary.ai_status.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(summary_stmt.get(), 10, summary.ai_error.c_str(), -1, SQLITE_STATIC);
        const int rc = sqlite3_step(summary_stmt.get());
        persistence::checkSqliteError(db_, rc, "Insert trace_summary");
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool SqliteTraceRepository::SaveSingleTraceSpans(const std::string& trace_id, const std::vector<TraceSpanRecord>& spans)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return false;
    }
    if (spans.empty()) {
        // 空集合直接成功返回，避免开启不必要事务，同时保持幂等调用语义。
        return true;
    }

    char* errmsg = nullptr;
    auto rollback = [this]() {
        char* rollback_err = nullptr;
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &rollback_err);
        if (rollback_err) {
            sqlite3_free(rollback_err);
        }
    };

    try {
        int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errmsg);
        if (errmsg) {
            sqlite3_free(errmsg);
            errmsg = nullptr;
        }
        persistence::checkSqliteError(db_, rc, "Failed to begin trace_span transaction");

        const char* sql_insert_span = R"(
            INSERT INTO trace_span
            (trace_id, span_id, parent_id, service_name, operation, start_time_ms, duration_ms, status, attributes_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";
        persistence::StmtPtr span_stmt;
        {
            sqlite3_stmt* raw_stmt = nullptr;
            rc = sqlite3_prepare_v2(db_, sql_insert_span, -1, &raw_stmt, nullptr);
            persistence::checkSqliteError(db_, rc, "Prepare trace_span insert");
            span_stmt.reset(raw_stmt);
        }

        for (const auto& span : spans) {
            sqlite3_reset(span_stmt.get());
            sqlite3_clear_bindings(span_stmt.get());

            if (!trace_id.empty() && !span.trace_id.empty() && span.trace_id != trace_id) {
                // 显式拦截 trace_id 不一致，避免“调用方参数”和“记录内容”冲突时写入脏数据。
                throw std::runtime_error("trace_id mismatch in SaveSingleTraceSpans");
            }
            const std::string& bind_trace_id = trace_id.empty() ? span.trace_id : trace_id;
            sqlite3_bind_text(span_stmt.get(), 1, bind_trace_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(span_stmt.get(), 2, span.span_id.c_str(), -1, SQLITE_STATIC);
            if (span.parent_id.has_value()) {
                sqlite3_bind_text(span_stmt.get(), 3, span.parent_id->c_str(), -1, SQLITE_STATIC);
            } else {
                sqlite3_bind_null(span_stmt.get(), 3);
            }
            sqlite3_bind_text(span_stmt.get(), 4, span.service_name.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(span_stmt.get(), 5, span.operation.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int64(span_stmt.get(), 6, span.start_time_ms);
            sqlite3_bind_int64(span_stmt.get(), 7, span.duration_ms);
            sqlite3_bind_text(span_stmt.get(), 8, span.status.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(span_stmt.get(), 9, span.attributes_json.c_str(), -1, SQLITE_STATIC);
            rc = sqlite3_step(span_stmt.get());
            persistence::checkSqliteError(db_, rc, "Insert trace_span");
        }

        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errmsg);
        if (errmsg) {
            sqlite3_free(errmsg);
            errmsg = nullptr;
        }
        persistence::checkSqliteError(db_, rc, "Commit trace_span transaction");
    } catch (const std::exception&) {
        rollback();
        return false;
    }
    return true;
}

bool SqliteTraceRepository::SaveSingleTraceAnalysis(const TraceAnalysisRecord& analysis)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return false;
    }

    char* errmsg = nullptr;
    auto rollback = [this]() {
        char* rollback_err = nullptr;
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &rollback_err);
        if (rollback_err) {
            sqlite3_free(rollback_err);
        }
    };

    try {
        int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errmsg);
        if (errmsg) {
            sqlite3_free(errmsg);
            errmsg = nullptr;
        }
        persistence::checkSqliteError(db_, rc, "Failed to begin trace_analysis transaction");

        const char* sql_insert_analysis = R"(
            INSERT INTO trace_analysis
            (trace_id, risk_level, summary, root_cause, solution, confidence)
            VALUES (?, ?, ?, ?, ?, ?);
        )";
        persistence::StmtPtr analysis_stmt;
        {
            sqlite3_stmt* raw_stmt = nullptr;
            rc = sqlite3_prepare_v2(db_, sql_insert_analysis, -1, &raw_stmt, nullptr);
            persistence::checkSqliteError(db_, rc, "Prepare trace_analysis insert");
            analysis_stmt.reset(raw_stmt);
        }
        sqlite3_bind_text(analysis_stmt.get(), 1, analysis.trace_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(analysis_stmt.get(), 2, analysis.risk_level.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(analysis_stmt.get(), 3, analysis.summary.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(analysis_stmt.get(), 4, analysis.root_cause.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(analysis_stmt.get(), 5, analysis.solution.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_double(analysis_stmt.get(), 6, analysis.confidence);
        rc = sqlite3_step(analysis_stmt.get());
        persistence::checkSqliteError(db_, rc, "Insert trace_analysis");

        // AI 成功时要把 risk_level 和 ai_status 一起回写到 summary。
        // 否则列表页会看到“风险等级变了，但状态还停在 pending”的脏组合。
        UpdateSummaryAnalysisOutcome(db_, analysis.trace_id, analysis.risk_level);

        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errmsg);
        if (errmsg) {
            sqlite3_free(errmsg);
            errmsg = nullptr;
        }
        persistence::checkSqliteError(db_, rc, "Commit trace_analysis transaction");
    } catch (const std::exception&) {
        rollback();
        return false;
    }
    return true;
}

bool SqliteTraceRepository::UpdateTraceAiState(const std::string& trace_id,
                                               const std::string& ai_status,
                                               const std::string& ai_error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return false;
    }

    try {
        UpdateSummaryAiState(db_, trace_id, ai_status, ai_error);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool SqliteTraceRepository::SavePrimaryBatch(const std::vector<TraceSummary>& summaries,
                                             const std::vector<TraceSpanRecord>& spans)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return false;
    }
    if (summaries.empty() && spans.empty()) {
        return true;
    }

    char* errmsg = nullptr;
    auto rollback = [this]() {
        char* rollback_err = nullptr;
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &rollback_err);
        if (rollback_err) {
            sqlite3_free(rollback_err);
        }
    };
    auto batch_edge_trace_id = [&summaries, &spans](bool first) -> std::string {
        // flush 失败时最怕只有一句“return false”，后面根本不知道是哪一批炸了。
        // 这里优先用 summary 的 trace_id 做边界；如果 summary 恰好为空，再退回 span 的 trace_id。
        if (!summaries.empty()) {
            return first ? summaries.front().trace_id : summaries.back().trace_id;
        }
        if (!spans.empty()) {
            return first ? spans.front().trace_id : spans.back().trace_id;
        }
        return "<empty>";
    };

    try {
        int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errmsg);
        if (errmsg) {
            sqlite3_free(errmsg);
            errmsg = nullptr;
        }
        persistence::checkSqliteError(db_, rc, "Failed to begin primary batch transaction");

        const char* sql_insert_summary = R"(
            INSERT INTO trace_summary
            (trace_id, service_name, start_time_ms, end_time_ms, duration_ms, span_count, token_count, risk_level, ai_status, ai_error)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";
        persistence::StmtPtr summary_stmt;
        {
            sqlite3_stmt* raw_stmt = nullptr;
            rc = sqlite3_prepare_v2(db_, sql_insert_summary, -1, &raw_stmt, nullptr);
            persistence::checkSqliteError(db_, rc, "Prepare trace_summary batch insert");
            summary_stmt.reset(raw_stmt);
        }

        for (const auto& summary : summaries) {
            sqlite3_reset(summary_stmt.get());
            sqlite3_clear_bindings(summary_stmt.get());

            sqlite3_bind_text(summary_stmt.get(), 1, summary.trace_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(summary_stmt.get(), 2, summary.service_name.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int64(summary_stmt.get(), 3, summary.start_time_ms);
            if (summary.end_time_ms.has_value()) {
                sqlite3_bind_int64(summary_stmt.get(), 4, summary.end_time_ms.value());
            } else {
                sqlite3_bind_null(summary_stmt.get(), 4);
            }
            sqlite3_bind_int64(summary_stmt.get(), 5, summary.duration_ms);
            sqlite3_bind_int64(summary_stmt.get(), 6, static_cast<sqlite3_int64>(summary.span_count));
            sqlite3_bind_int64(summary_stmt.get(), 7, static_cast<sqlite3_int64>(summary.token_count));
            sqlite3_bind_text(summary_stmt.get(), 8, summary.risk_level.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(summary_stmt.get(), 9, summary.ai_status.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(summary_stmt.get(), 10, summary.ai_error.c_str(), -1, SQLITE_STATIC);

            rc = sqlite3_step(summary_stmt.get());
            persistence::checkSqliteError(db_, rc, "Insert trace_summary batch item");
        }

        const char* sql_insert_span = R"(
            INSERT INTO trace_span
            (trace_id, span_id, parent_id, service_name, operation, start_time_ms, duration_ms, status, attributes_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";
        persistence::StmtPtr span_stmt;
        {
            sqlite3_stmt* raw_stmt = nullptr;
            rc = sqlite3_prepare_v2(db_, sql_insert_span, -1, &raw_stmt, nullptr);
            persistence::checkSqliteError(db_, rc, "Prepare trace_span batch insert");
            span_stmt.reset(raw_stmt);
        }

        for (const auto& span : spans) {
            sqlite3_reset(span_stmt.get());
            sqlite3_clear_bindings(span_stmt.get());

            sqlite3_bind_text(span_stmt.get(), 1, span.trace_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(span_stmt.get(), 2, span.span_id.c_str(), -1, SQLITE_STATIC);
            if (span.parent_id.has_value()) {
                sqlite3_bind_text(span_stmt.get(), 3, span.parent_id->c_str(), -1, SQLITE_STATIC);
            } else {
                sqlite3_bind_null(span_stmt.get(), 3);
            }
            sqlite3_bind_text(span_stmt.get(), 4, span.service_name.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(span_stmt.get(), 5, span.operation.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int64(span_stmt.get(), 6, span.start_time_ms);
            sqlite3_bind_int64(span_stmt.get(), 7, span.duration_ms);
            sqlite3_bind_text(span_stmt.get(), 8, span.status.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(span_stmt.get(), 9, span.attributes_json.c_str(), -1, SQLITE_STATIC);

            rc = sqlite3_step(span_stmt.get());
            persistence::checkSqliteError(db_, rc, "Insert trace_span batch item");
        }

        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errmsg);
        if (errmsg) {
            sqlite3_free(errmsg);
            errmsg = nullptr;
        }
        persistence::checkSqliteError(db_, rc, "Commit primary batch transaction");
    } catch (const std::exception& e) {
        std::cerr << "[SqliteTraceRepository] SavePrimaryBatch failed"
                  << " summary_count=" << summaries.size()
                  << " span_count=" << spans.size()
                  << " first_trace_id=" << batch_edge_trace_id(true)
                  << " last_trace_id=" << batch_edge_trace_id(false)
                  << " error=" << e.what()
                  << std::endl;
        rollback();
        return false;
    }

    return true;
}

bool SqliteTraceRepository::SaveAnalysisBatch(const std::vector<TraceAnalysisRecord>& analyses)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return false;
    }
    if (analyses.empty()) {
        return true;
    }

    char* errmsg = nullptr;
    auto rollback = [this]() {
        char* rollback_err = nullptr;
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &rollback_err);
        if (rollback_err) {
            sqlite3_free(rollback_err);
        }
    };

    try {
        int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errmsg);
        if (errmsg) {
            sqlite3_free(errmsg);
            errmsg = nullptr;
        }
        persistence::checkSqliteError(db_, rc, "Failed to begin analysis batch transaction");

        const char* sql_insert_analysis = R"(
            INSERT INTO trace_analysis
            (trace_id, risk_level, summary, root_cause, solution, confidence)
            VALUES (?, ?, ?, ?, ?, ?);
        )";
        persistence::StmtPtr analysis_stmt;
        {
            sqlite3_stmt* raw_stmt = nullptr;
            rc = sqlite3_prepare_v2(db_, sql_insert_analysis, -1, &raw_stmt, nullptr);
            persistence::checkSqliteError(db_, rc, "Prepare trace_analysis batch insert");
            analysis_stmt.reset(raw_stmt);
        }

        // analysis 虽然落在附属表，但列表页高频读取的还是 trace_summary。
        // 所以这里必须把最终 risk_level + ai_status 一起同步回写，避免读侧看到半成熟状态。
        const char* sql_update_summary_outcome = R"(
            UPDATE trace_summary
            SET risk_level = ?, ai_status = 'completed', ai_error = ''
            WHERE trace_id = ?;
        )";
        persistence::StmtPtr update_summary_outcome_stmt;
        {
            sqlite3_stmt* raw_stmt = nullptr;
            rc = sqlite3_prepare_v2(db_, sql_update_summary_outcome, -1, &raw_stmt, nullptr);
            persistence::checkSqliteError(db_, rc, "Prepare trace_summary outcome batch update");
            update_summary_outcome_stmt.reset(raw_stmt);
        }

        for (const auto& analysis : analyses) {
            sqlite3_reset(analysis_stmt.get());
            sqlite3_clear_bindings(analysis_stmt.get());

            sqlite3_bind_text(analysis_stmt.get(), 1, analysis.trace_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(analysis_stmt.get(), 2, analysis.risk_level.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(analysis_stmt.get(), 3, analysis.summary.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(analysis_stmt.get(), 4, analysis.root_cause.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(analysis_stmt.get(), 5, analysis.solution.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_double(analysis_stmt.get(), 6, analysis.confidence);

            rc = sqlite3_step(analysis_stmt.get());
            persistence::checkSqliteError(db_, rc, "Insert trace_analysis batch item");

            // 这里复用同一条 update stmt，避免 batch 内每条 analysis 都重新 prepare 一次 SQL。
            sqlite3_reset(update_summary_outcome_stmt.get());
            sqlite3_clear_bindings(update_summary_outcome_stmt.get());
            sqlite3_bind_text(update_summary_outcome_stmt.get(), 1, analysis.risk_level.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(update_summary_outcome_stmt.get(), 2, analysis.trace_id.c_str(), -1, SQLITE_STATIC);
            rc = sqlite3_step(update_summary_outcome_stmt.get());
            persistence::checkSqliteError(db_, rc, "Update trace_summary outcome batch item");
        }

        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errmsg);
        if (errmsg) {
            sqlite3_free(errmsg);
            errmsg = nullptr;
        }
        persistence::checkSqliteError(db_, rc, "Commit analysis batch transaction");
    } catch (const std::exception&) {
        rollback();
        return false;
    }

    return true;
}

bool SqliteTraceRepository::SaveSingleTraceAtomic(const TraceSummary& summary,
                                                  const std::vector<TraceSpanRecord>& spans,
                                                  const TraceAnalysisRecord* analysis)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return false;
    }

    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errmsg);
    if (errmsg) {
        sqlite3_free(errmsg);
        errmsg = nullptr;
    }
    persistence::checkSqliteError(db_, rc, "Failed to begin trace transaction");

    auto rollback = [this]() {
        char* rollback_err = nullptr;
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &rollback_err);
        if (rollback_err) {
            sqlite3_free(rollback_err);
        }
    };

    try {
        const char* sql_insert_summary = R"(
            INSERT INTO trace_summary
            (trace_id, service_name, start_time_ms, end_time_ms, duration_ms, span_count, token_count, risk_level, ai_status, ai_error)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";
        persistence::StmtPtr summary_stmt;
        {
            sqlite3_stmt* raw_stmt = nullptr;
            rc = sqlite3_prepare_v2(db_, sql_insert_summary, -1, &raw_stmt, nullptr);
            persistence::checkSqliteError(db_, rc, "Prepare trace_summary insert");
            summary_stmt.reset(raw_stmt);
        }
        // 这里绑定的字符串来自入参对象，生命周期覆盖 sqlite3_step，因此使用 SQLITE_STATIC 减少拷贝。
        sqlite3_bind_text(summary_stmt.get(), 1, summary.trace_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(summary_stmt.get(), 2, summary.service_name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(summary_stmt.get(), 3, summary.start_time_ms);
        if (summary.end_time_ms.has_value()) {
            sqlite3_bind_int64(summary_stmt.get(), 4, summary.end_time_ms.value());
        } else {
            sqlite3_bind_null(summary_stmt.get(), 4);
        }
        sqlite3_bind_int64(summary_stmt.get(), 5, summary.duration_ms);
        sqlite3_bind_int64(summary_stmt.get(), 6, static_cast<sqlite3_int64>(summary.span_count));
        sqlite3_bind_int64(summary_stmt.get(), 7, static_cast<sqlite3_int64>(summary.token_count));
        sqlite3_bind_text(summary_stmt.get(), 8, summary.risk_level.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(summary_stmt.get(), 9, summary.ai_status.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(summary_stmt.get(), 10, summary.ai_error.c_str(), -1, SQLITE_STATIC);
        rc = sqlite3_step(summary_stmt.get());
        persistence::checkSqliteError(db_, rc, "Insert trace_summary");

        const char* sql_insert_span = R"(
            INSERT INTO trace_span
            (trace_id, span_id, parent_id, service_name, operation, start_time_ms, duration_ms, status, attributes_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";
        persistence::StmtPtr span_stmt;
        {
            sqlite3_stmt* raw_stmt = nullptr;
            rc = sqlite3_prepare_v2(db_, sql_insert_span, -1, &raw_stmt, nullptr);
            persistence::checkSqliteError(db_, rc, "Prepare trace_span insert");
            span_stmt.reset(raw_stmt);
        }
        for (const auto& span : spans) {
            sqlite3_reset(span_stmt.get());
            sqlite3_clear_bindings(span_stmt.get());
            sqlite3_bind_text(span_stmt.get(), 1, span.trace_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(span_stmt.get(), 2, span.span_id.c_str(), -1, SQLITE_STATIC);
            if (span.parent_id.has_value()) {
                sqlite3_bind_text(span_stmt.get(), 3, span.parent_id->c_str(), -1, SQLITE_STATIC);
            } else {
                sqlite3_bind_null(span_stmt.get(), 3);
            }
            sqlite3_bind_text(span_stmt.get(), 4, span.service_name.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(span_stmt.get(), 5, span.operation.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int64(span_stmt.get(), 6, span.start_time_ms);
            sqlite3_bind_int64(span_stmt.get(), 7, span.duration_ms);
            sqlite3_bind_text(span_stmt.get(), 8, span.status.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(span_stmt.get(), 9, span.attributes_json.c_str(), -1, SQLITE_STATIC);
            rc = sqlite3_step(span_stmt.get());
            persistence::checkSqliteError(db_, rc, "Insert trace_span");
        }

        // 原子写现在只覆盖主链真实会被查询与展示的三段数据：
        // summary / spans / analysis。那张废弃调试附属表已经退出产品路径，这里不再写它。
        if (analysis) {
            const char* sql_insert_analysis = R"(
                INSERT INTO trace_analysis
                (trace_id, risk_level, summary, root_cause, solution, confidence)
                VALUES (?, ?, ?, ?, ?, ?);
            )";
            persistence::StmtPtr analysis_stmt;
            {
                sqlite3_stmt* raw_stmt = nullptr;
                rc = sqlite3_prepare_v2(db_, sql_insert_analysis, -1, &raw_stmt, nullptr);
                persistence::checkSqliteError(db_, rc, "Prepare trace_analysis insert");
                analysis_stmt.reset(raw_stmt);
            }
            sqlite3_bind_text(analysis_stmt.get(), 1, analysis->trace_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(analysis_stmt.get(), 2, analysis->risk_level.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(analysis_stmt.get(), 3, analysis->summary.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(analysis_stmt.get(), 4, analysis->root_cause.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(analysis_stmt.get(), 5, analysis->solution.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_double(analysis_stmt.get(), 6, analysis->confidence);
            rc = sqlite3_step(analysis_stmt.get());
            persistence::checkSqliteError(db_, rc, "Insert trace_analysis");

            UpdateSummaryAnalysisOutcome(db_, analysis->trace_id, analysis->risk_level);
        }

        rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errmsg);
        if (errmsg) {
            sqlite3_free(errmsg);
            errmsg = nullptr;
        }
        persistence::checkSqliteError(db_, rc, "Commit trace transaction");
    } catch (const std::exception&) {
        rollback();
        return false;
    }

    return true;
}
