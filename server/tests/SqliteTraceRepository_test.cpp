#include <filesystem>
#include <gtest/gtest.h>
#include <optional>
#include <sqlite3.h>
#include "persistence/SqliteTraceRepository.h"

class SqliteTraceRepositoryTest : public ::testing::Test
{
protected:
    void SetUp() override {
        db_path = "./test_trace_repo.db";
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove(db_path);
        }
        repo = std::make_unique<SqliteTraceRepository>(db_path);
    }

    void TearDown() override {
        repo.reset();
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove(db_path);
        }
    }

    int QueryCount(const std::string& sql) {
        sqlite3* db = nullptr;
        int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK) {
            if (db) {
                sqlite3_close_v2(db);
            }
            return -1;
        }
        sqlite3_stmt* stmt = nullptr;
        rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            sqlite3_close_v2(db);
            return -1;
        }
        int count = -1;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        sqlite3_close_v2(db);
        return count;
    }

    struct SummaryRow {
        std::string service_name;
        int64_t start_time_ms = 0;
        int64_t duration_ms = 0;
        std::string risk_level;
    };

    std::optional<SummaryRow> QuerySummary(const std::string& trace_id) {
        sqlite3* db = nullptr;
        int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK) {
            if (db) {
                sqlite3_close_v2(db);
            }
            return std::nullopt;
        }
        const char* sql = R"(
            SELECT service_name, start_time_ms, duration_ms, risk_level
            FROM trace_summary
            WHERE trace_id = ?;
        )";
        sqlite3_stmt* stmt = nullptr;
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            sqlite3_close_v2(db);
            return std::nullopt;
        }
        sqlite3_bind_text(stmt, 1, trace_id.c_str(), -1, SQLITE_TRANSIENT);
        std::optional<SummaryRow> row;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            SummaryRow out;
            out.service_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            out.start_time_ms = sqlite3_column_int64(stmt, 1);
            out.duration_ms = sqlite3_column_int64(stmt, 2);
            out.risk_level = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            row = out;
        }
        sqlite3_finalize(stmt);
        sqlite3_close_v2(db);
        return row;
    }

    persistence::TraceSummary MakeSummary(const std::string& trace_id) {
        persistence::TraceSummary summary;
        summary.trace_id = trace_id;
        summary.service_name = "service";
        summary.start_time_ms = 100;
        summary.end_time_ms = 200;
        summary.duration_ms = 100;
        summary.span_count = 2;
        summary.token_count = 10;
        summary.risk_level = "unknown";
        return summary;
    }

    persistence::TraceSpanRecord MakeSpan(const std::string& trace_id,
                                          const std::string& span_id,
                                          const std::string& parent_id) {
        persistence::TraceSpanRecord span;
        span.trace_id = trace_id;
        span.span_id = span_id;
        span.parent_id = parent_id.empty() ? std::optional<std::string>{} : std::optional<std::string>(parent_id);
        span.service_name = "service";
        span.operation = "op";
        span.start_time_ms = 100;
        span.duration_ms = 50;
        span.status = "OK";
        span.attributes_json = "{}";
        return span;
    }

    persistence::TraceAnalysisRecord MakeAnalysis(const std::string& trace_id) {
        persistence::TraceAnalysisRecord analysis;
        analysis.trace_id = trace_id;
        analysis.risk_level = "warning";
        analysis.summary = "summary";
        analysis.root_cause = "root";
        analysis.solution = "solution";
        analysis.confidence = 0.5;
        return analysis;
    }

    persistence::PromptDebugRecord MakePrompt(const std::string& trace_id) {
        persistence::PromptDebugRecord prompt;
        prompt.trace_id = trace_id;
        prompt.input_json = "{}";
        prompt.output_json = "{}";
        prompt.model = "gemini-2.0-flash-exp";
        prompt.duration_ms = 410;
        prompt.total_tokens = 685;
        prompt.timestamp = "2026-01-09 10:00:00";
        return prompt;
    }

protected:
    std::unique_ptr<SqliteTraceRepository> repo;
    std::string db_path;
};

TEST_F(SqliteTraceRepositoryTest, SaveSingleTraceAtomicSummaryAndSpans)
{
    persistence::TraceSummary summary = MakeSummary("trace-1");
    std::vector<persistence::TraceSpanRecord> spans;
    spans.push_back(MakeSpan("trace-1", "span-1", ""));
    spans.push_back(MakeSpan("trace-1", "span-2", "span-1"));

    bool ok = repo->SaveSingleTraceAtomic(summary, spans, nullptr, nullptr);
    EXPECT_TRUE(ok);

    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_summary;"), 1);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_span;"), 2);
}

TEST_F(SqliteTraceRepositoryTest, SaveSingleTraceSummaryPersistsRow)
{
    persistence::TraceSummary summary = MakeSummary("single-summary-1");
    bool ok = repo->SaveSingleTraceSummary(summary);
    EXPECT_TRUE(ok);

    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_summary;"), 1);
    auto row = QuerySummary("single-summary-1");
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(row->service_name, summary.service_name);
    EXPECT_EQ(row->duration_ms, summary.duration_ms);
}

TEST_F(SqliteTraceRepositoryTest, SaveSingleTraceSpansPersistsRows)
{
    persistence::TraceSummary summary = MakeSummary("single-span-1");
    ASSERT_TRUE(repo->SaveSingleTraceSummary(summary));

    std::vector<persistence::TraceSpanRecord> spans;
    spans.push_back(MakeSpan("single-span-1", "span-1", ""));
    spans.push_back(MakeSpan("single-span-1", "span-2", "span-1"));

    bool ok = repo->SaveSingleTraceSpans(summary.trace_id, spans);
    EXPECT_TRUE(ok);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_span;"), 2);
}

