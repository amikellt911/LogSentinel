#include "persistence/SqliteTraceRepository.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <sqlite3.h>
#include "persistence/SqliteHelper.h"

SqliteTraceRepository::SqliteTraceRepository(const std::string& db_path)
    : db_path_(db_path)
{
    std::string final_path;
    // 兼容 :memory: 与普通文件路径，保持与现有 SQLite 仓库一致的路径策略，避免配置分叉。
    if (db_path_ == ":memory:") {
        final_path = db_path_;
    } else if (db_path_.find('/') != std::string::npos || db_path_.find('\\') != std::string::npos) {
        final_path = db_path_;
    } else {
        std::string data_path = "../persistence/data/";
        if (!std::filesystem::exists(data_path)) {
            std::filesystem::create_directories(data_path);
        }
        final_path = data_path + db_path_;
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
  risk_level TEXT NOT NULL
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

CREATE TABLE IF NOT EXISTS prompt_debug (
  trace_id TEXT PRIMARY KEY,
  input_json TEXT NOT NULL,
  output_json TEXT NOT NULL,
  model TEXT NOT NULL,
  duration_ms INTEGER NOT NULL,
  total_tokens INTEGER NOT NULL,
  timestamp TEXT NOT NULL,
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
CREATE INDEX IF NOT EXISTS idx_trace_summary_start_time ON trace_summary(start_time_ms);
CREATE INDEX IF NOT EXISTS idx_trace_summary_service_name ON trace_summary(service_name);
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

bool SqliteTraceRepository::SaveSingleTraceSummary(const TraceSummary& summary)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return false;
    }

    try {
        const char* sql_insert_summary = R"(
            INSERT INTO trace_summary
            (trace_id, service_name, start_time_ms, end_time_ms, duration_ms, span_count, token_count, risk_level)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?);
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

    try {
        const char* sql_insert_analysis = R"(
            INSERT INTO trace_analysis
            (trace_id, risk_level, summary, root_cause, solution, confidence)
            VALUES (?, ?, ?, ?, ?, ?);
        )";
        persistence::StmtPtr analysis_stmt;
        {
            sqlite3_stmt* raw_stmt = nullptr;
            const int rc = sqlite3_prepare_v2(db_, sql_insert_analysis, -1, &raw_stmt, nullptr);
            persistence::checkSqliteError(db_, rc, "Prepare trace_analysis insert");
            analysis_stmt.reset(raw_stmt);
        }
        sqlite3_bind_text(analysis_stmt.get(), 1, analysis.trace_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(analysis_stmt.get(), 2, analysis.risk_level.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(analysis_stmt.get(), 3, analysis.summary.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(analysis_stmt.get(), 4, analysis.root_cause.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(analysis_stmt.get(), 5, analysis.solution.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_double(analysis_stmt.get(), 6, analysis.confidence);
        const int rc = sqlite3_step(analysis_stmt.get());
        persistence::checkSqliteError(db_, rc, "Insert trace_analysis");
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool SqliteTraceRepository::SaveSinglePromptDebug(const PromptDebugRecord& record)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return false;
    }

    try {
        const char* sql_insert_prompt = R"(
            INSERT INTO prompt_debug
            (trace_id, input_json, output_json, model, duration_ms, total_tokens, timestamp)
            VALUES (?, ?, ?, ?, ?, ?, ?);
        )";
        persistence::StmtPtr prompt_stmt;
        {
            sqlite3_stmt* raw_stmt = nullptr;
            const int rc = sqlite3_prepare_v2(db_, sql_insert_prompt, -1, &raw_stmt, nullptr);
            persistence::checkSqliteError(db_, rc, "Prepare prompt_debug insert");
            prompt_stmt.reset(raw_stmt);
        }
        sqlite3_bind_text(prompt_stmt.get(), 1, record.trace_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(prompt_stmt.get(), 2, record.input_json.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(prompt_stmt.get(), 3, record.output_json.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(prompt_stmt.get(), 4, record.model.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(prompt_stmt.get(), 5, record.duration_ms);
        sqlite3_bind_int64(prompt_stmt.get(), 6, static_cast<sqlite3_int64>(record.total_tokens));
        sqlite3_bind_text(prompt_stmt.get(), 7, record.timestamp.c_str(), -1, SQLITE_STATIC);
        const int rc = sqlite3_step(prompt_stmt.get());
        persistence::checkSqliteError(db_, rc, "Insert prompt_debug");
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
            (trace_id, service_name, start_time_ms, end_time_ms, duration_ms, span_count, token_count, risk_level)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?);
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

bool SqliteTraceRepository::SaveAnalysisBatch(const std::vector<TraceAnalysisRecord>& analyses,
                                              const std::vector<PromptDebugRecord>& prompt_debugs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return false;
    }
    if (analyses.empty() && prompt_debugs.empty()) {
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
        }

        const char* sql_insert_prompt = R"(
            INSERT INTO prompt_debug
            (trace_id, input_json, output_json, model, duration_ms, total_tokens, timestamp)
            VALUES (?, ?, ?, ?, ?, ?, ?);
        )";
        persistence::StmtPtr prompt_stmt;
        {
            sqlite3_stmt* raw_stmt = nullptr;
            rc = sqlite3_prepare_v2(db_, sql_insert_prompt, -1, &raw_stmt, nullptr);
            persistence::checkSqliteError(db_, rc, "Prepare prompt_debug batch insert");
            prompt_stmt.reset(raw_stmt);
        }

        for (const auto& prompt_debug : prompt_debugs) {
            sqlite3_reset(prompt_stmt.get());
            sqlite3_clear_bindings(prompt_stmt.get());

            sqlite3_bind_text(prompt_stmt.get(), 1, prompt_debug.trace_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(prompt_stmt.get(), 2, prompt_debug.input_json.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(prompt_stmt.get(), 3, prompt_debug.output_json.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(prompt_stmt.get(), 4, prompt_debug.model.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int64(prompt_stmt.get(), 5, prompt_debug.duration_ms);
            sqlite3_bind_int64(prompt_stmt.get(), 6, static_cast<sqlite3_int64>(prompt_debug.total_tokens));
            sqlite3_bind_text(prompt_stmt.get(), 7, prompt_debug.timestamp.c_str(), -1, SQLITE_STATIC);

            rc = sqlite3_step(prompt_stmt.get());
            persistence::checkSqliteError(db_, rc, "Insert prompt_debug batch item");
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
                                           const TraceAnalysisRecord* analysis,
                                           const PromptDebugRecord* prompt_debug)
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
            (trace_id, service_name, start_time_ms, end_time_ms, duration_ms, span_count, token_count, risk_level)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?);
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
        }

        if (prompt_debug) {
            const char* sql_insert_prompt = R"(
                INSERT INTO prompt_debug
                (trace_id, input_json, output_json, model, duration_ms, total_tokens, timestamp)
                VALUES (?, ?, ?, ?, ?, ?, ?);
            )";
            persistence::StmtPtr prompt_stmt;
            {
                sqlite3_stmt* raw_stmt = nullptr;
                rc = sqlite3_prepare_v2(db_, sql_insert_prompt, -1, &raw_stmt, nullptr);
                persistence::checkSqliteError(db_, rc, "Prepare prompt_debug insert");
                prompt_stmt.reset(raw_stmt);
            }
            sqlite3_bind_text(prompt_stmt.get(), 1, prompt_debug->trace_id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(prompt_stmt.get(), 2, prompt_debug->input_json.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(prompt_stmt.get(), 3, prompt_debug->output_json.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(prompt_stmt.get(), 4, prompt_debug->model.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int64(prompt_stmt.get(), 5, prompt_debug->duration_ms);
            sqlite3_bind_int64(prompt_stmt.get(), 6, static_cast<sqlite3_int64>(prompt_debug->total_tokens));
            sqlite3_bind_text(prompt_stmt.get(), 7, prompt_debug->timestamp.c_str(), -1, SQLITE_STATIC);
            rc = sqlite3_step(prompt_stmt.get());
            persistence::checkSqliteError(db_, rc, "Insert prompt_debug");
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
