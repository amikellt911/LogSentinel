#include "core/TraceSessionManager.h"
#include "threadpool/ThreadPool.h"
#include "ai/TraceAiProvider.h"
#include "core/ServiceRuntimeAccumulator.h"
#include "core/SystemRuntimeAccumulator.h"
#include "persistence/BufferedTraceRepository.h"
#include "persistence/TraceRepository.h"
#include "notification/INotifier.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>

namespace
{
    std::string toLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    int64_t NowSteadyMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    uint64_t NowSteadyNs()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::steady_clock::now().time_since_epoch())
                                         .count());
    }

    int64_t ResolveTraceEndTimeMs(const TraceRepository::TraceSummary& summary)
    {
        if (summary.end_time_ms.has_value())
        {
            return summary.end_time_ms.value();
        }
        return summary.start_time_ms + summary.duration_ms;
    }

    bool IsErrorSpanRecord(const TraceRepository::TraceSpanRecord& record)
    {
        return toLowerCopy(record.status) == "error";
    }

    size_t ResolveAlertTokenCount(const TraceRepository::TraceSummary& summary,
                                  const std::optional<TraceAiUsage>& usage)
    {
        // 告警里的 token_count 先优先吃 provider usage。既然模型已经告诉我们真实 total_tokens，
        // 那就不该继续用 Push 路径上的字符数近似值。只有 usage 缺失时，才回退到现有 summary.token_count。
        if (usage.has_value() && usage->total_tokens > 0)
        {
            return usage->total_tokens;
        }
        return summary.token_count;
    }

    SystemBackpressureStatus ToSystemBackpressureStatus(TraceSessionManager::OverloadState overload_state)
    {
        switch (overload_state)
        {
        case TraceSessionManager::OverloadState::Normal:
            return SystemBackpressureStatus::Normal;
        case TraceSessionManager::OverloadState::Overload:
            return SystemBackpressureStatus::Active;
        case TraceSessionManager::OverloadState::Critical:
            return SystemBackpressureStatus::Full;
        }
        return SystemBackpressureStatus::Normal;
    }

    PrimaryObservation BuildPrimaryObservation(const TraceRepository::TraceSummary& summary,
                                              const std::vector<TraceRepository::TraceSpanRecord>& span_records)
    {
        struct ServiceTempState
        {
            PrimaryServiceObservation service;
            std::unordered_map<std::string, size_t> operation_index;
        };

        PrimaryObservation observation;
        observation.trace_id = summary.trace_id;
        observation.trace_end_time_ms = ResolveTraceEndTimeMs(summary);
        observation.trace_risk_level = summary.risk_level;

        std::unordered_map<std::string, ServiceTempState> services;
        for (const auto& span_record : span_records)
        {
            if (!IsErrorSpanRecord(span_record) || span_record.service_name.empty())
            {
                continue;
            }

            ServiceTempState& temp_state = services[span_record.service_name];
            temp_state.service.service_name = span_record.service_name;
            temp_state.service.error_trace_hit = true;
            temp_state.service.error_span_count += 1;
            temp_state.service.error_span_duration_sum_ms += span_record.duration_ms;
            temp_state.service.latest_exception_time_ms =
                std::max(temp_state.service.latest_exception_time_ms,
                         span_record.start_time_ms + span_record.duration_ms);

            const std::string& operation_name = span_record.operation;
            auto operation_iter = temp_state.operation_index.find(operation_name);
            if (operation_iter == temp_state.operation_index.end())
            {
                PrimaryOperationObservation operation;
                operation.operation_name = operation_name;
                operation.error_span_count = 1;
                operation.error_span_duration_sum_ms = span_record.duration_ms;
                temp_state.service.operations.push_back(std::move(operation));
                temp_state.operation_index[operation_name] = temp_state.service.operations.size() - 1;
            }
            else
            {
                PrimaryOperationObservation& operation =
                    temp_state.service.operations[operation_iter->second];
                operation.error_span_count += 1;
                operation.error_span_duration_sum_ms += span_record.duration_ms;
            }
        }

        for (auto& entry : services)
        {
            observation.services.push_back(std::move(entry.second.service));
        }
        return observation;
    }

    AnalysisObservation BuildAnalysisObservation(const TraceRepository::TraceSummary& summary,
                                                 const std::vector<TraceRepository::TraceSpanRecord>& span_records,
                                                 const std::string& summary_text,
                                                 const std::string& risk_level)
    {
        AnalysisObservation observation;
        observation.trace_id = summary.trace_id;
        observation.trace_end_time_ms = ResolveTraceEndTimeMs(summary);
        observation.trace_risk_level = risk_level;
        observation.trace_summary = summary_text;

        std::unordered_map<std::string, AnalysisServiceSample> service_samples;
        for (const auto& span_record : span_records)
        {
            if (!IsErrorSpanRecord(span_record) || span_record.service_name.empty())
            {
                continue;
            }

            const int64_t sample_time_ms = span_record.start_time_ms + span_record.duration_ms;
            AnalysisServiceSample& sample = service_samples[span_record.service_name];
            const bool should_replace =
                sample.service_name.empty() ||
                span_record.duration_ms > sample.duration_ms ||
                (span_record.duration_ms == sample.duration_ms && sample_time_ms > sample.sample_time_ms);

            if (!should_replace)
            {
                continue;
            }

            sample.service_name = span_record.service_name;
            sample.representative_operation_name = span_record.operation;
            sample.sample_time_ms = sample_time_ms;
            sample.duration_ms = span_record.duration_ms;
            sample.sample_risk_level = risk_level;
        }

        for (auto& entry : service_samples)
        {
            observation.service_samples.push_back(std::move(entry.second));
        }
        return observation;
    }

}

TraceSession::TraceSession(size_t capacity)
    : capacity(capacity)
{
    spans.reserve(capacity);
}

TraceSessionManager::TraceSessionManager(ThreadPool *thread_pool,
                                         BufferedTraceRepository *buffered_trace_repo,
                                         TraceAiProvider *trace_ai,
                                         size_t capacity,
                                         size_t token_limit,
                                         INotifier *notifier,
                                         int64_t idle_timeout_ms,
                                         int64_t wheel_tick_ms,
                                         int64_t sealed_grace_window_ms,
                                         int64_t retry_base_delay_ms,
                                         size_t wheel_size,
                                         size_t buffered_span_hard_limit,
                                         size_t active_session_hard_limit,
                                         ServiceRuntimeAccumulator* service_runtime_accumulator,
                                         SystemRuntimeAccumulator* system_runtime_accumulator)
    : thread_pool_(thread_pool), buffered_trace_repo_(buffered_trace_repo), trace_ai_(trace_ai), notifier_(notifier), service_runtime_accumulator_(service_runtime_accumulator), system_runtime_accumulator_(system_runtime_accumulator), capacity_(capacity), token_limit_(token_limit), wheel_size_(wheel_size > 0 ? wheel_size : 512), idle_timeout_ms_(idle_timeout_ms > 0 ? idle_timeout_ms : 5000), wheel_tick_ms_(wheel_tick_ms > 0 ? wheel_tick_ms : 500), buffered_span_hard_limit_(buffered_span_hard_limit > 0 ? buffered_span_hard_limit : 4096), active_session_hard_limit_(active_session_hard_limit > 0 ? active_session_hard_limit : 1024)
{
    timeout_ticks_ = ComputeTimeoutTicks();
    // sealed/retry 这两档时间现在也跟着启动配置走，避免状态机里继续保留 1/2 tick 的硬编码。
    sealed_grace_ticks_ = ComputeDelayTicks(sealed_grace_window_ms);
    retry_base_delay_ticks_ = ComputeDelayTicks(retry_base_delay_ms);
    time_wheel_.resize(wheel_size_);
    // 先把 completed tombstone 的桶位也按同样的 wheel_size 建好。
    // 这样后面只要跟着 current_tick_ 同步推进，就能在同一套 tick 节奏里做过期回收。
    completed_trace_wheel_.resize(wheel_size_);
    buffered_span_watermark_ = BuildWatermark(buffered_span_hard_limit_);
    active_session_watermark_ = BuildWatermark(active_session_hard_limit_);
    const size_t pending_task_hard_limit =
        thread_pool_ ? std::max<size_t>(1, thread_pool_->maxQueueSize()) : 1;
    pending_task_watermark_ = BuildWatermark(pending_task_hard_limit);
    // dispatch queue 先按 worker queue 的一半建硬上限，目的是让“主线程摘 session”和“AI worker 真正吃任务”
    // 之间至少隔一层更窄的静态闸门；后面如果压测显示过窄，再按结果调整。
    dispatch_queue_hard_limit_ = std::max<size_t>(1, pending_task_hard_limit / 2);
    dispatch_queue_watermark_ = BuildWatermark(dispatch_queue_hard_limit_);
    dispatch_thread_ = std::thread(&TraceSessionManager::DispatchLoop, this);
}