TEST_F(SqliteTraceRepositoryTest, SaveSingleTraceSpansRollbackOnTraceIdMismatch)
{
    persistence::TraceSummary summary = MakeSummary("single-span-2");
    ASSERT_TRUE(repo->SaveSingleTraceSummary(summary));

    std::vector<persistence::TraceSpanRecord> spans;
    spans.push_back(MakeSpan("single-span-2", "span-1", ""));
    spans.push_back(MakeSpan("other-trace", "span-2", "span-1"));

    bool ok = repo->SaveSingleTraceSpans(summary.trace_id, spans);
    EXPECT_FALSE(ok);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_span;"), 0);
}

TEST_F(SqliteTraceRepositoryTest, SaveSingleTraceAnalysisPersistsRow)
{
    persistence::TraceSummary summary = MakeSummary("single-analysis-1");
    ASSERT_TRUE(repo->SaveSingleTraceSummary(summary));

    persistence::TraceAnalysisRecord analysis = MakeAnalysis(summary.trace_id);
    bool ok = repo->SaveSingleTraceAnalysis(analysis);
    EXPECT_TRUE(ok);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_analysis;"), 1);
}

TEST_F(SqliteTraceRepositoryTest, SaveSingleTraceAnalysisFailsWithoutSummary)
{
    persistence::TraceAnalysisRecord analysis = MakeAnalysis("missing-trace");
    bool ok = repo->SaveSingleTraceAnalysis(analysis);
    EXPECT_FALSE(ok);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_analysis;"), 0);
}

TEST_F(SqliteTraceRepositoryTest, SaveSinglePromptDebugPersistsRow)
{
    persistence::TraceSummary summary = MakeSummary("single-prompt-1");
    ASSERT_TRUE(repo->SaveSingleTraceSummary(summary));

    persistence::PromptDebugRecord prompt = MakePrompt(summary.trace_id);
    bool ok = repo->SaveSinglePromptDebug(prompt);
    EXPECT_TRUE(ok);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM prompt_debug;"), 1);
}

TEST_F(SqliteTraceRepositoryTest, SaveSinglePromptDebugFailsWithoutSummary)
{
    persistence::PromptDebugRecord prompt = MakePrompt("missing-trace");
    bool ok = repo->SaveSinglePromptDebug(prompt);
    EXPECT_FALSE(ok);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM prompt_debug;"), 0);
}

TEST_F(SqliteTraceRepositoryTest, SaveSingleTraceAtomicPersistsSummaryFields)
{
    persistence::TraceSummary summary = MakeSummary("trace-1");
    std::vector<persistence::TraceSpanRecord> spans;
    spans.push_back(MakeSpan("trace-1", "span-1", ""));

    bool ok = repo->SaveSingleTraceAtomic(summary, spans, nullptr, nullptr);
    EXPECT_TRUE(ok);

    auto row = QuerySummary("trace-1");
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(row->service_name, summary.service_name);
    EXPECT_EQ(row->start_time_ms, summary.start_time_ms);
    EXPECT_EQ(row->duration_ms, summary.duration_ms);
    EXPECT_EQ(row->risk_level, summary.risk_level);
}

TEST_F(SqliteTraceRepositoryTest, SaveSingleTraceAtomicWithAnalysis)
{
    persistence::TraceSummary summary = MakeSummary("trace-2");
    std::vector<persistence::TraceSpanRecord> spans;
    spans.push_back(MakeSpan("trace-2", "span-1", ""));

    persistence::TraceAnalysisRecord analysis = MakeAnalysis("trace-2");

    bool ok = repo->SaveSingleTraceAtomic(summary, spans, &analysis, nullptr);
    EXPECT_TRUE(ok);

    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_analysis;"), 1);
}

TEST_F(SqliteTraceRepositoryTest, SaveSingleTraceAtomicWithPromptDebug)
{
    persistence::TraceSummary summary = MakeSummary("trace-3");
    std::vector<persistence::TraceSpanRecord> spans;
    spans.push_back(MakeSpan("trace-3", "span-1", ""));

    persistence::PromptDebugRecord prompt = MakePrompt("trace-3");

    bool ok = repo->SaveSingleTraceAtomic(summary, spans, nullptr, &prompt);
    EXPECT_TRUE(ok);

    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM prompt_debug;"), 1);
}

TEST_F(SqliteTraceRepositoryTest, SaveSingleTraceAtomicRejectsOrphanSpan)
{
    persistence::TraceSummary summary = MakeSummary("trace-4");
    std::vector<persistence::TraceSpanRecord> spans;
    spans.push_back(MakeSpan("trace-4", "span-1", ""));

    bool ok = repo->SaveSingleTraceAtomic(summary, spans, nullptr, nullptr);
    EXPECT_TRUE(ok);

    // 直接写入不存在 trace_summary 的 span，应触发外键失败并返回 false
    persistence::TraceSummary other_summary = MakeSummary("trace-5");
    (void)other_summary;
    std::vector<persistence::TraceSpanRecord> bad_spans;
    bad_spans.push_back(MakeSpan("missing-trace", "span-x", ""));

    bool bad = repo->SaveSingleTraceAtomic(summary, bad_spans, nullptr, nullptr);
    EXPECT_FALSE(bad);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_span;"), 1);
}
