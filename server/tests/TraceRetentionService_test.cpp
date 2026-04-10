#include <filesystem>
#include <gtest/gtest.h>
#include <optional>
#include <sqlite3.h>
#include <thread>

#include "core/TraceRetentionService.h"
#include "persistence/SqliteTraceRepository.h"
#include "threadpool/ThreadPool.h"

class TraceRetentionServiceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        db_path_ = "./test_trace_retention.db";
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
    }

    void TearDown() override
    {
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
    }

    int QueryCount(const std::string& sql)
    {
        sqlite3* db = nullptr;
        int rc = sqlite3_open_v2(db_path_.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
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

    bool WaitUntil(const std::function<bool()>& predicate, int timeout_ms = 1000)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return predicate();
    }

    persistence::TraceSummary MakeSummary(const std::string& trace_id,
                                          int64_t start_time_ms,
                                          std::optional<int64_t> end_time_ms)
    {
        persistence::TraceSummary summary;
        summary.trace_id = trace_id;
        summary.service_name = "retention-service";
        summary.start_time_ms = start_time_ms;
        summary.end_time_ms = end_time_ms;
        summary.duration_ms = 100;
        summary.span_count = 1;
        summary.token_count = 10;
        summary.risk_level = "unknown";
        return summary;
    }

    persistence::TraceSpanRecord MakeSpan(const std::string& trace_id, const std::string& span_id)
    {
        persistence::TraceSpanRecord span;
        span.trace_id = trace_id;
        span.span_id = span_id;
        span.service_name = "retention-service";
        span.operation = "op";
        span.start_time_ms = 100;
        span.duration_ms = 50;
        span.status = "OK";
        span.attributes_json = "{}";
        return span;
    }

    persistence::TraceAnalysisRecord MakeAnalysis(const std::string& trace_id)
    {
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
    std::string db_path_;
};

TEST_F(TraceRetentionServiceTest, StartupCleanupDeletesExpiredTracesWithinBatchBudget)
{
    SqliteTraceRepository repo(db_path_);
    ThreadPool query_tpool(1);
    const int64_t now_ms = 10LL * 24 * 60 * 60 * 1000;

    auto old_a = MakeSummary("old-a", 1000, 2000);
    auto old_b = MakeSummary("old-b", 3000, 4000);
    auto old_c = MakeSummary("old-c", 5000, 6000);
    auto fresh = MakeSummary("fresh", now_ms - 2LL * 24 * 60 * 60 * 1000, now_ms - 2LL * 24 * 60 * 60 * 1000 + 100);
    auto old_a_analysis = MakeAnalysis(old_a.trace_id);
    auto old_b_analysis = MakeAnalysis(old_b.trace_id);
    auto old_c_analysis = MakeAnalysis(old_c.trace_id);
    auto fresh_analysis = MakeAnalysis(fresh.trace_id);

    ASSERT_TRUE(repo.SaveSingleTraceAtomic(old_a, {MakeSpan(old_a.trace_id, "span-old-a")}, &old_a_analysis));
    ASSERT_TRUE(repo.SaveSingleTraceAtomic(old_b, {MakeSpan(old_b.trace_id, "span-old-b")}, &old_b_analysis));
    ASSERT_TRUE(repo.SaveSingleTraceAtomic(old_c, {MakeSpan(old_c.trace_id, "span-old-c")}, &old_c_analysis));
    ASSERT_TRUE(repo.SaveSingleTraceAtomic(fresh, {MakeSpan(fresh.trace_id, "span-fresh")}, &fresh_analysis));

    TraceRetentionService::Config config;
    config.retention_days = 7;
    config.batch_size = 1;
    config.max_batches_per_run = 2;
    config.cleanup_interval_ms = 60 * 60 * 1000;
    auto service = std::make_shared<TraceRetentionService>(
        &repo, &query_tpool, config, [now_ms]() { return now_ms; });

    // 这里锁定“启动清理也要受批次数预算控制”，避免第一次启动就无限循环把数据库一直删下去。
    service->TriggerStartupCleanup();

    ASSERT_TRUE(WaitUntil([this]() {
        return QueryCount("SELECT COUNT(*) FROM trace_summary;") == 2;
    }));
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_span;"), 2);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_analysis;"), 2);

    query_tpool.shutdown();
}

TEST_F(TraceRetentionServiceTest, DisabledRetentionDoesNotScheduleCleanup)
{
    SqliteTraceRepository repo(db_path_);
    ThreadPool query_tpool(1);
    const int64_t now_ms = 10LL * 24 * 60 * 60 * 1000;

    auto expired = MakeSummary("expired", 1000, 2000);
    auto expired_analysis = MakeAnalysis(expired.trace_id);
    ASSERT_TRUE(repo.SaveSingleTraceAtomic(expired,
                                           {MakeSpan(expired.trace_id, "span-expired")},
                                           &expired_analysis));

    TraceRetentionService::Config config;
    config.retention_days = 0;
    config.batch_size = 10;
    config.max_batches_per_run = 3;
    auto service = std::make_shared<TraceRetentionService>(
        &repo, &query_tpool, config, [now_ms]() { return now_ms; });

    service->TriggerStartupCleanup();
    service->TrySchedulePeriodicCleanup(now_ms + 10 * 60 * 1000);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_summary;"), 1);
    EXPECT_EQ(query_tpool.pendingTasks(), 0u);

    query_tpool.shutdown();
}
