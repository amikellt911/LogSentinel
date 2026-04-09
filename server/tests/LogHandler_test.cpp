#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "core/TraceSessionManager.h"
#include "core/SystemRuntimeAccumulator.h"
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
                                /*sealed_grace_window_ms*/ 1000,
                                /*retry_base_delay_ms*/ 500,
                                /*wheel_size*/ 64,
                                /*buffered_span_hard_limit*/ 1024,
                                /*active_session_hard_limit*/ 5,
                                /*active_session_overload_percent*/ 60,
                                /*active_session_critical_percent*/ 80,
                                /*buffered_spans_overload_percent*/ 75,
                                /*buffered_spans_critical_percent*/ 90,
                                /*pending_tasks_overload_percent*/ 75,
                                /*pending_tasks_critical_percent*/ 90);
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

TEST_F(LogHandlerTracePostTest, HandleTracePostReturns202WithoutDeferredBeforeSealedTraceActuallyDispatches)
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
    // 这里不能再要求 handler 立即返回 deferred。
    // 既然 trace_end 现在先进入 sealed grace window，那么 HTTP 返回发生在真正 dispatch 之前，
    // handler 只能确认“入口已经收下了这条 span”，不能抢先承诺后面的 submit 结果。
    EXPECT_EQ(body.contains("deferred"), false);
    EXPECT_EQ(body.contains("message"), false);
    EXPECT_EQ(resp.headers_.count("Retry-After"), 0u);

    pool.shutdown();
}

TEST_F(LogHandlerTracePostTest, HandleTracePostRecordsAcceptedLogsIntoSystemRuntimeAccumulator)
{
    // 目的：锁定 /logs/spans 只要被系统成功接住，就应该累计系统监控里的总处理日志数。
    // 这里不要求 trace 立刻聚合完成，因为 SystemMonitor 这张卡表达的是“入口成功接住了多少条日志/span”。
    ThreadPool pool(1, 16);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    int64_t now_ms = 0;
    SystemRuntimeAccumulator system_runtime_accumulator(/*latency_sample_limit*/4,
                                                       /*series_limit*/8,
                                                       [&now_ms]() { return now_ms; },
                                                       []() { return 0ULL; });
    TraceSessionManager manager(&pool,
                                buffered_repo.get(),
                                nullptr,
                                /*capacity*/ 8,
                                /*token_limit*/ 0,
                                nullptr,
                                /*idle_timeout_ms*/ 5000,
                                /*wheel_tick_ms*/ 500,
                                /*sealed_grace_window_ms*/ 1000,
                                /*retry_base_delay_ms*/ 500,
                                /*wheel_size*/ 64,
                                /*buffered_span_hard_limit*/ 1024,
                                /*active_session_hard_limit*/ 128,
                                /*active_session_overload_percent*/ 75,
                                /*active_session_critical_percent*/ 90,
                                /*buffered_spans_overload_percent*/ 75,
                                /*buffered_spans_critical_percent*/ 90,
                                /*pending_tasks_overload_percent*/ 75,
                                /*pending_tasks_critical_percent*/ 90,
                                nullptr,
                                &system_runtime_accumulator);
    LogHandler handler(nullptr, nullptr, nullptr, &manager, &system_runtime_accumulator);
    HttpRequest req = MakeTraceRequest(4, 401, false);
    HttpResponse resp;

    handler.handleTracePost(req, &resp, nullptr);

    ASSERT_EQ(resp.statusCode_, HttpResponse::HttpStatusCode::k202Acceptd);
    now_ms = 1000;
    system_runtime_accumulator.OnTick();
    const SystemRuntimeSnapshot snapshot = system_runtime_accumulator.BuildSnapshot();
    EXPECT_EQ(snapshot.overview.total_logs, 1u);

    pool.shutdown();
}

TEST_F(LogHandlerTracePostTest, HandlePostReturns503WhenLegacyBatcherMissing)
{
    // 主链已经不再注册旧 `/logs` 入口，所以后续把 batcher 从 main.cpp 拔掉以后，
    // 这里必须稳定返回 503，而不是一脚踩进空指针。
    LogHandler handler(nullptr, nullptr, nullptr, nullptr);
    HttpRequest req;
    req.method_ = "POST";
    req.path_ = "/logs";
    req.version_ = "HTTP/1.1";
    req.trace_id = "legacy-trace-id";
    req.body_ = R"({"message":"legacy"})";
    HttpResponse resp;

    handler.handlePost(req, &resp, nullptr);

    ASSERT_EQ(resp.statusCode_, HttpResponse::HttpStatusCode::k503ServiceUnavailable);
    ASSERT_EQ(resp.headers_.at("Content-Type"), "application/json");
    const nlohmann::json body = ParseBody(resp);
    EXPECT_EQ(body.at("error"), "Legacy log pipeline is unavailable");
}

TEST_F(LogHandlerTracePostTest, HandleGetResultReturns503WhenLegacyQueryDependenciesMissing)
{
    // 旧 `/results/*` 查询已经不再挂主路由，所以 repo/tpool 后续都可能被主程序清掉。
    // 这里锁定“依赖缺失时同步返回 503”，避免别人手工复用旧 handler 时直接崩掉。
    LogHandler handler(nullptr, nullptr, nullptr, nullptr);
    HttpRequest req;
    req.method_ = "GET";
    req.path_ = "/results/legacy-trace-id";
    req.version_ = "HTTP/1.1";
    HttpResponse resp;

    handler.handleGetResult(req, &resp, nullptr);

    ASSERT_EQ(resp.statusCode_, HttpResponse::HttpStatusCode::k503ServiceUnavailable);
    ASSERT_EQ(resp.headers_.at("Content-Type"), "application/json");
    const nlohmann::json body = ParseBody(resp);
    EXPECT_EQ(body.at("error"), "Legacy result pipeline is unavailable");
}
