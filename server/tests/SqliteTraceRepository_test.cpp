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

    bool TableExists(const std::string& table_name) {
        sqlite3* db = nullptr;
        int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK) {
            if (db) {
                sqlite3_close_v2(db);
            }
            return false;
        }
        const char* sql = R"(
            SELECT COUNT(*)
            FROM sqlite_master
            WHERE type = 'table' AND name = ?;
        )";
        sqlite3_stmt* stmt = nullptr;
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            sqlite3_close_v2(db);
            return false;
        }
        sqlite3_bind_text(stmt, 1, table_name.c_str(), -1, SQLITE_TRANSIENT);
        bool exists = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            exists = sqlite3_column_int(stmt, 0) > 0;
        }
        sqlite3_finalize(stmt);
        sqlite3_close_v2(db);
        return exists;
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

    bool ok = repo->SaveSingleTraceAtomic(summary, spans, nullptr);
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

TEST_F(SqliteTraceRepositoryTest, SchemaDoesNotCreatePromptDebugTable)
{
    // prompt_debug 已经不在 MVP5 主链产品范围里，
    // 所以仓储初始化后不应该再顺手创建这张“没人读、没人用”的表。
    EXPECT_FALSE(TableExists("prompt_debug"));
}

TEST_F(SqliteTraceRepositoryTest, SaveSingleTraceAtomicPersistsSummaryFields)
{
    persistence::TraceSummary summary = MakeSummary("trace-1");
    std::vector<persistence::TraceSpanRecord> spans;
    spans.push_back(MakeSpan("trace-1", "span-1", ""));

    bool ok = repo->SaveSingleTraceAtomic(summary, spans, nullptr);
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

    // 原子写现在只覆盖 summary / spans / analysis，
    // 所以这个用例就是新的完整路径，不再额外夹带任何废弃调试附属表。
    bool ok = repo->SaveSingleTraceAtomic(summary, spans, &analysis);
    EXPECT_TRUE(ok);

    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_analysis;"), 1);
}

TEST_F(SqliteTraceRepositoryTest, SaveSingleTraceAtomicRejectsOrphanSpan)
{
    persistence::TraceSummary summary = MakeSummary("trace-4");
    std::vector<persistence::TraceSpanRecord> spans;
    spans.push_back(MakeSpan("trace-4", "span-1", ""));

    bool ok = repo->SaveSingleTraceAtomic(summary, spans, nullptr);
    EXPECT_TRUE(ok);

    // 直接写入不存在 trace_summary 的 span，应触发外键失败并返回 false
    persistence::TraceSummary other_summary = MakeSummary("trace-5");
    (void)other_summary;
    std::vector<persistence::TraceSpanRecord> bad_spans;
    bad_spans.push_back(MakeSpan("missing-trace", "span-x", ""));

    bool bad = repo->SaveSingleTraceAtomic(summary, bad_spans, nullptr);
    EXPECT_FALSE(bad);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_span;"), 1);
}

