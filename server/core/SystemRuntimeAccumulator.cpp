#include "core/SystemRuntimeAccumulator.h"

#include <fstream>
#include <sstream>
#include <unistd.h>

namespace
{
uint64_t BytesToMb(uint64_t bytes)
{
    return bytes / (1024ULL * 1024ULL);
}
} // namespace

SystemRuntimeAccumulator::FixedLatencySampleWindow::FixedLatencySampleWindow(size_t capacity)
    : values(capacity)
{
}

void SystemRuntimeAccumulator::FixedLatencySampleWindow::Push(uint64_t queue_wait_ms,
                                                             uint64_t inference_latency_ms)
{
    if (values.empty())
    {
        return;
    }

    if (size < values.size())
    {
        values[next_index].queue_wait_ms = queue_wait_ms;
        values[next_index].inference_latency_ms = inference_latency_ms;
        queue_wait_sum += queue_wait_ms;
        inference_latency_sum += inference_latency_ms;
        ++size;
        next_index = (next_index + 1) % values.size();
        return;
    }

    queue_wait_sum -= values[next_index].queue_wait_ms;
    inference_latency_sum -= values[next_index].inference_latency_ms;
    values[next_index].queue_wait_ms = queue_wait_ms;
    values[next_index].inference_latency_ms = inference_latency_ms;
    queue_wait_sum += queue_wait_ms;
    inference_latency_sum += inference_latency_ms;
    next_index = (next_index + 1) % values.size();
}

uint64_t SystemRuntimeAccumulator::FixedLatencySampleWindow::AverageQueueWaitMs() const
{
    if (size == 0)
    {
        return 0;
    }
    return queue_wait_sum / size;
}

uint64_t SystemRuntimeAccumulator::FixedLatencySampleWindow::AverageInferenceLatencyMs() const
{
    if (size == 0)
    {
        return 0;
    }
    return inference_latency_sum / size;
}

