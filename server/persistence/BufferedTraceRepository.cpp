#include "persistence/BufferedTraceRepository.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

uint64_t NowNs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
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
    const RuntimeStatsSnapshot stats = SnapshotRuntimeStats();
    if (stats.primary_append_calls == 0 &&
        stats.analysis_append_calls == 0 &&
        stats.primary_flush_calls == 0 &&
        stats.analysis_flush_calls == 0) {
        return;
    }
    std::clog << "[BufferedTraceRuntimeStats] " << DescribeRuntimeStats() << std::endl;
}

bool BufferedTraceRepository::AppendPrimary(TracePrimaryWrite write)
{
    primary_append_calls_.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(primary_mutex_);

    if (!current_primary_) {
        current_primary_ = CreatePrimaryBuffer();
    }
    if (!next_primary_) {
        next_primary_ = CreatePrimaryBuffer();
    }

    if (current_primary_->Empty()) {
        current_primary_->first_enqueue_ms = NowMs();
    }

    current_primary_->summaries.push_back(std::move(write.summary));
    current_primary_->spans.insert(current_primary_->spans.end(),
                                   std::make_move_iterator(write.spans.begin()),
                                   std::make_move_iterator(write.spans.end()));

    if (!ShouldFlushPrimaryCurrentBySizeLocked()) {
        return true;
    }

    RotatePrimaryBuffersLocked();
    flush_cv_.notify_one();
    return true;
}