TraceSessionManager::~TraceSessionManager()
{
    StopDispatchThread();
    const RuntimeStatsSnapshot stats = SnapshotRuntimeStats();
    if (stats.dispatch_count == 0 && stats.worker_begin_count == 0 && stats.analysis_enqueue_calls == 0)
    {
        return;
    }
    std::clog << "[TraceRuntimeStats] " << DescribeRuntimeStats() << std::endl;
}

TraceSessionManager::Watermark TraceSessionManager::BuildWatermark(size_t hard_limit)
{
    const size_t safe_hard_limit = std::max<size_t>(hard_limit, 1);
    Watermark mark;
    mark.low = std::max<size_t>(1, safe_hard_limit * 55 / 100);
    mark.high = std::max(mark.low, std::max<size_t>(1, safe_hard_limit * 75 / 100));
    mark.critical = std::max(mark.high, std::max<size_t>(1, safe_hard_limit * 90 / 100));
    mark.critical = std::min(mark.critical, safe_hard_limit);
    return mark;
}

uint64_t TraceSessionManager::ComputeDelayTicks(int64_t delay_ms) const
{
    if (delay_ms <= 0 || wheel_tick_ms_ <= 0)
    {
        return 1;
    }
    return static_cast<uint64_t>((delay_ms + wheel_tick_ms_ - 1) / wheel_tick_ms_);
}

uint64_t TraceSessionManager::ComputeSealDelayTicks(TraceSession::SealReason reason) const
{
    (void)reason;
    return std::max<uint64_t>(1, sealed_grace_ticks_);
}

uint64_t TraceSessionManager::ComputeRetryDelayTicks(size_t retry_count) const
{
    const uint64_t base_ticks = std::max<uint64_t>(1, retry_base_delay_ticks_);
    if (retry_count == 0)
    {
        return base_ticks;
    }
    // 继续保留最多 16 倍 base delay 的上限，避免下游长期故障时把单条 trace 的重试时间拖得过长。
    const uint64_t max_retry_delay_ticks = std::max<uint64_t>(base_ticks, base_ticks * 16);
    const size_t shift = std::min<size_t>(retry_count - 1, 4);
    const uint64_t scaled_delay_ticks = base_ticks << shift;
    return std::min<uint64_t>(scaled_delay_ticks, max_retry_delay_ticks);
}

void TraceSessionManager::DispatchLoop()
{
    while (true)
    {
        DispatchJob job;
        {
            std::unique_lock<std::mutex> lock(dispatch_queue_mutex_);
            dispatch_queue_cv_.wait(lock, [this]
                                    { return dispatch_stopping_ || !dispatch_queue_.empty(); });
            if (dispatch_stopping_ && dispatch_queue_.empty())
            {
                return;
            }
            job = std::move(dispatch_queue_.front());
            dispatch_queue_.pop();
        }
        ProcessDispatchJob(std::move(job));
    }
}

void TraceSessionManager::StopDispatchThread()
{
    {
        std::lock_guard<std::mutex> lock(dispatch_queue_mutex_);
        dispatch_stopping_ = true;
    }
    dispatch_queue_cv_.notify_all();
    if (dispatch_thread_.joinable())
    {
        dispatch_thread_.join();
    }
}

size_t TraceSessionManager::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

TraceSessionManager::RuntimeStatsSnapshot TraceSessionManager::SnapshotRuntimeStats() const
{
    RuntimeStatsSnapshot stats;
    stats.dispatch_count = dispatch_count_.load(std::memory_order_relaxed);
    stats.submit_ok_count = submit_ok_count_.load(std::memory_order_relaxed);
    stats.submit_fail_count = submit_fail_count_.load(std::memory_order_relaxed);
    stats.worker_begin_count = worker_begin_count_.load(std::memory_order_relaxed);
    stats.worker_done_count = worker_done_count_.load(std::memory_order_relaxed);
    stats.ai_calls = ai_calls_.load(std::memory_order_relaxed);
    stats.ai_total_ns = ai_total_ns_.load(std::memory_order_relaxed);
    stats.analysis_enqueue_calls = analysis_enqueue_calls_.load(std::memory_order_relaxed);
    stats.analysis_enqueue_total_ns = analysis_enqueue_total_ns_.load(std::memory_order_relaxed);
    return stats;
}

std::string TraceSessionManager::DescribeRuntimeStats() const
{
    const RuntimeStatsSnapshot stats = SnapshotRuntimeStats();
    std::ostringstream oss;
    oss << "dispatch_count=" << stats.dispatch_count
        << ", submit_ok_count=" << stats.submit_ok_count
        << ", submit_fail_count=" << stats.submit_fail_count
        << ", worker_begin_count=" << stats.worker_begin_count
        << ", worker_done_count=" << stats.worker_done_count
        << ", ai_calls=" << stats.ai_calls
        << ", ai_total_ns=" << stats.ai_total_ns
        << ", ai_avg_ms=" << (stats.ai_calls > 0 ? (static_cast<double>(stats.ai_total_ns) / stats.ai_calls / 1'000'000.0) : 0.0)
        // 这里现在明确写成 enqueue，而不是 save。
        // 既然真实 SQLite batch flush 已经挪到 BufferedTraceRepository 后台线程，
        // 那 manager 这边量到的只是在 worker 内把 analysis 丢进缓冲区的墙钟时间。
        << ", analysis_enqueue_calls=" << stats.analysis_enqueue_calls
        << ", analysis_enqueue_total_ns=" << stats.analysis_enqueue_total_ns
        << ", analysis_enqueue_avg_ms="
        << (stats.analysis_enqueue_calls > 0 ? (static_cast<double>(stats.analysis_enqueue_total_ns) / stats.analysis_enqueue_calls / 1'000'000.0) : 0.0);
    return oss.str();
}

TraceSessionManager::PushResult TraceSessionManager::Push(const SpanEvent &span)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return PushLocked(span, NowSteadyMs());
}