TEST_F(SqliteTraceRepositoryTest, GetTraceDetailReturnsSummarySpansAndAnalysis)
{
    persistence::TraceSummary summary = MakeSummary("detail-trace-1");
    summary.service_name = "order-service";
    summary.start_time_ms = 1000;
    summary.end_time_ms = 1600;
    summary.duration_ms = 600;
    summary.span_count = 2;
    summary.token_count = 42;
    ASSERT_TRUE(repo->SaveSingleTraceSummary(summary));

    persistence::TraceSpanRecord child_span = MakeSpan(summary.trace_id, "span-2", "span-1");
    child_span.service_name = "payment-service";
    child_span.operation = "charge";
    child_span.start_time_ms = 1200;
    child_span.duration_ms = 200;

    persistence::TraceSpanRecord root_span = MakeSpan(summary.trace_id, "span-1", "");
    root_span.service_name = "order-service";
    root_span.operation = "create-order";
    root_span.start_time_ms = 1000;
    root_span.duration_ms = 600;

    std::vector<persistence::TraceSpanRecord> spans;
    // 故意按乱序写入，验证详情查询会按 start_time_ms ASC, span_id ASC 排好。
    spans.push_back(child_span);
    spans.push_back(root_span);
    ASSERT_TRUE(repo->SaveSingleTraceSpans(summary.trace_id, spans));

    persistence::TraceAnalysisRecord analysis = MakeAnalysis(summary.trace_id);
    analysis.risk_level = "critical";
    analysis.summary = "trace summary";
    analysis.root_cause = "db slow";
    analysis.solution = "scale out";
    analysis.confidence = 0.91;
    ASSERT_TRUE(repo->SaveSingleTraceAnalysis(analysis));

    std::optional<TraceDetailRecord> detail = repo->GetTraceDetail(summary.trace_id);
    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ(detail->trace_id, summary.trace_id);
    EXPECT_EQ(detail->service_name, summary.service_name);
    EXPECT_EQ(detail->duration_ms, summary.duration_ms);
    EXPECT_EQ(detail->token_count, summary.token_count);
    EXPECT_EQ(detail->risk_level, analysis.risk_level);
    EXPECT_TRUE(detail->tags.empty());

    ASSERT_TRUE(detail->analysis.has_value());
    EXPECT_EQ(detail->analysis->risk_level, analysis.risk_level);
    EXPECT_EQ(detail->analysis->summary, analysis.summary);
    EXPECT_EQ(detail->analysis->root_cause, analysis.root_cause);
    EXPECT_EQ(detail->analysis->solution, analysis.solution);
    EXPECT_DOUBLE_EQ(detail->analysis->confidence, analysis.confidence);

    ASSERT_EQ(detail->spans.size(), 2u);
    EXPECT_EQ(detail->spans[0].span_id, "span-1");
    EXPECT_EQ(detail->spans[0].service_name, "order-service");
    EXPECT_EQ(detail->spans[0].raw_status, "OK");
    EXPECT_EQ(detail->spans[1].span_id, "span-2");
    ASSERT_TRUE(detail->spans[1].parent_id.has_value());
    EXPECT_EQ(detail->spans[1].parent_id.value(), "span-1");
}

TEST_F(SqliteTraceRepositoryTest, GetTraceDetailReturnsNulloptWhenTraceMissing)
{
    std::optional<TraceDetailRecord> detail = repo->GetTraceDetail("missing-trace");
    EXPECT_FALSE(detail.has_value());
}

TEST_F(SqliteTraceRepositoryTest, DeleteTraceByIdRemovesSummarySpansAndAnalysisTogether)
{
    persistence::TraceSummary summary = MakeSummary("delete-trace-1");
    std::vector<persistence::TraceSpanRecord> spans;
    spans.push_back(MakeSpan(summary.trace_id, "span-1", ""));
    spans.push_back(MakeSpan(summary.trace_id, "span-2", "span-1"));
    persistence::TraceAnalysisRecord analysis = MakeAnalysis(summary.trace_id);

    ASSERT_TRUE(repo->SaveSingleTraceAtomic(summary, spans, &analysis));
    ASSERT_EQ(QueryCount("SELECT COUNT(*) FROM trace_summary;"), 1);
    ASSERT_EQ(QueryCount("SELECT COUNT(*) FROM trace_span;"), 2);
    ASSERT_EQ(QueryCount("SELECT COUNT(*) FROM trace_analysis;"), 1);

    EXPECT_TRUE(repo->DeleteTraceById(summary.trace_id));
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_summary;"), 0);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_span;"), 0);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_analysis;"), 0);
}

