#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "ai/AiTypes.h"

enum class SystemBackpressureStatus
{
    Normal,
    Active,
    Full
};

struct SystemOverviewSnapshot
{
    uint64_t total_logs = 0;
    uint64_t ai_call_total = 0;
    uint64_t ai_queue_wait_ms = 0;
    uint64_t ai_inference_latency_ms = 0;
    uint64_t memory_rss_mb = 0;
    std::string backpressure_status = "Normal";
};

struct SystemTokenStatsSnapshot
{
    uint64_t input_tokens = 0;
    uint64_t output_tokens = 0;
    uint64_t total_tokens = 0;
    uint64_t avg_tokens_per_call = 0;
};

struct SystemMetricPoint
{
    int64_t time_ms = 0;
    uint64_t ingest_rate = 0;
    uint64_t ai_completion_rate = 0;
};

struct SystemRuntimeSnapshot
{
    // overview 对应顶部 6 张系统运行态卡片，表达“现在系统整体处于什么状态”。
    SystemOverviewSnapshot overview;
    // token_stats 对应中间 token 卡，表达累计 token 真值和平均单次调用消耗。
    SystemTokenStatsSnapshot token_stats;
    // timeseries 对应底部折线图，只保留入口速率和 AI 完成速率两条线。
    std::vector<SystemMetricPoint> timeseries;
};

class SystemRuntimeAccumulator
{
public:
    using TimeProvider = std::function<int64_t()>;
    using MemoryProvider = std::function<uint64_t()>;

    explicit SystemRuntimeAccumulator(size_t latency_sample_limit = 64,
                                      size_t series_limit = 60,
                                      TimeProvider time_provider = {},
                                      MemoryProvider memory_provider = {});

    // 接入速率和总处理日志数都按“成功被系统接住的 span 数”累计。
    // 这里不做时间窗，只做单调总数，后续由 OnTick 做差分采样。
    void RecordAcceptedLogs(uint64_t count = 1);

    // AI 完成一次调用后，把排队等待、真实推理耗时和可选 usage 一起记进系统运行态。
    // 这里的两张延迟卡吃的是固定样本平均，而不是全局累计平均。
    void RecordAiCompletion(uint64_t queue_wait_ms,
                            uint64_t inference_latency_ms,
                            std::optional<TraceAiUsage> usage);

    // 背压状态先只收口成系统综合结论，避免前端把单一队列占用率误当成背压定义。
    void UpdateBackpressureStatus(SystemBackpressureStatus status);

    // 定时采样入口：
    // 1) 读取当前进程 RSS；
    // 2) 用单调累计总数做差分，生成每秒速率点；
    // 3) 把结果裁进固定长度序列，供系统监控折线图直接读取。
    void OnTick();

    SystemRuntimeSnapshot BuildSnapshot() const;

private:
    struct FixedSampleRing
    {
        explicit FixedSampleRing(size_t capacity = 0);

        // 固定样本 ring 只保留最近 N 条延迟样本，目的是让页面看到“最近体感”，
        // 而不是被全局历史平均拉钝。
        void Push(uint64_t value);
        uint64_t Average() const;

        std::vector<uint64_t> values;
        size_t next_index = 0;
        size_t size = 0;
        uint64_t sum = 0;
    };

    static int64_t DefaultNowMs();
    static uint64_t DefaultReadProcessRssBytes();
    static std::string ToStatusString(SystemBackpressureStatus status);

    TimeProvider time_provider_;
    MemoryProvider memory_provider_;
    size_t series_limit_ = 0;

    std::atomic<uint64_t> total_logs_{0};
    std::atomic<uint64_t> ingest_total_{0};
    std::atomic<uint64_t> ai_call_total_{0};
    std::atomic<uint64_t> ai_completion_total_{0};
    std::atomic<uint64_t> input_tokens_total_{0};
    std::atomic<uint64_t> output_tokens_total_{0};
    std::atomic<uint64_t> total_tokens_total_{0};
    std::atomic<uint64_t> memory_rss_bytes_{0};
    std::atomic<SystemBackpressureStatus> backpressure_status_{SystemBackpressureStatus::Normal};

    mutable std::mutex mutex_;
    FixedSampleRing queue_wait_samples_;
    FixedSampleRing inference_latency_samples_;
    std::vector<SystemMetricPoint> timeseries_;
    uint64_t last_ingest_total_ = 0;
    uint64_t last_ai_completion_total_ = 0;
    int64_t last_sample_time_ms_ = 0;
};