TraceSessionManager::PushResult TraceSessionManager::PushLocked(const SpanEvent &span, int64_t now_ms)
{
    // 线程池是 trace 异步分发链路的硬依赖；缺失时直接拒绝，避免后续误报 accepted 后又静默丢数据。
    // TraceSessionManager 现在只认双缓冲写入器。既然主数据和分析结果都要走分段 append，
    // 那 thread_pool 和 buffered_trace_repo 缺一个都不能算链路可用。
    if (!thread_pool_ || !buffered_trace_repo_)
    {
        return PushResult::RejectedUnavailable;
    }
    auto iter = index_by_trace_.find(span.trace_key);
    const bool trace_exists = (iter != index_by_trace_.end());
    if (!trace_exists)
    {
        auto inflight_iter = dispatching_inflight_.find(span.trace_key);
        if (inflight_iter != dispatching_inflight_.end())
        {
            // dispatching inflight 代表这条 trace 已离开 manager、但还没进入 tombstone。
            // 第一步先按“延后吸收”处理，至少避免晚到 span 直接把旧 trace 复活成新 session。
            return PushResult::AcceptedDeferred;
        }
    }
    if (!trace_exists && IsCompletedTombstoneAliveLocked(span.trace_key))
    {
        // 这条 trace 已经完成并处于短暂 TIME_WAIT。
        // 既然现在来的是晚到 span，那么直接幂等吸收即可，不能再把旧 trace 复活成新 session。
        return PushResult::Accepted;
    }
    RefreshOverloadState();
    if (ShouldRejectIncomingTrace(trace_exists))
    {
        return PushResult::RejectedOverload;
    }

    if (iter == index_by_trace_.end())
    {
        auto session = std::make_unique<TraceSession>(capacity_);
        session->trace_key = span.trace_key;
        session->created_at_ms = now_ms;
        session->last_update_ms = now_ms;
        session->session_epoch = ++session_epoch_seq_;
        sessions_.push_back(std::move(session));
        index_by_trace_[span.trace_key] = sessions_.size() - 1;
        active_sessions_ += 1;
        iter = index_by_trace_.find(span.trace_key);
    }

    TraceSession &session = *sessions_[iter->second];
    if (session.lifecycle_state == TraceSession::LifecycleState::ReadyRetryLater)
    {
        // ready retry 会话已经完成主数据收口；这时再并入新 span，会把内存里的 trace 和已入缓冲的 primary 语义撕裂。
        return PushResult::AcceptedDeferred;
    }
    if (!session.span_ids.insert(span.span_id).second)
    {
        if (session.lifecycle_state == TraceSession::LifecycleState::Collecting)
        {
            // 发现重复 span_id 时先记录首个重复值，便于后续分发时标注异常来源。
            if (!session.duplicate_span_id.has_value())
            {
                session.duplicate_span_id = span.span_id;
            }
            SealSessionLocked(session, TraceSession::SealReason::DuplicateSpan);
            RefreshOverloadState();
            return PushResult::Accepted;
        }
        else
        {
            return PushResult::Accepted;
        }
    }

    // 先按到达顺序追加，后续聚合阶段再按 parent_id 重建结构。
    const bool already_sealed = (session.lifecycle_state == TraceSession::LifecycleState::Sealed);
    session.spans.push_back(span);
    total_buffered_spans_ += 1;
    session.token_count += token_estimator_.Estimate(span);
    session.last_update_ms = now_ms;
    if (!already_sealed)
    {
        session.lifecycle_state = TraceSession::LifecycleState::Collecting;
        session.retry_count = 0;
        session.next_retry_tick = 0;
    }

    if (already_sealed)
    {
        // sealed 会话允许吸收短窗口内的 late span，但 deadline 固定，不续命。
        RefreshOverloadState();
        return PushResult::Accepted;
    }

    if (span.trace_end.has_value() && span.trace_end.value())
    {
        SealSessionLocked(session, TraceSession::SealReason::TraceEnd);
        RefreshOverloadState();
        return PushResult::Accepted;
    }

    if (token_limit_ > 0 && session.token_count >= token_limit_)
    {
        SealSessionLocked(session, TraceSession::SealReason::TokenLimit);
        RefreshOverloadState();
        return PushResult::Accepted;
    }

    if (session.capacity > 0 && session.spans.size() >= session.capacity)
    {
        // 达到容量上限时先封口，再给一个最短 grace tick 吸收少量乱序 span。
        SealSessionLocked(session, TraceSession::SealReason::Capacity);
        RefreshOverloadState();
        return PushResult::Accepted;
    }

    // collecting 会话继续按“等待后续 span”语义重排超时计划。
    // 这里采用懒删除策略：不回删旧槽，只给会话 version+1 并插入新节点。
    ScheduleSessionNode(session);
    RefreshOverloadState();

    // 当前仅做本地写入与分发占位，先返回成功；后续接入背压分支后再细分 AcceptedDeferred/RejectedOverload。
    return PushResult::Accepted;
}

std::unique_ptr<TraceSession> TraceSessionManager::DetachSessionLocked(size_t trace_key, size_t *span_count)
{
    auto iter = index_by_trace_.find(trace_key);
    if (iter == index_by_trace_.end())
    {
        if (span_count)
        {
            *span_count = 0;
        }
        return nullptr;
    }

    const size_t index = iter->second;
    std::unique_ptr<TraceSession> session = std::move(sessions_[index]);
    const size_t local_span_count = session ? session->spans.size() : 0;
    index_by_trace_.erase(iter);

    if (index < sessions_.size() - 1)
    {
        sessions_[index] = std::move(sessions_.back());
        sessions_.pop_back();
        index_by_trace_[sessions_[index]->trace_key] = index;
    }
    else
    {
        sessions_.pop_back();
    }

    if (active_sessions_ > 0)
    {
        active_sessions_ -= 1;
    }
    if (total_buffered_spans_ >= local_span_count)
    {
        total_buffered_spans_ -= local_span_count;
    }
    else
    {
        total_buffered_spans_ = 0;
    }
    RefreshOverloadState();

    if (span_count)
    {
        *span_count = local_span_count;
    }
    return session;
}

void TraceSessionManager::RestoreSessionLocked(std::unique_ptr<TraceSession> session, size_t span_count)
{
    if (!session)
    {
        return;
    }
    session->lifecycle_state = TraceSession::LifecycleState::ReadyRetryLater;
    session->sealed_deadline_tick = 0;
    session->last_update_ms = NowSteadyMs();
    session->retry_count += 1;
    session->next_retry_tick =
        current_tick_ + ComputeRetryDelayTicks(session->retry_count);
    const size_t trace_key = session->trace_key;
    sessions_.push_back(std::move(session));
    index_by_trace_[trace_key] = sessions_.size() - 1;
    active_sessions_ += 1;
    total_buffered_spans_ += span_count;
    ScheduleSessionNode(*sessions_.back());
    RefreshOverloadState();
}

bool TraceSessionManager::EnqueueDispatchJobLocked(DispatchJob *job)
{
    if (!job || !job->session)
    {
        return true;
    }
    {
        std::lock_guard<std::mutex> queue_lock(dispatch_queue_mutex_);
        if (dispatch_stopping_ || dispatch_queue_.size() >= dispatch_queue_hard_limit_)
        {
            return false;
        }
        dispatch_queue_.push(std::move(*job));
    }
    dispatch_queue_cv_.notify_one();
    return true;
}

void TraceSessionManager::SweepExpiredSessions(int64_t now_ms,
                                               int64_t idle_timeout_ms,
                                               size_t max_dispatch_per_tick)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (sessions_.empty() && completed_trace_expire_tick_.empty())
    {
        return;
    }
    if (idle_timeout_ms > 0 && idle_timeout_ms != idle_timeout_ms_)
    {
        // 运行中调整超时策略时重建时间轮，避免继续使用旧超时计划。
        idle_timeout_ms_ = idle_timeout_ms;
        timeout_ticks_ = ComputeTimeoutTicks();
        RebuildTimeWheel();
    }

    if (last_tick_now_ms_ == 0)
    {
        last_tick_now_ms_ = now_ms;
    }
    int64_t elapsed_ms = now_ms - last_tick_now_ms_;
    uint64_t advance_ticks = 1;
    if (elapsed_ms > 0 && wheel_tick_ms_ > 0)
    {
        advance_ticks = static_cast<uint64_t>(elapsed_ms / wheel_tick_ms_);
        if (advance_ticks == 0)
        {
            advance_ticks = 1;
        }
    }
    // 防止长时间阻塞后一次性补 tick 过多导致本轮回调卡死。
    if (advance_ticks > wheel_size_)
    {
        advance_ticks = wheel_size_;
    }
    last_tick_now_ms_ = now_ms;

    std::vector<size_t> expired_trace_keys;
    expired_trace_keys.reserve(max_dispatch_per_tick > 0 ? max_dispatch_per_tick : sessions_.size());
    std::unordered_set<size_t> scheduled_once;
    scheduled_once.reserve(expired_trace_keys.capacity() > 0 ? expired_trace_keys.capacity() : 8);

    for (uint64_t step = 0; step < advance_ticks; ++step)
    {
        ++current_tick_;
        const size_t slot = static_cast<size_t>(current_tick_ % wheel_size_);
        SweepCompletedTombstonesLocked(slot);
        std::vector<TimeWheelNode> bucket = std::move(time_wheel_[slot]);
        time_wheel_[slot].clear();

        for (auto &node : bucket)
        {
            auto idx_iter = index_by_trace_.find(node.trace_key);
            if (idx_iter == index_by_trace_.end())
            {
                // 会话已分发并从内存移除，旧节点自然失效。
                continue;
            }
            TraceSession &session = *sessions_[idx_iter->second];
            if (session.session_epoch != node.epoch || session.timer_version != node.version)
            {
                // 非当前版本节点（旧计划）直接丢弃。
                continue;
            }
            if (node.expire_tick > current_tick_)
            {
                // 补 tick 场景下，如果还没到期，放回目标槽等待后续 tick。
                time_wheel_[node.expire_tick % wheel_size_].push_back(node);
                continue;
            }
            if (session.lifecycle_state == TraceSession::LifecycleState::ReadyRetryLater &&
                current_tick_ < session.next_retry_tick)
            {
                // retry 会话只有到达 next_retry_tick 才允许重投，避免固定频率打桩。
                time_wheel_[session.next_retry_tick % wheel_size_].push_back(node);
                continue;
            }
            if (session.lifecycle_state == TraceSession::LifecycleState::Sealed &&
                current_tick_ < session.sealed_deadline_tick)
            {
                // sealed 会话允许并入 late span，但 deadline 固定，不会因为后续 push 被重新向后推。
                time_wheel_[session.sealed_deadline_tick % wheel_size_].push_back(node);
                continue;
            }
            if (max_dispatch_per_tick > 0 && expired_trace_keys.size() >= max_dispatch_per_tick)
            {
                // 本轮达到上限时，将当前有效节点顺延一 tick，避免被直接丢失。
                node.expire_tick = current_tick_ + 1;
                time_wheel_[node.expire_tick % wheel_size_].push_back(node);
                continue;
            }
            if (scheduled_once.insert(node.trace_key).second)
            {
                expired_trace_keys.push_back(node.trace_key);
            }
        }
    }

    for (size_t trace_key : expired_trace_keys)
    {
        dispatch_count_.fetch_add(1, std::memory_order_relaxed);

        size_t span_count = 0;
        std::unique_ptr<TraceSession> session = DetachSessionLocked(trace_key, &span_count);
        if (!session)
        {
            continue;
        }

        dispatching_inflight_[trace_key] = DispatchingInflightState{session->session_epoch};
        DispatchJob job;
        job.session = std::move(session);
        if (EnqueueDispatchJobLocked(&job))
        {
            continue;
        }

        submit_fail_count_.fetch_add(1, std::memory_order_relaxed);
        dispatching_inflight_.erase(trace_key);
        RestoreSessionLocked(std::move(job.session), span_count);
    }
}

