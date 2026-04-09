#include <chrono>
#include <filesystem>
#include <functional>
#include <gtest/gtest.h>
#include <optional>
#include <sqlite3.h>
#include <thread>

#include "ai/MockTraceAi.h"
#include "core/TraceSessionManager.h"
#include "persistence/BufferedTraceRepository.h"
#include "persistence/SqliteTraceRepository.h"
#include "threadpool/ThreadPool.h"

namespace
{
std::unique_ptr<BufferedTraceRepository> MakeBufferedTraceRepository(TraceRepository* repo)
{
    auto sink = std::shared_ptr<TraceRepository>(repo, [](TraceRepository*) {});
    return std::make_unique<BufferedTraceRepository>(std::move(sink));
}

class FixedTraceAi : public TraceAiProvider
{
public:
    TraceAiResponse response{
        .analysis =
            LogAnalysisResult{
                .summary = "fixed-trace-summary",
                .risk_level = RiskLevel::WARNING,
                .root_cause = "fixed-root-cause",
                .solution = "fixed-solution",
            },
        .usage = std::nullopt,
    };

    std::string last_payload;

    TraceAiResponse AnalyzeTrace(const std::string& trace_payload) override
    {
        last_payload = trace_payload;
        return response;
    }
};
}

class TraceSessionManagerIntegrationTest : public ::testing::Test
{
protected:
    void SetUp() override {
        db_path = "./test_trace_integration.db";
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove(db_path);
        }
    }

    void TearDown() override {
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

    std::optional<std::string> QueryChildParent(const std::string& span_id) {
        sqlite3* db = nullptr;
        int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK) {
            if (db) {
                sqlite3_close_v2(db);
            }
            return std::nullopt;
        }
        const char* sql = R"(
            SELECT parent_id FROM trace_span WHERE span_id = ?;
        )";
        sqlite3_stmt* stmt = nullptr;
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            sqlite3_close_v2(db);
            return std::nullopt;
        }
        sqlite3_bind_text(stmt, 1, span_id.c_str(), -1, SQLITE_TRANSIENT);
        std::optional<std::string> parent;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* value = sqlite3_column_text(stmt, 0);
            if (value) {
                parent = reinterpret_cast<const char*>(value);
            }
        }
        sqlite3_finalize(stmt);
        sqlite3_close_v2(db);
        return parent;
    }

    struct SummaryRow {
        std::string service_name;
        int64_t start_time_ms = 0;
        int64_t duration_ms = 0;
        std::string risk_level;
        int span_count = 0;
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
            SELECT service_name, start_time_ms, duration_ms, risk_level, span_count
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
            out.span_count = sqlite3_column_int(stmt, 4);
            row = out;
        }
        sqlite3_finalize(stmt);
        sqlite3_close_v2(db);
        return row;
    }

    struct SpanRow {
        std::string trace_id;
        std::string span_id;
        std::optional<std::string> parent_id;
        std::string service_name;
        std::string operation;
        int64_t start_time_ms = 0;
        int64_t duration_ms = 0;
        std::string status;
    };

    std::optional<SpanRow> QuerySpan(const std::string& span_id) {
        sqlite3* db = nullptr;
        int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK) {
            if (db) {
                sqlite3_close_v2(db);
            }
            return std::nullopt;
        }
        const char* sql = R"(
            SELECT trace_id, span_id, parent_id, service_name, operation, start_time_ms, duration_ms, status
            FROM trace_span
            WHERE span_id = ?;
        )";
        sqlite3_stmt* stmt = nullptr;
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            sqlite3_close_v2(db);
            return std::nullopt;
        }
        sqlite3_bind_text(stmt, 1, span_id.c_str(), -1, SQLITE_TRANSIENT);
        std::optional<SpanRow> row;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            SpanRow out;
            out.trace_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            out.span_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const unsigned char* parent = sqlite3_column_text(stmt, 2);
            if (parent) {
                out.parent_id = reinterpret_cast<const char*>(parent);
            }
            out.service_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            out.operation = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            out.start_time_ms = sqlite3_column_int64(stmt, 5);
            out.duration_ms = sqlite3_column_int64(stmt, 6);
            out.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            row = out;
        }
        sqlite3_finalize(stmt);
        sqlite3_close_v2(db);
        return row;
    }

    struct AnalysisRow {
        std::string trace_id;
        std::string risk_level;
        std::string summary;
        std::string root_cause;
        std::string solution;
    };

    std::optional<AnalysisRow> QueryAnalysis(const std::string& trace_id) {
        sqlite3* db = nullptr;
        int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK) {
            if (db) {
                sqlite3_close_v2(db);
            }
            return std::nullopt;
        }
        const char* sql = R"(
            SELECT trace_id, risk_level, summary, root_cause, solution
            FROM trace_analysis
            WHERE trace_id = ?;
        )";
        sqlite3_stmt* stmt = nullptr;
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            sqlite3_close_v2(db);
            return std::nullopt;
        }
        sqlite3_bind_text(stmt, 1, trace_id.c_str(), -1, SQLITE_TRANSIENT);
        std::optional<AnalysisRow> row;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            AnalysisRow out;
            out.trace_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            out.risk_level = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            out.summary = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            out.root_cause = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            out.solution = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            row = out;
        }
        sqlite3_finalize(stmt);
        sqlite3_close_v2(db);
        return row;
    }

    std::optional<std::string> QuerySpanAttributes(const std::string& span_id) {
        sqlite3* db = nullptr;
        int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK) {
            if (db) {
                sqlite3_close_v2(db);
            }
            return std::nullopt;
        }
        const char* sql = R"(
            SELECT attributes_json FROM trace_span WHERE span_id = ?;
        )";
        sqlite3_stmt* stmt = nullptr;
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            sqlite3_close_v2(db);
            return std::nullopt;
        }
        sqlite3_bind_text(stmt, 1, span_id.c_str(), -1, SQLITE_TRANSIENT);
        std::optional<std::string> out;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* value = sqlite3_column_text(stmt, 0);
            if (value) {
                out = reinterpret_cast<const char*>(value);
            }
        }
        sqlite3_finalize(stmt);
        sqlite3_close_v2(db);
        return out;
    }

    bool WaitForCount(const std::string& sql, int expected, std::chrono::milliseconds timeout) {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (QueryCount(sql) == expected) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }

    bool WaitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return predicate();
    }

    void SweepTicks(TraceSessionManager& manager,
                    int tick_count,
                    int64_t idle_timeout_ms = 5000,
                    int64_t wheel_tick_ms = 500,
                    size_t max_dispatch_per_tick = 0,
                    int64_t base_now_ms = 1000) {
        for (int tick = 0; tick < tick_count; ++tick) {
            manager.SweepExpiredSessions(base_now_ms + static_cast<int64_t>(tick) * wheel_tick_ms,
                                         idle_timeout_ms,
                                         max_dispatch_per_tick);
        }
    }

    void SweepTraceEndSealWindow(TraceSessionManager& manager) {
        // trace_end 默认进入 2 tick sealed grace window，integration test 不跑 main 里的定时器，
        // 所以这里需要手动推进两次 sweep，把会话从 sealed 推到真正 dispatch。
        SweepTicks(manager, /*tick_count*/2);
    }

    void SweepProtectionSealWindow(TraceSessionManager& manager) {
        // 现在 trace_end / capacity / token_limit / duplicate_span 都统一走同一个 sealed grace 配置。
        // integration test 既然不跑 main 里的定时 sweep，这里就也必须推进 2 tick，
        // 否则测试还停留在旧的“保护性封口只等 1 tick”心智上，会比真实实现更早断言。
        SweepTicks(manager, /*tick_count*/2);
    }

    SpanEvent MakeSpan(size_t trace_key, size_t span_id, std::optional<size_t> parent_id) {
        SpanEvent span;
        span.trace_key = trace_key;
        span.span_id = span_id;
        span.parent_span_id = parent_id;
        span.start_time_ms = 100;
        span.end_time = 150;
        span.name = "trace-span";
        span.service_name = "trace-service";
        return span;
    }

