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

    if (!ShouldFlushPrimaryCurrentLocked()) {
        return true;
    }

    RotatePrimaryBuffersLocked();
    flush_cv_.notify_one();
    return true;
}

bool BufferedTraceRepository::AppendAnalysis(TraceAnalysisWrite write)
{
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

    if (!ShouldFlushAnalysisCurrentLocked()) {
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

bool BufferedTraceRepository::ShouldFlushPrimaryCurrentLocked() const
{
    if (!current_primary_ || current_primary_->Empty()) {
        return false;
    }

    // 主数据缓冲区的主水位先盯 spans。既然 summary 一条 trace 只有一条，
    // 真正占内存、真正影响 SQLite 批量写时长的还是 spans 这边。
    return current_primary_->spans.size() >= config_.primary_span_reserve;
}

bool BufferedTraceRepository::ShouldFlushAnalysisCurrentLocked() const
{
    if (!current_analysis_ || current_analysis_->Empty()) {
        return false;
    }

    // 分析结果这条线先以 analyses 条数做主水位。
    // 既然 prompt_debug 本来就是可选表，就不该拿它的数量做主条件。
    return current_analysis_->analyses.size() >= config_.analysis_reserve;
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
        flush_cv_.wait_for(lock, sleep_interval, [this]() { return stopping_; });
        lock.unlock();

        PrimaryBufferPtr primary_buffer;
        AnalysisBufferPtr analysis_buffer;

        {
            std::lock_guard<std::mutex> primary_lock(primary_mutex_);
            primary_buffer = TakeOneFullPrimaryBufferLocked();
        }
        {
            std::lock_guard<std::mutex> analysis_lock(analysis_mutex_);
            analysis_buffer = TakeOneFullAnalysisBufferLocked();
        }

        if (primary_buffer) {
            if (!primary_buffer->summaries.empty() || !primary_buffer->spans.empty()) {
                (void)sink_->SavePrimaryBatch(primary_buffer->summaries, primary_buffer->spans);
            }
            RecyclePrimaryBuffer(std::move(primary_buffer));
        }

        if (analysis_buffer) {
            if (!analysis_buffer->analyses.empty() || !analysis_buffer->prompt_debugs.empty()) {
                (void)sink_->SaveAnalysisBatch(analysis_buffer->analyses, analysis_buffer->prompt_debugs);
            }
            RecycleAnalysisBuffer(std::move(analysis_buffer));
        }

        lock.lock();
    }

    lock.unlock();

    while (true) {
        PrimaryBufferPtr primary_buffer;
        AnalysisBufferPtr analysis_buffer;

        {
            std::lock_guard<std::mutex> primary_lock(primary_mutex_);
            primary_buffer = TakeOneFullPrimaryBufferLocked();
        }
        {
            std::lock_guard<std::mutex> analysis_lock(analysis_mutex_);
            analysis_buffer = TakeOneFullAnalysisBufferLocked();
        }

        if (!primary_buffer && !analysis_buffer) {
            break;
        }

        if (primary_buffer) {
            if (!primary_buffer->summaries.empty() || !primary_buffer->spans.empty()) {
                (void)sink_->SavePrimaryBatch(primary_buffer->summaries, primary_buffer->spans);
            }
            RecyclePrimaryBuffer(std::move(primary_buffer));
        }

        if (analysis_buffer) {
            if (!analysis_buffer->analyses.empty() || !analysis_buffer->prompt_debugs.empty()) {
                (void)sink_->SaveAnalysisBatch(analysis_buffer->analyses, analysis_buffer->prompt_debugs);
            }
            RecycleAnalysisBuffer(std::move(analysis_buffer));
        }
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