int64_t SystemRuntimeAccumulator::DefaultNowMs()
{
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

uint64_t SystemRuntimeAccumulator::DefaultReadProcessRssBytes()
{
    std::ifstream statm("/proc/self/statm");
    if (!statm.is_open())
    {
        return 0;
    }

    uint64_t total_pages = 0;
    uint64_t resident_pages = 0;
    statm >> total_pages >> resident_pages;
    if (!statm.good() && !statm.eof())
    {
        return 0;
    }

    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
    {
        return 0;
    }
    return resident_pages * static_cast<uint64_t>(page_size);
}

std::string SystemRuntimeAccumulator::ToStatusString(SystemBackpressureStatus status)
{
    switch (status)
    {
    case SystemBackpressureStatus::Normal:
        return "Normal";
    case SystemBackpressureStatus::Active:
        return "Active";
    case SystemBackpressureStatus::Full:
        return "Full";
    }
    return "Normal";
}

SystemRuntimeAccumulator::SystemRuntimeAccumulator(size_t latency_sample_limit,
                                                   size_t series_limit,
                                                   TimeProvider time_provider,
                                                   MemoryProvider memory_provider)
    : time_provider_(time_provider ? std::move(time_provider) : DefaultNowMs),
      memory_provider_(memory_provider ? std::move(memory_provider) : DefaultReadProcessRssBytes),
      series_limit_(series_limit > 0 ? series_limit : 60),
      latency_samples_(latency_sample_limit),
      last_sample_time_ms_(time_provider_())
{
}

void SystemRuntimeAccumulator::RecordAcceptedLogs(uint64_t count)
{
    total_logs_.fetch_add(count, std::memory_order_relaxed);
    ingest_total_.fetch_add(count, std::memory_order_relaxed);
}

void SystemRuntimeAccumulator::RecordAiCallStarted()
{
    ai_call_total_.fetch_add(1, std::memory_order_relaxed);
}

void SystemRuntimeAccumulator::RecordAiCallCompleted(uint64_t queue_wait_ms,
                                                    uint64_t inference_latency_ms,
                                                    std::optional<TraceAiUsage> usage)
{
    ai_completion_total_.fetch_add(1, std::memory_order_relaxed);

    if (usage.has_value())
    {
        input_tokens_total_.fetch_add(usage->input_tokens, std::memory_order_relaxed);
        output_tokens_total_.fetch_add(usage->output_tokens, std::memory_order_relaxed);
        total_tokens_total_.fetch_add(usage->total_tokens, std::memory_order_relaxed);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    // 这里在一次短锁里把“同一调用的排队时间 + 推理时间”一起写进最近样本窗口。
    // 既然这两个值天然成对出现，那就不要拆成两套彼此独立的窗口，否则后面解释口径会越来越脏。
    latency_samples_.Push(queue_wait_ms, inference_latency_ms);
}

void SystemRuntimeAccumulator::UpdateBackpressureStatus(SystemBackpressureStatus status)
{
    backpressure_status_.store(status, std::memory_order_relaxed);
}

void SystemRuntimeAccumulator::OnTick()
{
    memory_rss_bytes_.store(memory_provider_(), std::memory_order_relaxed);

    const int64_t now_ms = time_provider_();
    std::lock_guard<std::mutex> lock(mutex_);
    if (now_ms <= last_sample_time_ms_)
    {
        return;
    }

    const uint64_t current_ingest_total = ingest_total_.load(std::memory_order_relaxed);
    const uint64_t current_ai_completion_total = ai_completion_total_.load(std::memory_order_relaxed);
    const uint64_t elapsed_ms = static_cast<uint64_t>(now_ms - last_sample_time_ms_);
    const uint64_t ingest_delta = current_ingest_total - last_ingest_total_;
    const uint64_t ai_completion_delta = current_ai_completion_total - last_ai_completion_total_;

    // 这里坚持用“单调累计值做差分”，而不是每秒清零总数。
    // 既然热路径可能有多线程同时记账，那么只做 fetch_add + 采样差分最稳，
    // 不会引入“清零时刚好撞到写线程”的额外同步复杂度。
    SystemMetricPoint point;
    point.time_ms = now_ms;
    point.ingest_rate = elapsed_ms > 0 ? (ingest_delta * 1000ULL) / elapsed_ms : 0;
    point.ai_completion_rate = elapsed_ms > 0 ? (ai_completion_delta * 1000ULL) / elapsed_ms : 0;
    timeseries_.push_back(point);
    if (timeseries_.size() > series_limit_)
    {
        timeseries_.erase(timeseries_.begin(),
                          timeseries_.begin() + static_cast<std::ptrdiff_t>(timeseries_.size() - series_limit_));
    }

    last_ingest_total_ = current_ingest_total;
    last_ai_completion_total_ = current_ai_completion_total;
    last_sample_time_ms_ = now_ms;
}

SystemRuntimeSnapshot SystemRuntimeAccumulator::BuildSnapshot() const
{
    SystemRuntimeSnapshot snapshot;
    snapshot.overview.total_logs = total_logs_.load(std::memory_order_relaxed);
    snapshot.overview.ai_call_total = ai_call_total_.load(std::memory_order_relaxed);
    snapshot.overview.memory_rss_mb = BytesToMb(memory_rss_bytes_.load(std::memory_order_relaxed));
    snapshot.overview.backpressure_status =
        ToStatusString(backpressure_status_.load(std::memory_order_relaxed));

    snapshot.token_stats.input_tokens = input_tokens_total_.load(std::memory_order_relaxed);
    snapshot.token_stats.output_tokens = output_tokens_total_.load(std::memory_order_relaxed);
    snapshot.token_stats.total_tokens = total_tokens_total_.load(std::memory_order_relaxed);
    if (snapshot.overview.ai_call_total > 0)
    {
        snapshot.token_stats.avg_tokens_per_call =
            snapshot.token_stats.total_tokens / snapshot.overview.ai_call_total;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    // 这两张延迟卡现在故意吃“固定样本平均”，不是全局累计平均。
    // 这样页面既不会像最后一条样本那样乱跳，也不会因为历史太长而完全失去敏感度。
    snapshot.overview.ai_queue_wait_ms = latency_samples_.AverageQueueWaitMs();
    snapshot.overview.ai_inference_latency_ms = latency_samples_.AverageInferenceLatencyMs();
    snapshot.timeseries = timeseries_;
    return snapshot;
}
