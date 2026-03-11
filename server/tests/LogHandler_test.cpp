#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "core/TraceSessionManager.h"
#include "handlers/LogHandler.h"
#include "persistence/BufferedTraceRepository.h"
#include "persistence/TraceRepository.h"
#include "threadpool/ThreadPool.h"

namespace
{
std::unique_ptr<BufferedTraceRepository> MakeBufferedTraceRepository(TraceRepository* repo)
{
    auto sink = std::shared_ptr<TraceRepository>(repo, [](TraceRepository*) {});
    return std::make_unique<BufferedTraceRepository>(std::move(sink));
}

class FakeTraceRepository : public TraceRepository
{
public:
    bool SaveSingleTraceSummary(const TraceSummary&) override
    {
        return true;
    }

    bool SaveSingleTraceSpans(const std::string&, const std::vector<TraceSpanRecord>&) override
    {
        return true;
    }

    bool SaveSingleTraceAnalysis(const TraceAnalysisRecord&) override
    {
        return true;
    }

    bool SaveSinglePromptDebug(const PromptDebugRecord&) override
    {
        return true;
    }

    bool SaveSingleTraceAtomic(const TraceSummary&,
                               const std::vector<TraceSpanRecord>&,
                               const TraceAnalysisRecord*,
                               const PromptDebugRecord*) override
    {
        return true;
    }
};
} // namespace

class LogHandlerTracePostTest : public ::testing::Test
{
protected:
    HttpRequest MakeTraceRequest(size_t trace_key, size_t span_id, bool trace_end = false)
    {
        HttpRequest req;
        req.method_ = "POST";
        req.path_ = "/logs/spans";
        req.version_ = "HTTP/1.1";
        req.body_ = nlohmann::json{
            {"trace_key", trace_key},
            {"span_id", span_id},
            {"start_time_ms", 1000},
            {"name", "unit-test-span"},
            {"service_name", "unit-test-service"},
            {"trace_end", trace_end},
        }
                        .dump();
        return req;
    }

    SpanEvent MakeSpan(size_t trace_key, size_t span_id)
    {
        SpanEvent span;
        span.trace_key = trace_key;
        span.span_id = span_id;
        span.start_time_ms = 1000;
        span.name = "collecting-span";
        span.service_name = "svc";
        return span;
    }

    nlohmann::json ParseBody(const HttpResponse& resp)
    {
        return nlohmann::json::parse(resp.body_);
    }
};

TEST_F(LogHandlerTracePostTest, HandleTracePostReturns503WhenTraceManagerMissing)
{
    LogHandler handler(nullptr, nullptr, nullptr, nullptr);
    HttpRequest req = MakeTraceRequest(1, 101, true);
    HttpResponse resp;

    handler.handleTracePost(req, &resp, nullptr);

    ASSERT_EQ(resp.statusCode_, HttpResponse::HttpStatusCode::k503ServiceUnavailable);
    ASSERT_EQ(resp.headers_.at("Content-Type"), "application/json");
    const nlohmann::json body = ParseBody(resp);
    EXPECT_EQ(body.at("error"), "Trace pipeline is unavailable");
}

TEST_F(LogHandlerTracePostTest, HandleTracePostReturns503WhenManagerRejectsUnavailable)
{
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(nullptr, buffered_repo.get(), nullptr, /*capacity*/ 8, /*token_limit*/ 0);
    LogHandler handler(nullptr, nullptr, nullptr, &manager);
    HttpRequest req = MakeTraceRequest(2, 201, true);
    HttpResponse resp;

    handler.handleTracePost(req, &resp, nullptr);

    ASSERT_EQ(resp.statusCode_, HttpResponse::HttpStatusCode::k503ServiceUnavailable);
    ASSERT_EQ(resp.headers_.at("Content-Type"), "application/json");
    const nlohmann::json body = ParseBody(resp);
    EXPECT_EQ(body.at("accepted"), false);
    EXPECT_EQ(body.at("error"), "Trace pipeline is unavailable");
}

TEST_F(LogHandlerTracePostTest, HandleTracePostReturns503AndRetryAfterWhenManagerRejectsOverload)
{
    ThreadPool pool(1, 16);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool,
                                buffered_repo.get(),
                                nullptr,
                                /*capacity*/ 8,
                                /*token_limit*/ 0,
                                nullptr,
                                /*idle_timeout_ms*/ 5000,
                                /*wheel_tick_ms*/ 500,
                                /*wheel_size*/ 64,
                                /*buffered_span_hard_limit*/ 1024,
                                /*active_session_hard_limit*/ 5);
    LogHandler handler(nullptr, nullptr, nullptr, &manager);

    // 这里先手动塞满 3 条 collecting trace，把 active_sessions 推到 high=3，
    // 这样后面通过 handler 发一个全新的 trace_key 时，就会稳定命中 overload 拒绝。
    SpanEvent span1 = MakeSpan(11, 1101);
    ASSERT_EQ(manager.Push(span1), TraceSessionManager::PushResult::Accepted);

    SpanEvent span2 = MakeSpan(12, 1201);
    ASSERT_EQ(manager.Push(span2), TraceSessionManager::PushResult::Accepted);

    SpanEvent span3 = MakeSpan(13, 1301);
    ASSERT_EQ(manager.Push(span3), TraceSessionManager::PushResult::Accepted);

    HttpRequest req = MakeTraceRequest(14, 1401, false);
    HttpResponse resp;
    handler.handleTracePost(req, &resp, nullptr);

    ASSERT_EQ(resp.statusCode_, HttpResponse::HttpStatusCode::k503ServiceUnavailable);
    ASSERT_EQ(resp.headers_.at("Retry-After"), "1");
    const nlohmann::json body = ParseBody(resp);
    EXPECT_EQ(body.at("accepted"), false);
    EXPECT_EQ(body.at("error"), "Trace pipeline is overloaded");
    EXPECT_EQ(body.at("retry_after_seconds"), 1);

    pool.shutdown();
}

TEST_F(LogHandlerTracePostTest, HandleTracePostReturns202AndDeferredBodyWhenDispatchIsDeferred)
{
    ThreadPool pool(1, 0);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/ 8, /*token_limit*/ 0);
    LogHandler handler(nullptr, nullptr, nullptr, &manager);
    HttpRequest req = MakeTraceRequest(3, 301, true);
    HttpResponse resp;

    handler.handleTracePost(req, &resp, nullptr);

    ASSERT_EQ(resp.statusCode_, HttpResponse::HttpStatusCode::k202Acceptd);
    ASSERT_EQ(resp.headers_.at("Content-Type"), "application/json");
    const nlohmann::json body = ParseBody(resp);
    EXPECT_EQ(body.at("accepted"), true);
    EXPECT_EQ(body.at("trace_key"), 3);
    EXPECT_EQ(body.at("span_id"), 301);
    EXPECT_EQ(body.at("deferred"), true);
    EXPECT_EQ(body.at("message"), "Trace accepted; internal dispatch deferred");
    EXPECT_EQ(resp.headers_.count("Retry-After"), 0u);

    pool.shutdown();
}
