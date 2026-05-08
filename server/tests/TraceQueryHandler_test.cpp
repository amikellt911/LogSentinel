#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "handlers/TraceQueryHandler.h"
#include "persistence/SqliteTraceRepository.h"

namespace
{
class TraceQueryHandlerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        db_path_ = "./test_trace_query_handler.db";
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
        repo_ = std::make_shared<SqliteTraceRepository>(db_path_);
    }

    void TearDown() override
    {
        repo_.reset();
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

    persistence::TraceSummary MakeSummary(const std::string& trace_id)
    {
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
                                          const std::string& parent_id)
    {
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

    std::shared_ptr<SqliteTraceRepository> repo_;
    std::string db_path_;
};
} // namespace

TEST_F(TraceQueryHandlerTest, HandleDeleteTraceReturns400WhenTraceIdMissing)
{
    TraceQueryHandler handler(repo_, nullptr);
    HttpRequest req;
    req.method_ = "DELETE";
    req.path_ = "/traces/";
    HttpResponse resp;

    // 这里锁的是路径参数提取语义：没有 trace_id 时，删除接口必须先挡在 handler 层。
    handler.handleDeleteTrace(req, &resp, nullptr);

    EXPECT_EQ(resp.statusCode_, HttpResponse::HttpStatusCode::k400BadRequest);
    const nlohmann::json body = nlohmann::json::parse(resp.body_);
    EXPECT_EQ(body.at("error"), "Missing trace_id in path");
}

TEST_F(TraceQueryHandlerTest, HandleDeleteTraceReturns200AndRemovesRows)
{
    TraceQueryHandler handler(repo_, nullptr);
    const std::string trace_id = "delete-trace-http";
    persistence::TraceSummary summary = MakeSummary(trace_id);
    std::vector<persistence::TraceSpanRecord> spans;
    spans.push_back(MakeSpan(trace_id, "span-1", ""));
    spans.push_back(MakeSpan(trace_id, "span-2", "span-1"));
    persistence::TraceAnalysisRecord analysis = MakeAnalysis(trace_id);

    ASSERT_TRUE(repo_->SaveSingleTraceAtomic(summary, spans, &analysis));
    ASSERT_EQ(QueryCount("SELECT COUNT(*) FROM trace_summary;"), 1);
    ASSERT_EQ(QueryCount("SELECT COUNT(*) FROM trace_span;"), 2);
    ASSERT_EQ(QueryCount("SELECT COUNT(*) FROM trace_analysis;"), 1);

    HttpRequest req;
    req.method_ = "DELETE";
    req.path_ = "/traces/" + trace_id;
    HttpResponse resp;

    // 删除接口按“单条 trace 一次性收口”返回，前端拿到 200 后就可以直接刷新列表并关抽屉。
    handler.handleDeleteTrace(req, &resp, nullptr);

    EXPECT_EQ(resp.statusCode_, HttpResponse::HttpStatusCode::k200Ok);
    const nlohmann::json body = nlohmann::json::parse(resp.body_);
    EXPECT_EQ(body.at("deleted"), true);
    EXPECT_EQ(body.at("trace_id"), trace_id);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_summary;"), 0);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_span;"), 0);
    EXPECT_EQ(QueryCount("SELECT COUNT(*) FROM trace_analysis;"), 0);
}

TEST_F(TraceQueryHandlerTest, HandleDeleteTraceReturns200WhenTraceAlreadyMissing)
{
    TraceQueryHandler handler(repo_, nullptr);
    HttpRequest req;
    req.method_ = "DELETE";
    req.path_ = "/traces/missing-trace";
    HttpResponse resp;

    // 这里故意不预置任何记录，锁死“重复删除/已删除”仍然按成功收口的接口语义。
    handler.handleDeleteTrace(req, &resp, nullptr);

    EXPECT_EQ(resp.statusCode_, HttpResponse::HttpStatusCode::k200Ok);
    const nlohmann::json body = nlohmann::json::parse(resp.body_);
    EXPECT_EQ(body.at("deleted"), true);
    EXPECT_EQ(body.at("trace_id"), "missing-trace");
}