uint64_t TraceSessionManager::ComputeTimeoutTicks() const
{
    if (idle_timeout_ms_ <= 0 || wheel_tick_ms_ <= 0)
    {
        return 1;
    }
    return static_cast<uint64_t>((idle_timeout_ms_ + wheel_tick_ms_ - 1) / wheel_tick_ms_);
}

void TraceSessionManager::AddCompletedTombstoneLocked(size_t trace_key)
{
    const uint64_t expire_tick = current_tick_ + completed_trace_tombstone_ticks_;
    completed_trace_expire_tick_[trace_key] = expire_tick;
    completed_trace_wheel_[expire_tick % wheel_size_].push_back(trace_key);
}

bool TraceSessionManager::IsCompletedTombstoneAliveLocked(size_t trace_key)
{
    auto iter = completed_trace_expire_tick_.find(trace_key);
    if (iter == completed_trace_expire_tick_.end())
    {
        return false;
    }
    if (iter->second > current_tick_)
    {
        return true;
    }
    completed_trace_expire_tick_.erase(iter);
    return false;
}

void TraceSessionManager::SweepCompletedTombstonesLocked(size_t slot)
{
    std::vector<size_t> bucket = std::move(completed_trace_wheel_[slot]);
    completed_trace_wheel_[slot].clear();

    for (size_t trace_key : bucket)
    {
        auto iter = completed_trace_expire_tick_.find(trace_key);
        if (iter == completed_trace_expire_tick_.end())
        {
            // 已被新 tombstone 覆盖或已过期删除，旧 wheel 节点直接丢弃。
            continue;
        }
        if (iter->second > current_tick_)
        {
            // 由于是取模回环，同一个槽里可能混着未来很多轮才真正到期的 tombstone。
            // 这里必须按真实 expire_tick 重新挂回去，不能因为扫到当前槽就提前遗忘。
            completed_trace_wheel_[iter->second % wheel_size_].push_back(trace_key);
            continue;
        }
        completed_trace_expire_tick_.erase(iter);
    }
}

void TraceSessionManager::ScheduleTimeoutNode(TraceSession &session)
{
    session.timer_version += 1;
    const uint64_t expire_tick = current_tick_ + timeout_ticks_;
    const size_t slot = static_cast<size_t>(expire_tick % wheel_size_);
    TimeWheelNode node;
    node.trace_key = session.trace_key;
    node.version = session.timer_version;
    node.epoch = session.session_epoch;
    node.expire_tick = expire_tick;
    time_wheel_[slot].push_back(std::move(node));
}

void TraceSessionManager::ScheduleSealedNode(TraceSession &session)
{
    session.timer_version += 1;
    const uint64_t expire_tick =
        session.sealed_deadline_tick > current_tick_ ? session.sealed_deadline_tick : current_tick_ + 1;
    const size_t slot = static_cast<size_t>(expire_tick % wheel_size_);
    TimeWheelNode node;
    node.trace_key = session.trace_key;
    node.version = session.timer_version;
    node.epoch = session.session_epoch;
    node.expire_tick = expire_tick;
    time_wheel_[slot].push_back(std::move(node));
}

void TraceSessionManager::ScheduleRetryNode(TraceSession &session)
{
    session.timer_version += 1;
    const uint64_t retry_tick =
        session.next_retry_tick > current_tick_ ? session.next_retry_tick : current_tick_ + 1;
    const size_t slot = static_cast<size_t>(retry_tick % wheel_size_);
    TimeWheelNode node;
    node.trace_key = session.trace_key;
    node.version = session.timer_version;
    node.epoch = session.session_epoch;
    node.expire_tick = retry_tick;
    time_wheel_[slot].push_back(std::move(node));
}

void TraceSessionManager::ScheduleSessionNode(TraceSession &session)
{
    if (session.lifecycle_state == TraceSession::LifecycleState::Sealed)
    {
        ScheduleSealedNode(session);
        return;
    }
    if (session.lifecycle_state == TraceSession::LifecycleState::ReadyRetryLater)
    {
        ScheduleRetryNode(session);
        return;
    }
    ScheduleTimeoutNode(session);
}

void TraceSessionManager::SealSessionLocked(TraceSession &session, TraceSession::SealReason reason)
{
    session.lifecycle_state = TraceSession::LifecycleState::Sealed;
    session.seal_reason = reason;
    session.sealed_deadline_tick = current_tick_ + ComputeSealDelayTicks(reason);
    ScheduleSessionNode(session);
}

void TraceSessionManager::RebuildTimeWheel()
{
    for (auto &bucket : time_wheel_)
    {
        bucket.clear();
    }
    for (auto &session_ptr : sessions_)
    {
        if (!session_ptr)
        {
            continue;
        }
        // collecting / sealed / retry_later 三种时间语义不同，重建时必须按当前生命周期分别重排。
        ScheduleSessionNode(*session_ptr);
    }
}

bool TraceSessionManager::Dispatch(size_t trace_key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return DispatchLocked(trace_key);
}