protected:
    std::string db_path;
};

TEST_F(TraceSessionManagerIntegrationTest, DispatchesOnTraceEndAndPersistsAnalysis)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    MockTraceAi ai;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), &ai, /*capacity*/10, /*token_limit*/0);

    SpanEvent span = MakeSpan(1, 100, std::nullopt);
    span.trace_end = true;

    // 依赖 AI Proxy 已启动，否则分析阶段会失败导致等待超时。
    EXPECT_EQ(manager.Push(span), TraceSessionManager::PushResult::Accepted);
    SweepTraceEndSealWindow(manager);

    // 等待异步任务完成并落库，避免线程池尚未消费导致测试误判。
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_analysis;", 1, std::chrono::seconds(3)));

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, BufferedMainPathPersistsPrimaryAndAnalysisRecords)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    FixedTraceAi ai;
    BufferedTraceRepository::Config buffer_config;
    buffer_config.primary_flush_interval_ms = 50;
    buffer_config.analysis_flush_interval_ms = 50;
    auto sink = std::shared_ptr<TraceRepository>(&repo, [](TraceRepository*) {});
    auto buffered_repo = std::make_unique<BufferedTraceRepository>(std::move(sink), buffer_config);
    TraceSessionManager manager(&pool, buffered_repo.get(), &ai, /*capacity*/8, /*token_limit*/0);

    SpanEvent root = MakeSpan(101, 1010, std::nullopt);
    root.name = "checkout-root";
    root.service_name = "checkout-service";
    root.start_time_ms = 1000;
    root.end_time = 1400;
    root.status = SpanEvent::Status::Ok;
    root.attributes["http.method"] = "POST";

    SpanEvent child = MakeSpan(101, 1011, 1010);
    child.name = "query-inventory";
    child.service_name = "checkout-service";
    child.start_time_ms = 1100;
    child.end_time = 1250;
    child.status = SpanEvent::Status::Error;
    child.attributes["db.system"] = "sqlite";
    child.trace_end = true;

    EXPECT_EQ(manager.Push(root), TraceSessionManager::PushResult::Accepted);
    EXPECT_EQ(manager.Push(child), TraceSessionManager::PushResult::Accepted);
    SweepTraceEndSealWindow(manager);

    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 2, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_analysis;", 1, std::chrono::seconds(3)));

    EXPECT_FALSE(ai.last_payload.empty());

    auto summary = QuerySummary("101");
    ASSERT_TRUE(summary.has_value());
    EXPECT_EQ(summary->service_name, "checkout-service");
    EXPECT_EQ(summary->start_time_ms, 1000);
    EXPECT_EQ(summary->duration_ms, 400);
    EXPECT_EQ(summary->span_count, 2);
    // analysis flush 成功后，SQLite 会把最终 risk_level 回写到 trace_summary，列表页直接读主表即可拿到最新等级。
    EXPECT_EQ(summary->risk_level, "warning");

    auto root_row = QuerySpan("1010");
    ASSERT_TRUE(root_row.has_value());
    EXPECT_EQ(root_row->trace_id, "101");
    EXPECT_FALSE(root_row->parent_id.has_value());
    EXPECT_EQ(root_row->operation, "checkout-root");
    EXPECT_EQ(root_row->status, "OK");
    EXPECT_EQ(root_row->duration_ms, 400);

    auto child_row = QuerySpan("1011");
    ASSERT_TRUE(child_row.has_value());
    EXPECT_EQ(child_row->trace_id, "101");
    ASSERT_TRUE(child_row->parent_id.has_value());
    EXPECT_EQ(child_row->parent_id.value(), "1010");
    EXPECT_EQ(child_row->operation, "query-inventory");
    EXPECT_EQ(child_row->status, "ERROR");
    EXPECT_EQ(child_row->duration_ms, 150);

    auto analysis = QueryAnalysis("101");
    ASSERT_TRUE(analysis.has_value());
    EXPECT_EQ(analysis->trace_id, "101");
    EXPECT_EQ(analysis->risk_level, "warning");
    EXPECT_EQ(analysis->summary, "fixed-trace-summary");
    EXPECT_EQ(analysis->root_cause, "fixed-root-cause");
    EXPECT_EQ(analysis->solution, "fixed-solution");

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, BufferedRepositoryDestructorDrainsRemainingPrimaryAndAnalysis)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    FixedTraceAi ai;

    {
        BufferedTraceRepository::Config buffer_config;
        buffer_config.primary_summary_reserve = 64;
        buffer_config.primary_span_reserve = 64;
        // analysis 缓冲区现在只服务 trace_analysis 这一张表，
        // 所以这里保留 analysis_reserve 就够了，不再给废弃调试支线预留容量。
        buffer_config.analysis_reserve = 64;
        // 故意把时间阈值拉长，确保测试关注的是“析构 drain”，不是后台定时 flush。
        buffer_config.primary_flush_interval_ms = 60 * 1000;
        buffer_config.analysis_flush_interval_ms = 60 * 1000;
        auto sink = std::shared_ptr<TraceRepository>(&repo, [](TraceRepository*) {});
        auto buffered_repo = std::make_unique<BufferedTraceRepository>(std::move(sink), buffer_config);
        TraceSessionManager manager(&pool, buffered_repo.get(), &ai, /*capacity*/8, /*token_limit*/0);

        SpanEvent root = MakeSpan(202, 2020, std::nullopt);
        root.name = "drain-root";
        root.service_name = "drain-service";
        root.start_time_ms = 2000;
        root.end_time = 2600;

        SpanEvent child = MakeSpan(202, 2021, 2020);
        child.name = "drain-child";
        child.service_name = "drain-service";
        child.start_time_ms = 2100;
        child.end_time = 2400;
        child.trace_end = true;

        EXPECT_EQ(manager.Push(root), TraceSessionManager::PushResult::Accepted);
        EXPECT_EQ(manager.Push(child), TraceSessionManager::PushResult::Accepted);
        SweepTraceEndSealWindow(manager);

        ASSERT_TRUE(WaitUntil([&manager]() {
            return manager.SnapshotRuntimeStats().worker_done_count >= 1;
        }, std::chrono::seconds(2)));

        // 这里必须还是 0，才能证明后面查到的数据来自 StopFlushThread 的 drain，而不是时间到自动刷盘。
        EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_summary;"), 0);
        EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_span;"), 0);
        EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_analysis;"), 0);
    }

    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(2)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 2, std::chrono::seconds(2)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_analysis;", 1, std::chrono::seconds(2)));

    auto analysis = QueryAnalysis("202");
    ASSERT_TRUE(analysis.has_value());
    EXPECT_EQ(analysis->risk_level, "warning");
    EXPECT_EQ(analysis->summary, "fixed-trace-summary");

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, DispatchesOnCapacityAndStoresParentId)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    MockTraceAi ai;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), &ai, /*capacity*/2, /*token_limit*/0);

    SpanEvent root = MakeSpan(2, 200, std::nullopt);
    SpanEvent child = MakeSpan(2, 201, 200);

    // 这里不设置 trace_end，验证容量触发的分发路径是否可用。
    EXPECT_EQ(manager.Push(root), TraceSessionManager::PushResult::Accepted);
    EXPECT_EQ(manager.Push(child), TraceSessionManager::PushResult::Accepted);
    SweepProtectionSealWindow(manager);

    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 2, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_analysis;", 1, std::chrono::seconds(3)));

    auto parent_id = QueryChildParent("201");
    ASSERT_TRUE(parent_id.has_value());
    EXPECT_EQ(parent_id.value(), "200");

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, DispatchesOnTokenLimitWithoutTraceEnd)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    // 这个用例只验证 token 阈值触发分发，不接 AI，避免外部依赖导致干扰。
    TokenEstimator estimator;

    SpanEvent span1 = MakeSpan(13, 1300, std::nullopt);
    span1.name = "token-root";
    span1.service_name = "svc";
    span1.attributes["env"] = "prod";

    SpanEvent span2 = MakeSpan(13, 1301, 1300);
    span2.name = "token-child";
    span2.service_name = "svc";
    span2.attributes["error_code"] = "DB_CONN_TIMEOUT";

    const size_t token_limit = estimator.Estimate(span1) + estimator.Estimate(span2);
    ASSERT_GT(token_limit, 0u);

    // capacity 设大，确保不会被容量路径抢先触发。
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool,
                                buffered_repo.get(),
                                /*trace_ai*/nullptr,
                                /*capacity*/100,
                                token_limit);

    EXPECT_EQ(manager.Push(span1), TraceSessionManager::PushResult::Accepted);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_summary;"), 0);

    EXPECT_EQ(manager.Push(span2), TraceSessionManager::PushResult::Accepted);
    SweepProtectionSealWindow(manager);
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 2, std::chrono::seconds(3)));
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_analysis;"), 0);

    auto summary = QuerySummary("13");
    ASSERT_TRUE(summary.has_value());
    EXPECT_GE(static_cast<size_t>(summary->span_count), 2u);

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, DispatchesOnDuplicateSpanId)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    MockTraceAi ai;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), &ai, /*capacity*/10, /*token_limit*/0);

    SpanEvent root = MakeSpan(3, 300, std::nullopt);
    EXPECT_EQ(manager.Push(root), TraceSessionManager::PushResult::Accepted);

    // 重复 span_id 会触发提前分发。
    SpanEvent dup = MakeSpan(3, 300, std::nullopt);
    EXPECT_EQ(manager.Push(dup), TraceSessionManager::PushResult::Accepted);

    // duplicate_span 当前走“1 tick 封口后异步 dispatch”语义，所以这里也要手动推进一次 protection seal window。
    SweepProtectionSealWindow(manager);
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_analysis;", 1, std::chrono::seconds(3)));

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, PersistsSummarySpanAndAnalysisFields)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    MockTraceAi ai;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), &ai, /*capacity*/10, /*token_limit*/0);

    SpanEvent root = MakeSpan(4, 400, std::nullopt);
    root.name = "root-op";
    root.service_name = "order-service";
    root.start_time_ms = 1000;
    root.end_time = 1300;
    root.status = SpanEvent::Status::Ok;

    SpanEvent child = MakeSpan(4, 401, 400);
    child.name = "child-op";
    child.service_name = "order-service";
    child.start_time_ms = 1100;
    child.end_time = 1200;
    child.status = SpanEvent::Status::Error;

    EXPECT_EQ(manager.Push(root), TraceSessionManager::PushResult::Accepted);
    child.trace_end = true;
    EXPECT_EQ(manager.Push(child), TraceSessionManager::PushResult::Accepted);
    SweepTraceEndSealWindow(manager);

    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 2, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_analysis;", 1, std::chrono::seconds(3)));

    auto summary = QuerySummary("4");
    ASSERT_TRUE(summary.has_value());
    EXPECT_EQ(summary->service_name, "order-service");
    EXPECT_EQ(summary->start_time_ms, 1000);
    EXPECT_EQ(summary->duration_ms, 300);
    EXPECT_EQ(summary->risk_level, "unknown");

    auto root_row = QuerySpan("400");
    ASSERT_TRUE(root_row.has_value());
    EXPECT_EQ(root_row->trace_id, "4");
    EXPECT_FALSE(root_row->parent_id.has_value());
    EXPECT_EQ(root_row->service_name, "order-service");
    EXPECT_EQ(root_row->operation, "root-op");
    EXPECT_EQ(root_row->start_time_ms, 1000);
    EXPECT_EQ(root_row->duration_ms, 300);
    EXPECT_EQ(root_row->status, "OK");

    auto child_row = QuerySpan("401");
    ASSERT_TRUE(child_row.has_value());
    EXPECT_EQ(child_row->trace_id, "4");
    ASSERT_TRUE(child_row->parent_id.has_value());
    EXPECT_EQ(child_row->parent_id.value(), "400");
    EXPECT_EQ(child_row->service_name, "order-service");
    EXPECT_EQ(child_row->operation, "child-op");
    EXPECT_EQ(child_row->start_time_ms, 1100);
    EXPECT_EQ(child_row->duration_ms, 100);
    EXPECT_EQ(child_row->status, "ERROR");

    auto analysis = QueryAnalysis("4");
    ASSERT_TRUE(analysis.has_value());
    EXPECT_EQ(analysis->trace_id, "4");
    // 这里不再把 risk_level 固定为 unknown：
    // 当前风险等级由 MockTraceAi/Proxy 基于 payload 动态给出，固定断言会导致环境差异下不稳定。
    EXPECT_FALSE(analysis->risk_level.empty());
    EXPECT_FALSE(analysis->summary.empty());
    EXPECT_FALSE(analysis->root_cause.empty());
    EXPECT_FALSE(analysis->solution.empty());

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, MarksCycleAsAnomaly)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent span_a = MakeSpan(5, 500, 501);
    span_a.attributes["seed"] = "cycle";
    SpanEvent span_b = MakeSpan(5, 501, 500);
    span_b.attributes["seed"] = "cycle";
    span_b.trace_end = true;

    EXPECT_EQ(manager.Push(span_a), TraceSessionManager::PushResult::Accepted);
    EXPECT_EQ(manager.Push(span_b), TraceSessionManager::PushResult::Accepted);
    SweepTraceEndSealWindow(manager);

    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(3)));
    // 当前实现在“全环且无 root”时，序列化遍历顺序为空，因此不会写入 trace_span。
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 0, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_analysis;", 0, std::chrono::seconds(3)));

    auto summary = QuerySummary("5");
    ASSERT_TRUE(summary.has_value());
    // 即使 trace_span 当前未落库，summary 仍应记录会话层统计，避免整条 trace 完全丢失。
    EXPECT_EQ(summary->span_count, 2);

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, TreatsMissingParentAsRoot)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    MockTraceAi ai;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), &ai, /*capacity*/10, /*token_limit*/0);

    SpanEvent orphan = MakeSpan(6, 600, 999);
    orphan.trace_end = true;

    EXPECT_EQ(manager.Push(orphan), TraceSessionManager::PushResult::Accepted);
    SweepTraceEndSealWindow(manager);

    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_analysis;", 1, std::chrono::seconds(3)));

    auto row = QuerySpan("600");
    ASSERT_TRUE(row.has_value());
    // 当前实现策略：缺失父节点会按 root 参与遍历，但落库仍保留原始 parent_id 用于排障。
    ASSERT_TRUE(row->parent_id.has_value());
    EXPECT_EQ(row->parent_id.value(), "999");

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, HandlesOutOfOrderSpans)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    MockTraceAi ai;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), &ai, /*capacity*/10, /*token_limit*/0);

    SpanEvent child = MakeSpan(7, 701, 700);
    SpanEvent root = MakeSpan(7, 700, std::nullopt);
    root.trace_end = true;

    EXPECT_EQ(manager.Push(child), TraceSessionManager::PushResult::Accepted);
    EXPECT_EQ(manager.Push(root), TraceSessionManager::PushResult::Accepted);
    SweepTraceEndSealWindow(manager);

    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 2, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_analysis;", 1, std::chrono::seconds(3)));

    auto row = QuerySpan("701");
    ASSERT_TRUE(row.has_value());
    ASSERT_TRUE(row->parent_id.has_value());
    EXPECT_EQ(row->parent_id.value(), "700");

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, DispatchesOnIdleTimeoutWithoutTraceEnd)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    // 这个用例只验证“超时触发基础落库”，不依赖 AI 返回结果，避免外部依赖导致抖动。
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool,
                                buffered_repo.get(),
                                /*trace_ai*/nullptr,
                                /*capacity*/10,
                                /*token_limit*/0,
                                /*notifier*/nullptr,
                                /*idle_timeout_ms*/500,
                                /*wheel_tick_ms*/100,
                                /*wheel_size*/256);

    auto now_steady_ms = []() -> int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };

    SpanEvent root = MakeSpan(8, 800, std::nullopt);
    SpanEvent child = MakeSpan(8, 801, 800);

    // 不设置 trace_end，强制走“空闲超时分发”路径。
    EXPECT_EQ(manager.Push(root), TraceSessionManager::PushResult::Accepted);
    EXPECT_EQ(manager.Push(child), TraceSessionManager::PushResult::Accepted);

    // 初始时刻不应提前落库。
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_summary;"), 0);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_span;"), 0);

    // 测试中手动模拟 runEvery 周期调用，保证时序可控，不依赖 event loop 调度抖动。
    bool dispatched = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        manager.SweepExpiredSessions(now_steady_ms(),
                                     /*idle_timeout_ms*/500,
                                     /*max_dispatch_per_tick*/0);
        if (QueryCount("SELECT COUNT(*) FROM trace_summary;") == 1) {
            dispatched = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_TRUE(dispatched) << "3 秒内未触发 idle timeout 分发";
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 2, std::chrono::seconds(2)));
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_analysis;"), 0);

    // 已经分发完成后再次推进 sweep，不应重复写入。
    manager.SweepExpiredSessions(now_steady_ms() + 1000,
                                 /*idle_timeout_ms*/500,
                                 /*max_dispatch_per_tick*/0);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_summary;"), 1);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_span;"), 2);

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, FrequentUpdatesPreventEarlyTimeoutDispatch)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool,
                                buffered_repo.get(),
                                /*trace_ai*/nullptr,
                                /*capacity*/32,
                                /*token_limit*/0,
                                /*notifier*/nullptr,
                                /*idle_timeout_ms*/500,
                                /*wheel_tick_ms*/100,
                                /*wheel_size*/256);

    auto now_steady_ms = []() -> int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };

    // 每 200ms 持续追加同一 trace 的新 span，模拟“活跃会话持续续命”。
    // 由于小于 idle_timeout(500ms)，理论上不应被提前超时分发。
    constexpr size_t trace_key = 9;
    constexpr int total_spans = 8;
    for (int i = 0; i < total_spans; ++i) {
        SpanEvent span = MakeSpan(trace_key, static_cast<size_t>(900 + i), std::nullopt);
        EXPECT_EQ(manager.Push(span), TraceSessionManager::PushResult::Accepted);

        manager.SweepExpiredSessions(now_steady_ms(),
                                     /*idle_timeout_ms*/500,
                                     /*max_dispatch_per_tick*/0);
        EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_summary;"), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 停止流量后再等待超时，应该最终分发并落库。
    bool dispatched = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        manager.SweepExpiredSessions(now_steady_ms(),
                                     /*idle_timeout_ms*/500,
                                     /*max_dispatch_per_tick*/0);
        if (QueryCount("SELECT COUNT(*) FROM trace_summary;") == 1) {
            dispatched = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_TRUE(dispatched) << "停止续命后 3 秒内仍未触发超时分发";
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", total_spans, std::chrono::seconds(2)));
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_analysis;"), 0);

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, MaxDispatchPerTickDefersRemainingTimedOutSessions)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool,
                                buffered_repo.get(),
                                /*trace_ai*/nullptr,
                                /*capacity*/10,
                                /*token_limit*/0,
                                /*notifier*/nullptr,
                                /*idle_timeout_ms*/400,
                                /*wheel_tick_ms*/100,
                                /*wheel_size*/256);

    auto now_steady_ms = []() -> int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };

    // 构造 3 条 trace，让它们在同一轮达到超时条件。
    EXPECT_EQ(manager.Push(MakeSpan(10, 1000, std::nullopt)), TraceSessionManager::PushResult::Accepted);
    EXPECT_EQ(manager.Push(MakeSpan(11, 1100, std::nullopt)), TraceSessionManager::PushResult::Accepted);
    EXPECT_EQ(manager.Push(MakeSpan(12, 1200, std::nullopt)), TraceSessionManager::PushResult::Accepted);

    const int64_t base = now_steady_ms();

    // 先推进到接近超时，不应提前落库。
    manager.SweepExpiredSessions(base + 100, /*idle_timeout_ms*/400, /*max_dispatch_per_tick*/1);
    manager.SweepExpiredSessions(base + 200, /*idle_timeout_ms*/400, /*max_dispatch_per_tick*/1);
    manager.SweepExpiredSessions(base + 300, /*idle_timeout_ms*/400, /*max_dispatch_per_tick*/1);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_summary;"), 0);

    // 第 4 轮到期：因为上限=1，本轮只能分发 1 条。
    manager.SweepExpiredSessions(base + 400, /*idle_timeout_ms*/400, /*max_dispatch_per_tick*/1);
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(2)));

    // 后续每轮再放行 1 条，验证顺延策略生效。
    manager.SweepExpiredSessions(base + 500, /*idle_timeout_ms*/400, /*max_dispatch_per_tick*/1);
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 2, std::chrono::seconds(2)));

    manager.SweepExpiredSessions(base + 600, /*idle_timeout_ms*/400, /*max_dispatch_per_tick*/1);
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 3, std::chrono::seconds(2)));

    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 3, std::chrono::seconds(2)));
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_analysis;"), 0);

    pool.shutdown();
}