TEST_F(SqliteTraceRepositoryTest, DeleteExpiredTracesBatchRemovesOnlyExpiredTraceIds)
{
    persistence::TraceSummary expired_with_end = MakeSummary("expired-end");
    expired_with_end.start_time_ms = 1000;
    expired_with_end.end_time_ms = 1500;

    persistence::TraceSummary expired_without_end = MakeSummary("expired-start");
    expired_without_end.start_time_ms = 1800;
    expired_without_end.end_time_ms.reset();

    persistence::TraceSummary fresh = MakeSummary("fresh-trace");
    fresh.start_time_ms = 4000;
    fresh.end_time_ms = 5000;
    persistence::TraceAnalysisRecord expired_with_end_analysis = MakeAnalysis(expired_with_end.trace_id);
    persistence::TraceAnalysisRecord expired_without_end_analysis = MakeAnalysis(expired_without_end.trace_id);
    persistence::TraceAnalysisRecord fresh_analysis = MakeAnalysis(fresh.trace_id);

    ASSERT_TRUE(repo->SaveSingleTraceAtomic(
        expired_with_end,
        {MakeSpan(expired_with_end.trace_id, "span-a", "")},
        &expired_with_end_analysis));
    ASSERT_TRUE(repo->SaveSingleTraceAtomic(
        expired_without_end,
        {MakeSpan(expired_without_end.trace_id, "span-b", "")},
        &expired_without_end_analysis));
    ASSERT_TRUE(repo->SaveSingleTraceAtomic(
        fresh,
        {MakeSpan(fresh.trace_id, "span-c", "")},
        &fresh_analysis));

    const size_t deleted = repo->DeleteExpiredTracesBatch(/*cutoff_ms*/3000, /*limit*/10);
    EXPECT_EQ(deleted, 2u);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_summary;"), 1);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_span;"), 1);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_analysis;"), 1);
    EXPECT_TRUE(QuerySummary("fresh-trace").has_value());
    EXPECT_FALSE(QuerySummary("expired-end").has_value());
    EXPECT_FALSE(QuerySummary("expired-start").has_value());
}

TEST_F(SqliteTraceRepositoryTest, DeleteExpiredTracesBatchDeletesOldestExpiredTracesFirst)
{
    persistence::TraceSummary oldest = MakeSummary("trace-oldest");
    oldest.start_time_ms = 1000;
    oldest.end_time_ms = 1100;

    persistence::TraceSummary middle = MakeSummary("trace-middle");
    middle.start_time_ms = 2000;
    middle.end_time_ms = 2100;

    persistence::TraceSummary newest_expired = MakeSummary("trace-newest-expired");
    newest_expired.start_time_ms = 2500;
    newest_expired.end_time_ms = 2600;
    persistence::TraceAnalysisRecord oldest_analysis = MakeAnalysis(oldest.trace_id);
    persistence::TraceAnalysisRecord middle_analysis = MakeAnalysis(middle.trace_id);
    persistence::TraceAnalysisRecord newest_analysis = MakeAnalysis(newest_expired.trace_id);

    ASSERT_TRUE(repo->SaveSingleTraceAtomic(oldest,
                                            {MakeSpan(oldest.trace_id, "span-oldest", "")},
                                            &oldest_analysis));
    ASSERT_TRUE(repo->SaveSingleTraceAtomic(middle,
                                            {MakeSpan(middle.trace_id, "span-middle", "")},
                                            &middle_analysis));
    ASSERT_TRUE(repo->SaveSingleTraceAtomic(newest_expired,
                                            {MakeSpan(newest_expired.trace_id, "span-newest", "")},
                                            &newest_analysis));

    // 这里锁死“最老优先 + limit 生效”的删除语义，避免 retention 一轮删的是随机过期 trace。
    const size_t deleted = repo->DeleteExpiredTracesBatch(/*cutoff_ms*/3000, /*limit*/2);
    EXPECT_EQ(deleted, 2u);
    EXPECT_FALSE(QuerySummary("trace-oldest").has_value());
    EXPECT_FALSE(QuerySummary("trace-middle").has_value());
    EXPECT_TRUE(QuerySummary("trace-newest-expired").has_value());
}