bool BufferedTraceRepository::AppendAnalysis(TraceAnalysisWrite write)
{
    analysis_append_calls_.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(analysis_mutex_);

    if (!current_analysis_) {
        current_analysis_ = CreateAnalysisBuffer();
    }
    if (!next_analysis_) {
        next_analysis_ = CreateAnalysisBuffer();
    }

    if (current_analysis_->Empty()) {
        current_analysis_->first_enqueue_ms = NowMs();
    }

    if (write.analysis.has_value()) {
        current_analysis_->analyses.push_back(std::move(write.analysis.value()));
    }
    if (write.prompt_debug.has_value()) {
        current_analysis_->prompt_debugs.push_back(std::move(write.prompt_debug.value()));
    }

    if (!ShouldFlushAnalysisCurrentBySizeLocked()) {
        return true;
    }

    RotateAnalysisBuffersLocked();
    flush_cv_.notify_one();
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

bool BufferedTraceRepository::ShouldFlushPrimaryCurrentBySizeLocked() const
{
    if (!current_primary_ || current_primary_->Empty()) {
        return false;
    }

    // 主数据缓冲区的主水位先盯 spans。既然 summary 一条 trace 只有一条，
    // 真正占内存、真正影响 SQLite 批量写时长的还是 spans 这边。
    return current_primary_->spans.size() >= config_.primary_span_reserve;
}

bool BufferedTraceRepository::ShouldFlushAnalysisCurrentBySizeLocked() const
{
    if (!current_analysis_ || current_analysis_->Empty()) {
        return false;
    }

    // 分析结果这条线先以 analyses 条数做主水位。
    // 既然 prompt_debug 本来就是可选表，就不该拿它的数量做主条件。
    return current_analysis_->analyses.size() >= config_.analysis_reserve;
}

bool BufferedTraceRepository::ShouldFlushPrimaryCurrentByTimeLocked(int64_t now_ms) const
{
    if (!current_primary_ || current_primary_->Empty() || current_primary_->first_enqueue_ms <= 0) {
        return false;
    }
    return now_ms - current_primary_->first_enqueue_ms >= config_.primary_flush_interval_ms;
}

bool BufferedTraceRepository::ShouldFlushAnalysisCurrentByTimeLocked(int64_t now_ms) const
{
    if (!current_analysis_ || current_analysis_->Empty() || current_analysis_->first_enqueue_ms <= 0) {
        return false;
    }
    return now_ms - current_analysis_->first_enqueue_ms >= config_.analysis_flush_interval_ms;
}

BufferedTraceRepository::PrimaryBufferPtr BufferedTraceRepository::TakeOrCreateFreePrimaryBufferLocked()
{
    if (!free_primary_buffers_.empty()) {
        auto buffer = std::move(free_primary_buffers_.back());
        free_primary_buffers_.pop_back();
        return buffer;
    }
    return CreatePrimaryBuffer();
}

BufferedTraceRepository::AnalysisBufferPtr BufferedTraceRepository::TakeOrCreateFreeAnalysisBufferLocked()
{
    if (!free_analysis_buffers_.empty()) {
        auto buffer = std::move(free_analysis_buffers_.back());
        free_analysis_buffers_.pop_back();
        return buffer;
    }
    return CreateAnalysisBuffer();
}

void BufferedTraceRepository::RotatePrimaryBuffersLocked()
{
    if (!current_primary_ || current_primary_->Empty()) {
        return;
    }

    full_primary_buffers_.push_back(std::move(current_primary_));
    current_primary_ = std::move(next_primary_);
    if (!current_primary_) {
        current_primary_ = CreatePrimaryBuffer();
    }
    next_primary_ = TakeOrCreateFreePrimaryBufferLocked();
}

void BufferedTraceRepository::RotateAnalysisBuffersLocked()
{
    if (!current_analysis_ || current_analysis_->Empty()) {
        return;
    }

    full_analysis_buffers_.push_back(std::move(current_analysis_));
    current_analysis_ = std::move(next_analysis_);
    if (!current_analysis_) {
        current_analysis_ = CreateAnalysisBuffer();
    }
    next_analysis_ = TakeOrCreateFreeAnalysisBufferLocked();
}

BufferedTraceRepository::PrimaryBufferPtr BufferedTraceRepository::TakeOneFullPrimaryBufferLocked()
{
    if (full_primary_buffers_.empty()) {
        return nullptr;
    }
    auto buffer = std::move(full_primary_buffers_.front());
    full_primary_buffers_.erase(full_primary_buffers_.begin());
    return buffer;
}

BufferedTraceRepository::AnalysisBufferPtr BufferedTraceRepository::TakeOneFullAnalysisBufferLocked()
{
    if (full_analysis_buffers_.empty()) {
        return nullptr;
    }
    auto buffer = std::move(full_analysis_buffers_.front());
    full_analysis_buffers_.erase(full_analysis_buffers_.begin());
    return buffer;
}

BufferedTraceRepository::PrimaryBufferPtr BufferedTraceRepository::TakeOnePrimaryBufferForFlushLocked(int64_t now_ms,
                                                                                                      bool draining)
{
    if (!full_primary_buffers_.empty()) {
        return TakeOneFullPrimaryBufferLocked();
    }

    if ((draining || ShouldFlushPrimaryCurrentByTimeLocked(now_ms)) && current_primary_ && !current_primary_->Empty()) {
        RotatePrimaryBuffersLocked();
        return TakeOneFullPrimaryBufferLocked();
    }

    return nullptr;
}

BufferedTraceRepository::AnalysisBufferPtr BufferedTraceRepository::TakeOneAnalysisBufferForFlushLocked(int64_t now_ms,
                                                                                                        bool draining)
{
    if (!full_analysis_buffers_.empty()) {
        return TakeOneFullAnalysisBufferLocked();
    }

    if ((draining || ShouldFlushAnalysisCurrentByTimeLocked(now_ms)) && current_analysis_ && !current_analysis_->Empty()) {
        RotateAnalysisBuffersLocked();
        return TakeOneFullAnalysisBufferLocked();
    }

    return nullptr;
}

void BufferedTraceRepository::RecyclePrimaryBuffer(PrimaryBufferPtr buffer)
{
    if (!buffer) {
        return;
    }
    buffer->ClearButKeepCapacity();
    std::lock_guard<std::mutex> lock(primary_mutex_);
    free_primary_buffers_.push_back(std::move(buffer));
}

void BufferedTraceRepository::RecycleAnalysisBuffer(AnalysisBufferPtr buffer)
{
    if (!buffer) {
        return;
    }
    buffer->ClearButKeepCapacity();
    std::lock_guard<std::mutex> lock(analysis_mutex_);
    free_analysis_buffers_.push_back(std::move(buffer));
}

void BufferedTraceRepository::FlushLoop()
{
    const auto primary_interval = std::chrono::milliseconds(std::max<int64_t>(1, config_.primary_flush_interval_ms));
    const auto analysis_interval = std::chrono::milliseconds(std::max<int64_t>(1, config_.analysis_flush_interval_ms));
    const auto sleep_interval = std::min(primary_interval, analysis_interval);

    std::unique_lock<std::mutex> lock(state_mutex_);
    while (!stopping_) {
        // 这里只负责“把后台线程叫醒”，不要把 stopping_ 写成唯一 predicate。
        // 既然前台写满桶时会 notify_one()，那这里就应该立刻醒来；
        // 醒来以后再统一检查 stopping_、满桶队列和按时间切 current 的条件。
        flush_cv_.wait_for(lock, sleep_interval);
        if (stopping_) {
            break;
        }
        lock.unlock();

        PrimaryBufferPtr primary_buffer;
        AnalysisBufferPtr analysis_buffer;
        const int64_t now_ms = NowMs();

        {
            std::lock_guard<std::mutex> primary_lock(primary_mutex_);
            primary_buffer = TakeOnePrimaryBufferForFlushLocked(now_ms, false);
        }
        {
            std::lock_guard<std::mutex> analysis_lock(analysis_mutex_);
            analysis_buffer = TakeOneAnalysisBufferForFlushLocked(now_ms, false);
        }

        if (primary_buffer) {
            if (!primary_buffer->summaries.empty() || !primary_buffer->spans.empty()) {
                const uint64_t flush_begin_ns = NowNs();
                const bool saved = sink_->SavePrimaryBatch(primary_buffer->summaries, primary_buffer->spans);
                primary_flush_calls_.fetch_add(1, std::memory_order_relaxed);
                primary_flush_total_ns_.fetch_add(NowNs() - flush_begin_ns, std::memory_order_relaxed);
                primary_flushed_summary_count_.fetch_add(primary_buffer->summaries.size(), std::memory_order_relaxed);
                primary_flushed_span_count_.fetch_add(primary_buffer->spans.size(), std::memory_order_relaxed);
                if (!saved) {
                    primary_flush_fail_count_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            RecyclePrimaryBuffer(std::move(primary_buffer));
        }

        if (analysis_buffer) {
            if (!analysis_buffer->analyses.empty() || !analysis_buffer->prompt_debugs.empty()) {
                const uint64_t flush_begin_ns = NowNs();
                const bool saved = sink_->SaveAnalysisBatch(analysis_buffer->analyses, analysis_buffer->prompt_debugs);
                analysis_flush_calls_.fetch_add(1, std::memory_order_relaxed);
                analysis_flush_total_ns_.fetch_add(NowNs() - flush_begin_ns, std::memory_order_relaxed);
                analysis_flushed_analysis_count_.fetch_add(analysis_buffer->analyses.size(), std::memory_order_relaxed);
                analysis_flushed_prompt_debug_count_.fetch_add(analysis_buffer->prompt_debugs.size(), std::memory_order_relaxed);
                if (!saved) {
                    analysis_flush_fail_count_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            RecycleAnalysisBuffer(std::move(analysis_buffer));
        }

        lock.lock();
    }

    lock.unlock();

    while (true) {
        PrimaryBufferPtr primary_buffer;
        AnalysisBufferPtr analysis_buffer;
        const int64_t now_ms = NowMs();

        {
            std::lock_guard<std::mutex> primary_lock(primary_mutex_);
            primary_buffer = TakeOnePrimaryBufferForFlushLocked(now_ms, true);
        }
        {
            std::lock_guard<std::mutex> analysis_lock(analysis_mutex_);
            analysis_buffer = TakeOneAnalysisBufferForFlushLocked(now_ms, true);
        }

        if (!primary_buffer && !analysis_buffer) {
            break;
        }

        if (primary_buffer) {
            if (!primary_buffer->summaries.empty() || !primary_buffer->spans.empty()) {
                const uint64_t flush_begin_ns = NowNs();
                const bool saved = sink_->SavePrimaryBatch(primary_buffer->summaries, primary_buffer->spans);
                primary_flush_calls_.fetch_add(1, std::memory_order_relaxed);
                primary_flush_total_ns_.fetch_add(NowNs() - flush_begin_ns, std::memory_order_relaxed);
                primary_flushed_summary_count_.fetch_add(primary_buffer->summaries.size(), std::memory_order_relaxed);
                primary_flushed_span_count_.fetch_add(primary_buffer->spans.size(), std::memory_order_relaxed);
                if (!saved) {
                    primary_flush_fail_count_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            RecyclePrimaryBuffer(std::move(primary_buffer));
        }

        if (analysis_buffer) {
            if (!analysis_buffer->analyses.empty() || !analysis_buffer->prompt_debugs.empty()) {
                const uint64_t flush_begin_ns = NowNs();
                const bool saved = sink_->SaveAnalysisBatch(analysis_buffer->analyses, analysis_buffer->prompt_debugs);
                analysis_flush_calls_.fetch_add(1, std::memory_order_relaxed);
                analysis_flush_total_ns_.fetch_add(NowNs() - flush_begin_ns, std::memory_order_relaxed);
                analysis_flushed_analysis_count_.fetch_add(analysis_buffer->analyses.size(), std::memory_order_relaxed);
                analysis_flushed_prompt_debug_count_.fetch_add(analysis_buffer->prompt_debugs.size(), std::memory_order_relaxed);
                if (!saved) {
                    analysis_flush_fail_count_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            RecycleAnalysisBuffer(std::move(analysis_buffer));
        }
    }
}

BufferedTraceRepository::RuntimeStatsSnapshot BufferedTraceRepository::SnapshotRuntimeStats() const
{
    RuntimeStatsSnapshot stats;
    stats.primary_append_calls = primary_append_calls_.load(std::memory_order_relaxed);
    stats.analysis_append_calls = analysis_append_calls_.load(std::memory_order_relaxed);
    stats.primary_flush_calls = primary_flush_calls_.load(std::memory_order_relaxed);
    stats.primary_flush_fail_count = primary_flush_fail_count_.load(std::memory_order_relaxed);
    stats.primary_flush_total_ns = primary_flush_total_ns_.load(std::memory_order_relaxed);
    stats.primary_flushed_summary_count = primary_flushed_summary_count_.load(std::memory_order_relaxed);
    stats.primary_flushed_span_count = primary_flushed_span_count_.load(std::memory_order_relaxed);
    stats.analysis_flush_calls = analysis_flush_calls_.load(std::memory_order_relaxed);
    stats.analysis_flush_fail_count = analysis_flush_fail_count_.load(std::memory_order_relaxed);
    stats.analysis_flush_total_ns = analysis_flush_total_ns_.load(std::memory_order_relaxed);
    stats.analysis_flushed_analysis_count = analysis_flushed_analysis_count_.load(std::memory_order_relaxed);
    stats.analysis_flushed_prompt_debug_count = analysis_flushed_prompt_debug_count_.load(std::memory_order_relaxed);
    return stats;
}

std::string BufferedTraceRepository::DescribeRuntimeStats() const
{
    const RuntimeStatsSnapshot stats = SnapshotRuntimeStats();
    std::ostringstream oss;
    oss << "primary_append_calls=" << stats.primary_append_calls
        << ", analysis_append_calls=" << stats.analysis_append_calls
        << ", primary_flush_calls=" << stats.primary_flush_calls
        << ", primary_flush_fail_count=" << stats.primary_flush_fail_count
        << ", primary_flush_total_ns=" << stats.primary_flush_total_ns
        << ", primary_flush_avg_ms="
        << (stats.primary_flush_calls > 0 ? (static_cast<double>(stats.primary_flush_total_ns) / stats.primary_flush_calls / 1'000'000.0) : 0.0)
        << ", primary_flushed_summary_count=" << stats.primary_flushed_summary_count
        << ", primary_flushed_span_count=" << stats.primary_flushed_span_count
        << ", analysis_flush_calls=" << stats.analysis_flush_calls
        << ", analysis_flush_fail_count=" << stats.analysis_flush_fail_count
        << ", analysis_flush_total_ns=" << stats.analysis_flush_total_ns
        << ", analysis_flush_avg_ms="
        << (stats.analysis_flush_calls > 0 ? (static_cast<double>(stats.analysis_flush_total_ns) / stats.analysis_flush_calls / 1'000'000.0) : 0.0)
        << ", analysis_flushed_analysis_count=" << stats.analysis_flushed_analysis_count
        << ", analysis_flushed_prompt_debug_count=" << stats.analysis_flushed_prompt_debug_count;
    return oss.str();
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
