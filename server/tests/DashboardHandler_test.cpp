#include <gtest/gtest.h>

#include <memory>

#include <nlohmann/json.hpp>

#include "core/SystemRuntimeAccumulator.h"
#include "handlers/DashboardHandler.h"

namespace
{
TraceAiUsage MakeUsage(uint64_t input_tokens,
                       uint64_t output_tokens,
                       uint64_t total_tokens)
{
    TraceAiUsage usage;
    usage.input_tokens = input_tokens;
    usage.output_tokens = output_tokens;
    usage.total_tokens = total_tokens;
    return usage;
}
} // namespace

TEST(DashboardHandlerTest, HandleGetStatsReturnsSystemRuntimeSnapshotJson)
{
    int64_t now_ms = 0;
    auto accumulator = std::make_shared<SystemRuntimeAccumulator>(/*latency_sample_limit*/4,
                                                                  /*series_limit*/8,
                                                                  [&now_ms]() { return now_ms; },
                                                                  []() { return 128ULL * 1024ULL * 1024ULL; });
    accumulator->RecordAcceptedLogs(3);
    accumulator->RecordAiCallStarted();
    accumulator->RecordAiCallCompleted(/*queue_wait_ms*/30,
                                       /*inference_latency_ms*/120,
                                       MakeUsage(9, 6, 15));
    accumulator->UpdateBackpressureStatus(SystemBackpressureStatus::Active);
    now_ms = 1000;
    accumulator->OnTick();

    DashboardHandler handler(accumulator);
    HttpRequest req;
    req.method_ = "GET";
    req.path_ = "/dashboard";
    HttpResponse resp;

    // 这里锁的是 /dashboard 现在已经不再查 SQLite 老统计，而是直接吐系统监控快照。
    // 只要 handler 还返回旧结构，这个测试就会立刻失败。
    handler.handleGetStats(req, &resp, nullptr);

    ASSERT_EQ(resp.statusCode_, HttpResponse::HttpStatusCode::k200Ok);
    ASSERT_EQ(resp.headers_.at("Content-Type"), "application/json");
    const nlohmann::json body = nlohmann::json::parse(resp.body_);
    EXPECT_EQ(body.at("overview").at("total_logs"), 3);
    EXPECT_EQ(body.at("overview").at("ai_call_total"), 1);
    EXPECT_EQ(body.at("overview").at("backpressure_status"), "Active");
    EXPECT_EQ(body.at("token_stats").at("total_tokens"), 15);
    ASSERT_TRUE(body.at("timeseries").is_array());
    ASSERT_EQ(body.at("timeseries").size(), 1u);
    EXPECT_EQ(body.at("timeseries").at(0).at("ingest_rate"), 3);
    EXPECT_EQ(body.at("timeseries").at(0).at("ai_completion_rate"), 1);
}
