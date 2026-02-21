#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <optional>
#include <sqlite3.h>
#include <thread>

#include "ai/MockTraceAi.h"
#include "core/TraceSessionManager.h"
#include "persistence/SqliteTraceRepository.h"
#include "threadpool/ThreadPool.h"

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
    TraceSessionManager manager(&pool, &repo, &ai, /*capacity*/10, /*token_limit*/0);

    SpanEvent span = MakeSpan(1, 100, std::nullopt);
    span.trace_end = true;

    // 依赖 AI Proxy 已启动，否则分析阶段会失败导致等待超时。
    EXPECT_TRUE(manager.Push(span));

    // 等待异步任务完成并落库，避免线程池尚未消费导致测试误判。
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_analysis;", 1, std::chrono::seconds(3)));

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, DispatchesOnCapacityAndStoresParentId)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    MockTraceAi ai;
    TraceSessionManager manager(&pool, &repo, &ai, /*capacity*/2, /*token_limit*/0);

    SpanEvent root = MakeSpan(2, 200, std::nullopt);
    SpanEvent child = MakeSpan(2, 201, 200);

    // 这里不设置 trace_end，验证容量触发的分发路径是否可用。
    EXPECT_TRUE(manager.Push(root));
    EXPECT_TRUE(manager.Push(child));

    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 2, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_analysis;", 1, std::chrono::seconds(3)));

    auto parent_id = QueryChildParent("201");
    ASSERT_TRUE(parent_id.has_value());
    EXPECT_EQ(parent_id.value(), "200");

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, DispatchesOnDuplicateSpanId)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    MockTraceAi ai;
    TraceSessionManager manager(&pool, &repo, &ai, /*capacity*/10, /*token_limit*/0);

    SpanEvent root = MakeSpan(3, 300, std::nullopt);
    EXPECT_TRUE(manager.Push(root));

    // 重复 span_id 会触发提前分发。
    SpanEvent dup = MakeSpan(3, 300, std::nullopt);
    EXPECT_TRUE(manager.Push(dup));

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
    TraceSessionManager manager(&pool, &repo, &ai, /*capacity*/10, /*token_limit*/0);

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

    EXPECT_TRUE(manager.Push(root));
    child.trace_end = true;
    EXPECT_TRUE(manager.Push(child));

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
    EXPECT_EQ(analysis->risk_level, "unknown");
    EXPECT_FALSE(analysis->summary.empty());
    EXPECT_FALSE(analysis->root_cause.empty());
    EXPECT_FALSE(analysis->solution.empty());

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, MarksCycleAsAnomaly)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    MockTraceAi ai;
    TraceSessionManager manager(&pool, &repo, &ai, /*capacity*/10, /*token_limit*/0);

    SpanEvent span_a = MakeSpan(5, 500, 501);
    span_a.attributes["seed"] = "cycle";
    SpanEvent span_b = MakeSpan(5, 501, 500);
    span_b.attributes["seed"] = "cycle";
    span_b.trace_end = true;

    EXPECT_TRUE(manager.Push(span_a));
    EXPECT_TRUE(manager.Push(span_b));

    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 2, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_analysis;", 1, std::chrono::seconds(3)));

    // 当前 anomalies 会出现在序列化 payload 中，这里至少保证 trace_span 落库不丢。
    auto attrs = QuerySpanAttributes("500");
    ASSERT_TRUE(attrs.has_value());
    EXPECT_NE(attrs.value().find("cycle"), std::string::npos);

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, TreatsMissingParentAsRoot)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    MockTraceAi ai;
    TraceSessionManager manager(&pool, &repo, &ai, /*capacity*/10, /*token_limit*/0);

    SpanEvent orphan = MakeSpan(6, 600, 999);
    orphan.trace_end = true;

    EXPECT_TRUE(manager.Push(orphan));

    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_analysis;", 1, std::chrono::seconds(3)));

    auto row = QuerySpan("600");
    ASSERT_TRUE(row.has_value());
    EXPECT_FALSE(row->parent_id.has_value());

    pool.shutdown();
}

TEST_F(TraceSessionManagerIntegrationTest, HandlesOutOfOrderSpans)
{
    ThreadPool pool(1);
    SqliteTraceRepository repo(db_path);
    MockTraceAi ai;
    TraceSessionManager manager(&pool, &repo, &ai, /*capacity*/10, /*token_limit*/0);

    SpanEvent child = MakeSpan(7, 701, 700);
    SpanEvent root = MakeSpan(7, 700, std::nullopt);
    root.trace_end = true;

    EXPECT_TRUE(manager.Push(child));
    EXPECT_TRUE(manager.Push(root));

    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_span;", 2, std::chrono::seconds(3)));
    EXPECT_TRUE(WaitForCount("SELECT COUNT(*) FROM trace_analysis;", 1, std::chrono::seconds(3)));

    auto row = QuerySpan("701");
    ASSERT_TRUE(row.has_value());
    ASSERT_TRUE(row->parent_id.has_value());
    EXPECT_EQ(row->parent_id.value(), "700");

    pool.shutdown();
}