bool TraceSessionManager::DispatchLocked(size_t trace_key)
{
    // 双保险：正常路径已在 Push 入口拒绝无线程池情况；这里继续防御，避免未来别的路径直接调 Dispatch 时删掉 session。
    if (!thread_pool_)
    {
        return false;
    }
    auto iter = index_by_trace_.find(trace_key);
    if (iter == index_by_trace_.end())
    {
        return true;
    }
    dispatch_count_.fetch_add(1, std::memory_order_relaxed);

    const size_t index = iter->second;
    std::unique_ptr<TraceSession> session = std::move(sessions_[index]);
    const size_t span_count = session ? session->spans.size() : 0;
    index_by_trace_.erase(iter);

    if (index < sessions_.size() - 1)
    {
        // swap+pop_back：用最后一个元素覆盖被移除位置，保持容器紧凑并避免线性搬移。
        sessions_[index] = std::move(sessions_.back());
        sessions_.pop_back();
        index_by_trace_[sessions_[index]->trace_key] = index;
    }
    else
    {
        sessions_.pop_back();
    }
    if (active_sessions_ > 0)
    {
        active_sessions_ -= 1;
    }
    if (total_buffered_spans_ >= span_count)
    {
        total_buffered_spans_ -= span_count;
    }
    else
    {
        total_buffered_spans_ = 0;
    }
    RefreshOverloadState();

    auto rollback_session = [this, trace_key, span_count](std::unique_ptr<TraceSession> restored_session)
    {
        restored_session->lifecycle_state = TraceSession::LifecycleState::ReadyRetryLater;
        restored_session->sealed_deadline_tick = 0;
        restored_session->last_update_ms = NowSteadyMs();
        restored_session->retry_count += 1;
        restored_session->next_retry_tick =
            current_tick_ + ComputeRetryDelayTicks(restored_session->retry_count);
        sessions_.push_back(std::move(restored_session));
        index_by_trace_[trace_key] = sessions_.size() - 1;
        active_sessions_ += 1;
        total_buffered_spans_ += span_count;
        ScheduleSessionNode(*sessions_.back());
        RefreshOverloadState();
    };

    std::string trace_payload;
    TraceRepository::TraceSummary summary;
    std::vector<TraceRepository::TraceSpanRecord> span_records;
    if (session)
    {
        TraceIndex index = BuildTraceIndex(*session);
        std::vector<const SpanEvent *> order;
        trace_payload = SerializeTrace(index, &order);
        summary = BuildTraceSummary(*session, order);
        span_records = BuildSpanRecords(order);
    }

    if (buffered_trace_repo_ && session && !session->primary_enqueued)
    {
        BufferedTraceRepository::TracePrimaryWrite primary_write;
        primary_write.summary = summary;
        primary_write.spans = span_records;
        if (!buffered_trace_repo_->AppendPrimary(std::move(primary_write)))
        {
            rollback_session(std::move(session));
            return false;
        }
        session->primary_enqueued = true;
    }

    auto session_holder = std::make_shared<std::unique_ptr<TraceSession>>(std::move(session));
    TraceSessionManager *manager = this;
    BufferedTraceRepository *buffered_trace_repo = buffered_trace_repo_;
    TraceAiProvider *trace_ai = trace_ai_;
    INotifier *notifier = notifier_;
    SystemRuntimeAccumulator* system_runtime_accumulator = system_runtime_accumulator_;
    const uint64_t worker_enqueue_ns = NowSteadyNs();
    if (!thread_pool_->submit([manager, buffered_trace_repo, trace_ai, notifier, system_runtime_accumulator, worker_enqueue_ns, session_holder, trace_payload = std::move(trace_payload), summary = std::move(summary), span_records = std::move(span_records)]() mutable
                              {
        if (!manager || !session_holder || !(*session_holder)) {
            return;
        }
        manager->worker_begin_count_.fetch_add(1, std::memory_order_relaxed);
        const uint64_t worker_begin_ns = NowSteadyNs();
        const uint64_t queue_wait_ms =
            worker_begin_ns >= worker_enqueue_ns ? (worker_begin_ns - worker_enqueue_ns) / 1000000ULL : 0;

        TraceRepository::TraceAnalysisRecord analysis_record;
        TraceRepository::TraceAnalysisRecord* analysis_ptr = nullptr;
        size_t alert_token_count = summary.token_count;
        std::optional<TraceAiUsage> completed_usage;
        if (trace_ai) {
            if (system_runtime_accumulator) {
                // AI 调用总数表达的是“真正开始发起模型调用”的次数。
                // 所以 started 记在 worker 真开始调 AnalyzeTrace 之前，而不是 trace 刚被系统接住时。
                system_runtime_accumulator->RecordAiCallStarted();
            }
            manager->ai_calls_.fetch_add(1, std::memory_order_relaxed);
            const uint64_t ai_begin_ns = NowSteadyNs();
            uint64_t inference_latency_ms = 0;
            try {
                TraceAiResponse ai_response = trace_ai->AnalyzeTrace(trace_payload);
                analysis_record = manager->BuildAnalysisRecord(summary.trace_id, ai_response.analysis);
                analysis_ptr = &analysis_record;
                alert_token_count = ResolveAlertTokenCount(summary, ai_response.usage);
                completed_usage = ai_response.usage;
            } catch (const std::exception& e) {
                analysis_record.trace_id = summary.trace_id;
                analysis_record.risk_level = "unknown";
                analysis_record.summary = "AI_ANALYSIS_FAILED";
                analysis_record.root_cause = e.what();
                analysis_record.solution = "请检查 AI 代理与模型服务状态，稍后重试该 trace 的分析。";
                analysis_record.confidence = 0.0;
                analysis_ptr = &analysis_record;
            } catch (...) {
                analysis_record.trace_id = summary.trace_id;
                analysis_record.risk_level = "unknown";
                analysis_record.summary = "AI_ANALYSIS_FAILED";
                analysis_record.root_cause = "Unknown non-std exception";
                analysis_record.solution = "请检查 AI 代理与模型服务状态，稍后重试该 trace 的分析。";
                analysis_record.confidence = 0.0;
                analysis_ptr = &analysis_record;
            }
            const uint64_t ai_end_ns = NowSteadyNs();
            manager->ai_total_ns_.fetch_add(ai_end_ns - ai_begin_ns, std::memory_order_relaxed);
            inference_latency_ms = ai_end_ns >= ai_begin_ns ? (ai_end_ns - ai_begin_ns) / 1000000ULL : 0;
            if (system_runtime_accumulator) {
                // queue_wait 和 inference_latency 都属于同一次 AI 调用的收尾结果。
                // 这里在调用结束后一次性提交，避免把两张延迟卡拆成两个成熟时机不同的半成品。
                system_runtime_accumulator->RecordAiCallCompleted(queue_wait_ms, inference_latency_ms, completed_usage);
            }
        }

        manager->analysis_enqueue_calls_.fetch_add(1, std::memory_order_relaxed);
        const uint64_t enqueue_begin_ns = NowSteadyNs();
        bool saved = true;
        if (!buffered_trace_repo) {
            saved = false;
        } else {
            BufferedTraceRepository::TraceAnalysisWrite analysis_write;
            if (analysis_ptr) {
                analysis_write.analysis = *analysis_ptr;
            }
            saved = buffered_trace_repo->AppendAnalysis(std::move(analysis_write));
        }
        manager->analysis_enqueue_total_ns_.fetch_add(NowSteadyNs() - enqueue_begin_ns, std::memory_order_relaxed);
        manager->worker_done_count_.fetch_add(1, std::memory_order_relaxed);
        if (!saved || !notifier || !analysis_ptr) {
            return;
        }

        const std::string risk_level = toLowerCopy(analysis_ptr->risk_level);
        if (risk_level != "critical") {
            return;
        }

        TraceAlertEvent event;
        event.trace_id = summary.trace_id;
        event.service_name = summary.service_name;
        event.start_time_ms = summary.start_time_ms;
        event.duration_ms = summary.duration_ms;
        event.span_count = summary.span_count;
        event.token_count = alert_token_count;
        event.risk_level = analysis_ptr->risk_level;
        event.summary = analysis_ptr->summary;
        event.root_cause = analysis_ptr->root_cause;
        event.solution = analysis_ptr->solution;
        event.confidence = analysis_ptr->confidence;
        notifier->notifyTraceAlert(event); }))
    {
        submit_fail_count_.fetch_add(1, std::memory_order_relaxed);
        std::unique_ptr<TraceSession> restored_session = std::move(*session_holder);
        rollback_session(std::move(restored_session));
        return false;
    }
    AddCompletedTombstoneLocked(trace_key);
    submit_ok_count_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void TraceSessionManager::ProcessDispatchJob(DispatchJob job)
{
    std::unique_ptr<TraceSession> session = std::move(job.session);
    if (!session)
    {
        return;
    }

    const size_t trace_key = session->trace_key;
    const uint64_t session_epoch = session->session_epoch;
    const size_t span_count = session->spans.size();

    std::vector<TraceRepository::TraceSpanRecord> span_records;
    const std::string *trace_payload_ptr = nullptr;
    const TraceRepository::TraceSummary *summary_ptr = nullptr;

    // 先吃 session 自己已经准备好的缓存。
    // 既然 submit 失败回滚时会把同一个 session 放回 manager，那么下次 retry 再进来时，
    // 应该优先复用 prepared 结果，而不是上来就把整条 trace 再建树、再算一遍。
    if (session->prepared_trace_payload.has_value())
    {
        trace_payload_ptr = &session->prepared_trace_payload.value();
    }
    if (session->prepared_summary.has_value())
    {
        summary_ptr = &session->prepared_summary.value();
    }

    const bool need_payload = (trace_payload_ptr == nullptr);
    const bool need_summary = (summary_ptr == nullptr);
    const bool need_primary = !session->primary_enqueued;

    // 下面把“缺 payload / 缺 summary / 缺 primary”拆开处理。
    // 这样每个 if 都只表达一种缺口，而不是把三种意图揉成一个大 if 再在里面来回跳。
    std::optional<TraceIndex> trace_index;
    std::vector<const SpanEvent *> order;
    bool order_ready = false;

    auto ensure_trace_index = [&]() -> const TraceIndex &
    {
        if (!trace_index.has_value())
        {
            trace_index = BuildTraceIndex(*session);
        }
        return trace_index.value();
    };

    auto ensure_order = [&]() -> const std::vector<const SpanEvent *> &
    {
        if (order_ready)
        {
            return order;
        }

        // 当前 DFS 顺序仍然依赖 SerializeTrace 的副作用产出。
        // 所以只要 summary / primary 还要用 order，就得走一次 SerializeTrace；
        // 但如果 payload 已经准备好了，这里只把序列化结果当“取 order 的代价”丢掉，不再额外拷贝回局部变量。
        if (trace_payload_ptr == nullptr)
        {
            session->prepared_trace_payload = SerializeTrace(ensure_trace_index(), &order);
            trace_payload_ptr = &session->prepared_trace_payload.value();
        }
        else
        {
            (void)SerializeTrace(ensure_trace_index(), &order);
        }
        order_ready = true;
        return order;
    };

    if (need_payload)
    {
        if (need_summary || need_primary)
        {
            // 既然后面反正要 order，就直接在取 order 时把 payload 一并补齐，避免同一轮重复序列化两次。
            (void)ensure_order();
        }
        else
        {
            session->prepared_trace_payload = SerializeTrace(ensure_trace_index(), nullptr);
            trace_payload_ptr = &session->prepared_trace_payload.value();
        }
    }

    if (need_summary)
    {
        const std::vector<const SpanEvent *> &ready_order = ensure_order();
        session->prepared_summary = BuildTraceSummary(*session, ready_order);
        summary_ptr = &session->prepared_summary.value();
    }

    if (need_primary)
    {
        const std::vector<const SpanEvent *> &ready_order = ensure_order();
        span_records = BuildSpanRecords(ready_order);
    }

    // 服务监控 observation 需要一份“这条 trace 的错误 span 平铺视图”。
    // 第一刀先直接复用 span_records；如果当前是 retry 路径且主数据已入缓冲，就补建一份临时 records，
    // 这样既不重复写主数据，也不用让服务监控再去理解 TraceSession 的内部结构。
    std::vector<TraceRepository::TraceSpanRecord> observation_span_records;
    const std::vector<TraceRepository::TraceSpanRecord>* observation_span_records_ptr = nullptr;
    if (service_runtime_accumulator_)
    {
        if (!span_records.empty())
        {
            observation_span_records_ptr = &span_records;
        }
        else
        {
            const std::vector<const SpanEvent *>& ready_order = ensure_order();
            observation_span_records = BuildSpanRecords(ready_order);
            observation_span_records_ptr = &observation_span_records;
        }
    }

    std::optional<PrimaryObservation> primary_observation;
    if (service_runtime_accumulator_ && summary_ptr && observation_span_records_ptr)
    {
        primary_observation = BuildPrimaryObservation(*summary_ptr, *observation_span_records_ptr);
    }
    std::shared_ptr<std::vector<TraceRepository::TraceSpanRecord>> analysis_observation_span_records;
    if (service_runtime_accumulator_ && observation_span_records_ptr)
    {
        // worker 线程会晚于当前函数返回才真正执行，所以这里必须把 observation 用到的 span 视图单独拷出去，
        // 不能把局部 vector 的地址直接借给异步任务，否则 ProcessDispatchJob 退栈后就是悬空指针。
        analysis_observation_span_records =
            std::make_shared<std::vector<TraceRepository::TraceSpanRecord>>(*observation_span_records_ptr);
    }

    // 上面三个缺口都补完后，worker 与 append 路径只读 session 内部 prepared 数据即可。
    // 这样 trace_payload / summary 在本线程里就不用再多拷一份中间局部副本。
    if (!trace_payload_ptr && session->prepared_trace_payload.has_value())
    {
        trace_payload_ptr = &session->prepared_trace_payload.value();
    }
    if (!summary_ptr && session->prepared_summary.has_value())
    {
        summary_ptr = &session->prepared_summary.value();
    }

    // primary 指的是“主数据首段”：
    // 1) trace_summary：给列表页、详情页和后续 analysis 结果做主键骨架；
    // 2) span_records：给瀑布图/调用链详情提供原始 span 明细。
    // 既然这两份数据是后续 AI analysis、告警和查询的基础，那么它们必须先 append 到 BufferedTraceRepository，
    // 不能等 worker 线程里的 AI 分析跑完再写。否则一旦 AI 线程成功、主数据却还没进缓冲区，
    // 就会出现“analysis 已有结果，但 trace_summary / trace_spans 还没有”的前后错位。
    //
    // 同时这里还要守住“只 append 一次”的约束：
    // - 第一次 dispatch 时，primary_enqueued=false，所以会真正写 summary + spans；
    // - 如果后面 worker submit 失败，session 会带着 prepared 缓存回滚回 manager；
    // - 下一次 retry 再进 ProcessDispatchJob 时，primary_enqueued=true，说明主数据已经成功入缓冲，
    //   这时就必须跳过 AppendPrimary，避免同一条 trace 重复写出第二份主数据。
    if (buffered_trace_repo_ && !session->primary_enqueued)
    {
        BufferedTraceRepository::TracePrimaryWrite primary_write;
        // summary_ptr 指向 session 内部 prepared_summary。
        // 这里做一次值拷贝是必要的，因为写入器拿到的是独立 write 对象，不能直接借 session 内部对象跨层保存引用。
        if (summary_ptr)
        {
            primary_write.summary = *summary_ptr;
        }
        // span_records 是本轮 dispatch 临时构建出来的主数据明细；
        // append 之后当前线程不再需要它，所以直接 move 进写入对象，避免再拷一份 vector 内容。
        primary_write.spans = std::move(span_records);
        if (!buffered_trace_repo_->AppendPrimary(std::move(primary_write)))
        {
            // AppendPrimary 失败说明“主数据首段”连缓冲写入器这一层都没进去。
            // 既然这时 trace 还停留在 dispatch 中间态，就不能让它继续留在 inflight，否则：
            // 1) manager 里查不到这条 session；
            // 2) tombstone 也还没建立；
            // 3) 晚到 span 会落在“旧 session 已摘走、新 session 还没回滚”的缝里，状态会乱。
            // 所以这里必须在同一把 manager 锁里做三件事：
            // - 删掉 inflight 标记
            // - 把 session 放回 manager
            // - 改成 ReadyRetryLater，挂上下一次 retry tick
            std::lock_guard<std::mutex> lock(mutex_);
            auto inflight_iter = dispatching_inflight_.find(trace_key);
            if (inflight_iter != dispatching_inflight_.end() &&
                inflight_iter->second.session_epoch == session_epoch)
            {
                dispatching_inflight_.erase(inflight_iter);
            }
            RestoreSessionLocked(std::move(session), span_count);
            return;
        }
        // 只有 AppendPrimary 成功后，才能把 primary_enqueued 置 true。
        // 这个位代表“summary + spans 已经成功送入 BufferedTraceRepository”，
        // 不代表 analysis 已经完成，更不代表 SQLite flush 已经落盘。
        session->primary_enqueued = true;
    }

    auto session_holder = std::make_shared<std::unique_ptr<TraceSession>>(std::move(session));
    TraceSessionManager *manager = this;
    BufferedTraceRepository *buffered_trace_repo = buffered_trace_repo_;
    TraceAiProvider *trace_ai = trace_ai_;
    INotifier *notifier = notifier_;
    ServiceRuntimeAccumulator* service_runtime_accumulator = service_runtime_accumulator_;
    SystemRuntimeAccumulator* system_runtime_accumulator = system_runtime_accumulator_;
    const std::string *worker_trace_payload = trace_payload_ptr;
    const TraceRepository::TraceSummary *worker_summary = summary_ptr;
    const uint64_t worker_enqueue_ns = NowSteadyNs();
    if (!thread_pool_->submit([manager, buffered_trace_repo, trace_ai, notifier, service_runtime_accumulator, system_runtime_accumulator, worker_enqueue_ns, session_holder, worker_trace_payload, worker_summary, analysis_observation_span_records]() mutable
                              {
        if (!manager || !session_holder || !(*session_holder) || !worker_trace_payload || !worker_summary) {
            return;
        }
        manager->worker_begin_count_.fetch_add(1, std::memory_order_relaxed);
        const uint64_t worker_begin_ns = NowSteadyNs();
        const uint64_t queue_wait_ms =
            worker_begin_ns >= worker_enqueue_ns ? (worker_begin_ns - worker_enqueue_ns) / 1000000ULL : 0;

        TraceRepository::TraceAnalysisRecord analysis_record;
        TraceRepository::TraceAnalysisRecord* analysis_ptr = nullptr;
        size_t alert_token_count = worker_summary->token_count;
        std::optional<TraceAiUsage> completed_usage;
        if (trace_ai) {
            if (system_runtime_accumulator) {
                // 系统监控里的 AI 调用总数要落在“真正准备调模型”的时间点，
                // 不能在 submit 成功时就提前加，否则排队中断或后续没进入模型都算脏数据。
                system_runtime_accumulator->RecordAiCallStarted();
            }
            manager->ai_calls_.fetch_add(1, std::memory_order_relaxed);
            const uint64_t ai_begin_ns = NowSteadyNs();
            uint64_t inference_latency_ms = 0;
            try {
                TraceAiResponse ai_response = trace_ai->AnalyzeTrace(*worker_trace_payload);
                analysis_record = manager->BuildAnalysisRecord(worker_summary->trace_id, ai_response.analysis);
                analysis_ptr = &analysis_record;
                // worker_summary 指向的是已经落完 primary 的只读摘要，这里不能回写它本体。
                // 所以后面发告警时单独走 alert_token_count，避免为了一个展示口径去改数据库主记录。
                alert_token_count = ResolveAlertTokenCount(*worker_summary, ai_response.usage);
                completed_usage = ai_response.usage;
            } catch (const std::exception& e) {
                analysis_record.trace_id = worker_summary->trace_id;
                analysis_record.risk_level = "unknown";
                analysis_record.summary = "AI_ANALYSIS_FAILED";
                analysis_record.root_cause = e.what();
                analysis_record.solution = "请检查 AI 代理与模型服务状态，稍后重试该 trace 的分析。";
                analysis_record.confidence = 0.0;
                analysis_ptr = &analysis_record;
            } catch (...) {
                analysis_record.trace_id = worker_summary->trace_id;
                analysis_record.risk_level = "unknown";
                analysis_record.summary = "AI_ANALYSIS_FAILED";
                analysis_record.root_cause = "Unknown non-std exception";
                analysis_record.solution = "请检查 AI 代理与模型服务状态，稍后重试该 trace 的分析。";
                analysis_record.confidence = 0.0;
                analysis_ptr = &analysis_record;
            }
            const uint64_t ai_end_ns = NowSteadyNs();
            manager->ai_total_ns_.fetch_add(ai_end_ns - ai_begin_ns, std::memory_order_relaxed);
            inference_latency_ms = ai_end_ns >= ai_begin_ns ? (ai_end_ns - ai_begin_ns) / 1000000ULL : 0;
            if (system_runtime_accumulator) {
                // 这里把排队等待和真实推理耗时作为同一条完成样本写进去。
                // 前者在 worker 开始时就能算，但只有到 AI 收尾时，这条调用样本才算真正成熟。
                system_runtime_accumulator->RecordAiCallCompleted(queue_wait_ms, inference_latency_ms, completed_usage);
            }
        }

        manager->analysis_enqueue_calls_.fetch_add(1, std::memory_order_relaxed);
        const uint64_t enqueue_begin_ns = NowSteadyNs();
        bool saved = true;
        if (!buffered_trace_repo) {
            saved = false;
        } else {
            BufferedTraceRepository::TraceAnalysisWrite analysis_write;
            if (analysis_ptr) {
                analysis_write.analysis = *analysis_ptr;
            }
            saved = buffered_trace_repo->AppendAnalysis(std::move(analysis_write));
        }
        manager->analysis_enqueue_total_ns_.fetch_add(NowSteadyNs() - enqueue_begin_ns, std::memory_order_relaxed);
        manager->worker_done_count_.fetch_add(1, std::memory_order_relaxed);
        if (service_runtime_accumulator && analysis_ptr && analysis_observation_span_records)
        {
            // AI 回来后只补最近样本，不再回写 overview / 服务统计，避免同一条 trace 重复记账。
            service_runtime_accumulator->OnAnalysisReady(
                BuildAnalysisObservation(*worker_summary,
                                         *analysis_observation_span_records,
                                         analysis_ptr->summary,
                                         analysis_ptr->risk_level));
        }
        if (!saved || !notifier || !analysis_ptr) {
            return;
        }

        const std::string risk_level = toLowerCopy(analysis_ptr->risk_level);
        if (risk_level != "critical") {
            return;
        }

        TraceAlertEvent event;
        event.trace_id = worker_summary->trace_id;
        event.service_name = worker_summary->service_name;
        event.start_time_ms = worker_summary->start_time_ms;
        event.duration_ms = worker_summary->duration_ms;
        event.span_count = worker_summary->span_count;
        event.token_count = alert_token_count;
        event.risk_level = analysis_ptr->risk_level;
        event.summary = analysis_ptr->summary;
        event.root_cause = analysis_ptr->root_cause;
        event.solution = analysis_ptr->solution;
        event.confidence = analysis_ptr->confidence;
        notifier->notifyTraceAlert(event); }))
    {
        submit_fail_count_.fetch_add(1, std::memory_order_relaxed);
        std::unique_ptr<TraceSession> restored_session = std::move(*session_holder);
        std::lock_guard<std::mutex> lock(mutex_);
        auto inflight_iter = dispatching_inflight_.find(trace_key);
        if (inflight_iter != dispatching_inflight_.end() &&
            inflight_iter->second.session_epoch == session_epoch)
        {
            dispatching_inflight_.erase(inflight_iter);
        }
        RestoreSessionLocked(std::move(restored_session), span_count);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto inflight_iter = dispatching_inflight_.find(trace_key);
        if (inflight_iter != dispatching_inflight_.end() &&
            inflight_iter->second.session_epoch == session_epoch)
        {
            dispatching_inflight_.erase(inflight_iter);
        }
        AddCompletedTombstoneLocked(trace_key);
    }
    if (service_runtime_accumulator_ && primary_observation.has_value())
    {
        // 只有 worker submit 真成功后，才把这条 trace 记进服务监控统计。
        // 这样前面如果发生 rollback / retry，就不会把“其实没成功进入后链路”的脏数据记进去。
        service_runtime_accumulator_->OnPrimaryCommitted(primary_observation.value());
    }
    submit_ok_count_.fetch_add(1, std::memory_order_relaxed);
}

void TraceSessionManager::RefreshOverloadState()
{
    const size_t pending_tasks = thread_pool_ ? thread_pool_->pendingTasks() : 0;

    const bool hit_critical =
        total_buffered_spans_ >= buffered_span_watermark_.critical ||
        active_sessions_ >= active_session_watermark_.critical ||
        pending_tasks >= pending_task_watermark_.critical;
    const bool hit_high =
        total_buffered_spans_ >= buffered_span_watermark_.high ||
        active_sessions_ >= active_session_watermark_.high ||
        pending_tasks >= pending_task_watermark_.high;
    const bool back_to_low =
        total_buffered_spans_ <= buffered_span_watermark_.low &&
        active_sessions_ <= active_session_watermark_.low &&
        pending_tasks <= pending_task_watermark_.low;

    if (hit_critical)
    {
        overload_state_ = OverloadState::Critical;
    }
    else if (overload_state_ == OverloadState::Normal)
    {
        overload_state_ = hit_high ? OverloadState::Overload : OverloadState::Normal;
    }
    else if (back_to_low)
    {
        overload_state_ = OverloadState::Normal;
    }
    else
    {
        overload_state_ = OverloadState::Overload;
    }

    if (system_runtime_accumulator_)
    {
        // 系统监控只展示“综合背压结论”，不直接暴露 manager 内部的高水位实现细节。
        // 所以这里统一把内部 overload_state 映射成前端更容易解释的 Normal/Active/Full 三档。
        system_runtime_accumulator_->UpdateBackpressureStatus(ToSystemBackpressureStatus(overload_state_));
    }
}

bool TraceSessionManager::ShouldRejectIncomingTrace(bool trace_exists) const
{
    if (overload_state_ == OverloadState::Normal)
    {
        return false;
    }
    if (overload_state_ == OverloadState::Critical)
    {
        return true;
    }
    return !trace_exists;
}

TraceSessionManager::TraceIndex TraceSessionManager::BuildTraceIndex(const TraceSession &session)
{
    TraceIndex index;
    index.span_map.reserve(session.spans.size());

    for (const auto &span : session.spans)
    {
        index.span_map[span.span_id] = &span;
    }

    for (const auto &span : session.spans)
    {
        if (span.parent_span_id.has_value())
        {
            const size_t parent_id = span.parent_span_id.value();
            if (index.span_map.find(parent_id) != index.span_map.end())
            {
                index.children[parent_id].push_back(span.span_id);
                continue;
            }
        }
        // 找不到父节点或没有 parent_id 时，视为 root。
        index.roots.push_back(span.span_id);
    }

    return index;
}

std::string TraceSessionManager::SerializeTrace(const TraceIndex &index, std::vector<const SpanEvent *> *order)
{
    nlohmann::json output;
    std::unordered_set<size_t> visited;
    visited.reserve(index.span_map.size());
    bool has_cycle = false;
    std::vector<size_t> cycle_spans;

    auto build_node = [&index, order, &visited, &has_cycle, &cycle_spans](size_t span_id, const auto &self_ref) -> nlohmann::json
    {
        nlohmann::json node;
        if (!visited.insert(span_id).second)
        {
            // 发现环时仅记录异常，避免继续递归导致无限循环。
            has_cycle = true;
            cycle_spans.push_back(span_id);
            return node;
        }
        auto span_iter = index.span_map.find(span_id);
        if (span_iter == index.span_map.end())
        {
            return node;
        }
        const SpanEvent &span = *span_iter->second;
        if (order)
        {
            order->push_back(&span);
        }

        node["trace_id"] = span.trace_key;
        node["span_id"] = span.span_id;
        if (span.parent_span_id.has_value())
        {
            node["parent_id"] = span.parent_span_id.value();
        }
        else
        {
            node["parent_id"] = nullptr;
        }
        node["name"] = span.name;
        node["service_name"] = span.service_name;
        node["start_time_ms"] = span.start_time_ms;
        if (span.end_time.has_value())
        {
            node["end_time_ms"] = span.end_time.value();
        }
        else
        {
            node["end_time_ms"] = nullptr;
        }

        if (span.status.has_value())
        {
            switch (span.status.value())
            {
            case SpanEvent::Status::Unset:
                node["status"] = "UNSET";
                break;
            case SpanEvent::Status::Ok:
                node["status"] = "OK";
                break;
            case SpanEvent::Status::Error:
                node["status"] = "ERROR";
                break;
            }
        }
        else
        {
            node["status"] = nullptr;
        }

        if (span.kind.has_value())
        {
            switch (span.kind.value())
            {
            case SpanEvent::Kind::Internal:
                node["kind"] = "INTERNAL";
                break;
            case SpanEvent::Kind::Server:
                node["kind"] = "SERVER";
                break;
            case SpanEvent::Kind::Client:
                node["kind"] = "CLIENT";
                break;
            case SpanEvent::Kind::Producer:
                node["kind"] = "PRODUCER";
                break;
            case SpanEvent::Kind::Consumer:
                node["kind"] = "CONSUMER";
                break;
            }
        }
        else
        {
            node["kind"] = nullptr;
        }

        node["attributes"] = span.attributes;

        auto child_iter = index.children.find(span_id);
        const std::vector<size_t> *children_ids = nullptr;
        if (child_iter != index.children.end())
        {
            children_ids = &child_iter->second;
        }

        node["children"] = nlohmann::json::array();
        if (children_ids)
        {
            std::vector<size_t> ordered_children = *children_ids;
            std::sort(ordered_children.begin(), ordered_children.end(), [&index](size_t left, size_t right)
                      {
                const auto left_iter = index.span_map.find(left);
                const auto right_iter = index.span_map.find(right);
                if (left_iter == index.span_map.end() || right_iter == index.span_map.end()) {
                    return left < right;
                }
                const SpanEvent& left_span = *left_iter->second;
                const SpanEvent& right_span = *right_iter->second;
                if (left_span.start_time_ms != right_span.start_time_ms) {
                    return left_span.start_time_ms < right_span.start_time_ms;
                }
                return left_span.span_id < right_span.span_id; });
            for (size_t child_id : ordered_children)
            {
                nlohmann::json child_node = self_ref(child_id, self_ref);
                if (!child_node.is_null() && !child_node.empty())
                {
                    node["children"].push_back(std::move(child_node));
                }
            }
        }

        return node;
    };

    output["spans"] = nlohmann::json::array();
    for (size_t root_id : index.roots)
    {
        nlohmann::json root_node = build_node(root_id, build_node);
        if (!root_node.is_null() && !root_node.empty())
        {
            output["spans"].push_back(std::move(root_node));
        }
    }
    if (has_cycle)
    {
        // 记录位置（第一处）：
        // 1) 异常会写入当前序列化结果 output["anomalies"]，并随 trace_payload 继续流转。
        // 2) 当前实现不单独持久化 anomalies 表，因此排障时应优先看 AI 输入/日志中的 payload 内容。
        // 因为有环所以没有root所以就是dfs根本不会跑，导致order为空，ai无法具有分析性
        nlohmann::json anomalies = nlohmann::json::array();
        for (size_t span_id : cycle_spans)
        {
            nlohmann::json item;
            item["type"] = "cycle_detected";
            item["span_id"] = span_id;
            anomalies.push_back(std::move(item));
        }
        output["anomalies"] = std::move(anomalies);
    }

    return output.dump();
}

TraceRepository::TraceSummary TraceSessionManager::BuildTraceSummary(const TraceSession &session,
                                                                     const std::vector<const SpanEvent *> &order)
{
    TraceRepository::TraceSummary summary;
    summary.trace_id = std::to_string(session.trace_key);
    summary.service_name = order.empty()
                               ? (session.spans.empty() ? "" : session.spans.front().service_name)
                               : order.front()->service_name;

    int64_t min_start = std::numeric_limits<int64_t>::max();
    int64_t max_end = std::numeric_limits<int64_t>::min();
    bool has_end = false;
    for (const auto &span : session.spans)
    {
        min_start = std::min(min_start, span.start_time_ms);
        if (span.end_time.has_value())
        {
            has_end = true;
            max_end = std::max(max_end, span.end_time.value());
        }
    }

    summary.start_time_ms = session.spans.empty() ? 0 : min_start;
    summary.end_time_ms = has_end ? std::optional<int64_t>(max_end) : std::nullopt;
    summary.duration_ms = (has_end && summary.start_time_ms > 0) ? (max_end - summary.start_time_ms) : 0;
    summary.span_count = session.spans.size();
    summary.token_count = session.token_count;
    summary.risk_level = "unknown";

    return summary;
}

std::vector<TraceRepository::TraceSpanRecord> TraceSessionManager::BuildSpanRecords(
    const std::vector<const SpanEvent *> &order)
{
    std::vector<TraceRepository::TraceSpanRecord> span_records;
    span_records.reserve(order.size());

    for (const SpanEvent *span_ptr : order)
    {
        if (!span_ptr)
        {
            continue;
        }
        const SpanEvent &span = *span_ptr;
        TraceRepository::TraceSpanRecord record;
        record.trace_id = std::to_string(span.trace_key);
        record.span_id = std::to_string(span.span_id);
        record.parent_id = span.parent_span_id.has_value()
                               ? std::optional<std::string>(std::to_string(span.parent_span_id.value()))
                               : std::nullopt;
        record.service_name = span.service_name;
        record.operation = span.name;
        record.start_time_ms = span.start_time_ms;
        record.duration_ms = span.end_time.has_value() ? (span.end_time.value() - span.start_time_ms) : 0;

        if (!span.status.has_value())
        {
            record.status = "UNSET";
        }
        else
        {
            switch (span.status.value())
            {
            case SpanEvent::Status::Unset:
                record.status = "UNSET";
                break;
            case SpanEvent::Status::Ok:
                record.status = "OK";
                break;
            case SpanEvent::Status::Error:
                record.status = "ERROR";
                break;
            }
        }

        record.attributes_json = nlohmann::json(span.attributes).dump();
        span_records.push_back(std::move(record));
    }

    return span_records;
}

TraceRepository::TraceAnalysisRecord TraceSessionManager::BuildAnalysisRecord(const std::string &trace_id,
                                                                              const LogAnalysisResult &analysis)
{
    TraceRepository::TraceAnalysisRecord analysis_record;
    nlohmann::json risk_json = analysis.risk_level;

    analysis_record.trace_id = trace_id;
    analysis_record.risk_level = risk_json.get<std::string>();
    analysis_record.summary = analysis.summary;
    analysis_record.root_cause = analysis.root_cause;
    analysis_record.solution = analysis.solution;
    analysis_record.confidence = 0.0;

    return analysis_record;
}
