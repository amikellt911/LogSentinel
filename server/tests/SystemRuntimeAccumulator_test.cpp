#include <gtest/gtest.h>

#include "core/SystemRuntimeAccumulator.h"

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

TEST(SystemRuntimeAccumulatorTest, BuildSnapshotReturnsStableEmptySnapshotByDefault)
{
    int64_t now_ms = 0;
    SystemRuntimeAccumulator accumulator(/*latency_sample_limit*/4,
                                         /*series_limit*/8,
                                         [&now_ms]()
                                         {
                                             return now_ms;
                                         },
                                         []()
                                         {
                                             return static_cast<uint64_t>(0);
                                         });

    // 刚构造时不应该返回半成品；即使系统监控页先启动，后端也要给出结构稳定的空快照。
    const SystemRuntimeSnapshot snapshot = accumulator.BuildSnapshot();
    EXPECT_EQ(snapshot.overview.total_logs, 0U);
    EXPECT_EQ(snapshot.overview.ai_call_total, 0U);
    EXPECT_EQ(snapshot.overview.ai_queue_wait_ms, 0U);
    EXPECT_EQ(snapshot.overview.ai_inference_latency_ms, 0U);
    EXPECT_EQ(snapshot.overview.memory_rss_mb, 0U);
    EXPECT_EQ(snapshot.overview.backpressure_status, "Normal");
    EXPECT_EQ(snapshot.token_stats.input_tokens, 0U);
    EXPECT_EQ(snapshot.token_stats.output_tokens, 0U);
    EXPECT_EQ(snapshot.token_stats.total_tokens, 0U);
    EXPECT_EQ(snapshot.token_stats.avg_tokens_per_call, 0U);
    EXPECT_TRUE(snapshot.timeseries.empty());
}

TEST(SystemRuntimeAccumulatorTest, TickPublishesCountersLatencyAveragesAndRates)
{
    int64_t now_ms = 0;
    SystemRuntimeAccumulator accumulator(/*latency_sample_limit*/4,
                                         /*series_limit*/8,
                                         [&now_ms]()
                                         {
                                             return now_ms;
                                         },
                                         []()
                                         {
                                             return static_cast<uint64_t>(256ULL * 1024ULL * 1024ULL);
                                         });

    accumulator.RecordAcceptedLogs(5);
    accumulator.RecordAiCompletion(/*queue_wait_ms*/40, /*inference_latency_ms*/200, MakeUsage(10, 15, 25));
    accumulator.RecordAiCompletion(/*queue_wait_ms*/20, /*inference_latency_ms*/100, MakeUsage(20, 30, 50));
    accumulator.UpdateBackpressureStatus(SystemBackpressureStatus::Active);

    now_ms = 1000;
    accumulator.OnTick();

    // 这里锁的是系统监控第一版最小口径：
    // 1) 总量直接累计；
    // 2) 两张延迟卡吃固定样本平均；
    // 3) 折线图速率由“单调总数做差分”得到，而不是每秒把总数清零。
    const SystemRuntimeSnapshot snapshot = accumulator.BuildSnapshot();
    EXPECT_EQ(snapshot.overview.total_logs, 5U);
    EXPECT_EQ(snapshot.overview.ai_call_total, 2U);
    EXPECT_EQ(snapshot.overview.ai_queue_wait_ms, 30U);
    EXPECT_EQ(snapshot.overview.ai_inference_latency_ms, 150U);
    EXPECT_EQ(snapshot.overview.memory_rss_mb, 256U);
    EXPECT_EQ(snapshot.overview.backpressure_status, "Active");

    EXPECT_EQ(snapshot.token_stats.input_tokens, 30U);
    EXPECT_EQ(snapshot.token_stats.output_tokens, 45U);
    EXPECT_EQ(snapshot.token_stats.total_tokens, 75U);
    EXPECT_EQ(snapshot.token_stats.avg_tokens_per_call, 37U);

    ASSERT_EQ(snapshot.timeseries.size(), 1U);
    EXPECT_EQ(snapshot.timeseries[0].ingest_rate, 5U);
    EXPECT_EQ(snapshot.timeseries[0].ai_completion_rate, 2U);
}

TEST(SystemRuntimeAccumulatorTest, TickUsesMonotonicTotalsToComputePerSecondRates)
{
    int64_t now_ms = 0;
    SystemRuntimeAccumulator accumulator(/*latency_sample_limit*/4,
                                         /*series_limit*/8,
                                         [&now_ms]()
                                         {
                                             return now_ms;
                                         },
                                         []()
                                         {
                                             return static_cast<uint64_t>(0);
                                         });

    accumulator.RecordAcceptedLogs(3);
    accumulator.RecordAiCompletion(/*queue_wait_ms*/10, /*inference_latency_ms*/20, std::nullopt);
    now_ms = 1000;
    accumulator.OnTick();

    now_ms = 2000;
    accumulator.OnTick();

    const SystemRuntimeSnapshot snapshot = accumulator.BuildSnapshot();
    ASSERT_EQ(snapshot.timeseries.size(), 2U);
    EXPECT_EQ(snapshot.timeseries[0].ingest_rate, 3U);
    EXPECT_EQ(snapshot.timeseries[0].ai_completion_rate, 1U);
    EXPECT_EQ(snapshot.timeseries[1].ingest_rate, 0U);
    EXPECT_EQ(snapshot.timeseries[1].ai_completion_rate, 0U);
}

TEST(SystemRuntimeAccumulatorTest, RecentLatencyUsesFixedSampleWindowInsteadOfGlobalAverage)
{
    int64_t now_ms = 0;
    SystemRuntimeAccumulator accumulator(/*latency_sample_limit*/2,
                                         /*series_limit*/8,
                                         [&now_ms]()
                                         {
                                             return now_ms;
                                         },
                                         []()
                                         {
                                             return static_cast<uint64_t>(0);
                                         });

    accumulator.RecordAiCompletion(/*queue_wait_ms*/10, /*inference_latency_ms*/100, std::nullopt);
    accumulator.RecordAiCompletion(/*queue_wait_ms*/20, /*inference_latency_ms*/200, std::nullopt);
    accumulator.RecordAiCompletion(/*queue_wait_ms*/100, /*inference_latency_ms*/400, std::nullopt);

    const SystemRuntimeSnapshot snapshot = accumulator.BuildSnapshot();
    EXPECT_EQ(snapshot.overview.ai_queue_wait_ms, 60U);
    EXPECT_EQ(snapshot.overview.ai_inference_latency_ms, 300U);
}
