#include "persistence/BufferedTraceRepository.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace
{
int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
}

BufferedTraceRepository::BufferedTraceRepository(std::shared_ptr<TraceRepository> sink)
    : BufferedTraceRepository(std::move(sink), Config{})
{
}

BufferedTraceRepository::BufferedTraceRepository(std::shared_ptr<TraceRepository> sink, Config config)
    : sink_(std::move(sink)), config_(config)
{
    if (!sink_) {
        throw std::invalid_argument("BufferedTraceRepository requires non-null sink");
    }
    if (config_.initial_buffer_count < 4) {
        // 第一版最少也要保住 current/next/free/free 这四只桶，
        // 否则前台一旦连续切桶，马上就会退化回热路径临时分配。
        config_.initial_buffer_count = 4;
    }

    current_primary_ = CreatePrimaryBuffer();
    next_primary_ = CreatePrimaryBuffer();
    current_analysis_ = CreateAnalysisBuffer();
    next_analysis_ = CreateAnalysisBuffer();

    for (size_t i = 2; i < config_.initial_buffer_count; ++i) {
        free_primary_buffers_.push_back(CreatePrimaryBuffer());
        free_analysis_buffers_.push_back(CreateAnalysisBuffer());
    }

    flush_thread_ = std::thread(&BufferedTraceRepository::FlushLoop, this);
}

BufferedTraceRepository::~BufferedTraceRepository()
{
    StopFlushThread();
}

bool BufferedTraceRepository::AppendPrimary(TracePrimaryWrite write)
{
    (void)write;
    // 这一刀先把类边界收正：外层只暴露两个 append 入口。
    // 真正的“写 current / 达水位切桶 / 进 full 队列”下一刀再接。
    return true;
}

bool BufferedTraceRepository::AppendAnalysis(TraceAnalysisWrite write)
{
    (void)write;
    return true;
}

BufferedTraceRepository::PrimaryBufferPtr BufferedTraceRepository::CreatePrimaryBuffer() const
{
    auto buffer = std::make_unique<PrimaryBufferGroup>();
    buffer->summaries.reserve(config_.primary_summary_reserve);
    buffer->spans.reserve(config_.primary_span_reserve);
    return buffer;
}

BufferedTraceRepository::AnalysisBufferPtr BufferedTraceRepository::CreateAnalysisBuffer() const
{
    auto buffer = std::make_unique<AnalysisBufferGroup>();
    buffer->analyses.reserve(config_.analysis_reserve);
    buffer->prompt_debugs.reserve(config_.prompt_debug_reserve);
    return buffer;
}

void BufferedTraceRepository::FlushLoop()
{
    const auto primary_interval = std::chrono::milliseconds(std::max<int64_t>(1, config_.primary_flush_interval_ms));
    const auto analysis_interval = std::chrono::milliseconds(std::max<int64_t>(1, config_.analysis_flush_interval_ms));
    const auto sleep_interval = std::min(primary_interval, analysis_interval);

    std::unique_lock<std::mutex> lock(state_mutex_);
    while (!stopping_) {
        // 第一版先只把线程生命周期和唤醒骨架立起来。
        // 真正的“切桶 + 取桶 + 无锁 flush”逻辑下一刀再接，不在这一刀里把并发状态机和 SQL 一起揉进去。
        flush_cv_.wait_for(lock, sleep_interval, [this]() { return stopping_; });
        (void)NowMs();
    }
}

void BufferedTraceRepository::StopFlushThread()
{
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
    }
    flush_cv_.notify_all();
    if (flush_thread_.joinable()) {
        flush_thread_.join();
    }
}
