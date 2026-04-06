#include "core/ServiceRuntimeAccumulator.h"

#include <algorithm>
#include <chrono>

namespace
{
int64_t SafeAverageLatency(int64_t duration_sum_ms, uint64_t count)
{
    // 平均耗时展示只依赖 sum/count，这里统一兜底 count=0 的情况，
    // 避免服务刚进榜但异常 span 计数还没起来时被 0 除。
    if (count == 0)
    {
        return 0;
    }
    return duration_sum_ms / static_cast<int64_t>(count);
}

std::string MakeGlobalOperationKey(const std::string& service_name, const std::string& operation_name)
{
    // 当前全局操作榜按“服务 + 操作名”唯一定位。
    // 用一个简单字符串 key 就够了，后面如果真要极致性能再拆 pair/hash。
    return service_name + "\n" + operation_name;
}
} // namespace

ServiceRuntimeAccumulator::ServiceRuntimeAccumulator(size_t service_top_k,
                                                     size_t operation_top_k,
                                                     size_t recent_sample_limit,
                                                     size_t window_minutes,
                                                     MonotonicNowMsFn monotonic_now_ms_fn)
    : service_top_k_(std::max<size_t>(1, service_top_k)),
      operation_top_k_(std::max<size_t>(1, operation_top_k)),
      recent_sample_limit_(std::max<size_t>(1, recent_sample_limit)),
      window_minutes_(std::max<size_t>(1, window_minutes)),
      monotonic_now_ms_fn_(std::move(monotonic_now_ms_fn))
{
    if (!monotonic_now_ms_fn_)
    {
        monotonic_now_ms_fn_ = &ServiceRuntimeAccumulator::DefaultMonotonicNowMs;
    }

    // 分钟桶除了要保留“最近 window 分钟”之外，还必须额外留一个活跃写入分钟。
    // 否则当前分钟和 window 边界上的最老分钟会打到同一个槽，导致还没退窗就被覆盖。
    buckets_.resize(window_minutes_ + 1);
    active_minute_ = CurrentMinuteLocked();
    sealed_minute_ = active_minute_ - 1;

    // 构造期先发布一个空快照，避免 HTTP 比主链路更早起来时返回缺字段 JSON。
    published_snapshot_ = BuildSnapshotLocked();
}

void ServiceRuntimeAccumulator::OnPrimaryCommitted(const PrimaryObservation& observation)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // 这里先只写“当前活跃分钟”的原料桶，不直接改窗口累计态。
    // 既然当前分钟还没结束，那么它的数据还可能继续增长，提前发到前端会让榜单抖动。
    const int64_t minute_id = CurrentMinuteLocked();
    active_minute_ = std::max(active_minute_, minute_id);
    MinuteBucket& bucket = EnsureBucketForMinuteLocked(minute_id);

    bool trace_has_error = false;
    for (const auto& service_observation : observation.services)
    {
        if (service_observation.service_name.empty())
        {
            continue;
        }

        // recent/latest 是“最近态”，不走分钟窗；这里继续按全局最近时间更新。
        RecentServiceState& recent_service = recent_services_[service_observation.service_name];
        recent_service.service_name = service_observation.service_name;
        recent_service.latest_exception_time_ms =
            std::max(recent_service.latest_exception_time_ms, service_observation.latest_exception_time_ms);
        latest_exception_time_ms_ =
            std::max(latest_exception_time_ms_, service_observation.latest_exception_time_ms);

        ServiceDelta& service_delta = bucket.service_deltas[service_observation.service_name];
        if (service_observation.error_trace_hit)
        {
            // 服务卡上的 exception_count 按“trace + service”去重后只记 1 次，
            // 所以 observation 这里已经提前做完归并，桶里直接吃这一个 delta。
            ++service_delta.exception_count;
            trace_has_error = true;
        }
        AddUnsigned(service_delta.error_span_count, service_observation.error_span_count);
        AddSigned(service_delta.error_span_duration_sum_ms, service_observation.error_span_duration_sum_ms);

        for (const auto& operation_observation : service_observation.operations)
        {
            if (operation_observation.operation_name.empty())
            {
                continue;
            }

            OperationDelta& operation_delta = service_delta.operations[operation_observation.operation_name];
            AddUnsigned(operation_delta.count, operation_observation.error_span_count);
            AddSigned(operation_delta.duration_sum_ms, operation_observation.error_span_duration_sum_ms);
        }
    }

    if (trace_has_error)
    {
        // overview 的异常链路数也是窗口统计，所以这里只给当前分钟桶记增量。
        ++bucket.abnormal_trace_count;
    }
}

void ServiceRuntimeAccumulator::OnAnalysisReady(const AnalysisObservation& observation)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // analysis 路径不碰分钟窗统计，只补最近态样本。
    // 这样主链路统计和 AI 后补两条路径不会互相覆盖，也不会重复记 overview。
    for (const auto& service_sample : observation.service_samples)
    {
        if (service_sample.service_name.empty())
        {
            continue;
        }

        RecentServiceState& recent_service = recent_services_[service_sample.service_name];
        recent_service.service_name = service_sample.service_name;
        recent_service.latest_exception_time_ms =
            std::max(recent_service.latest_exception_time_ms, service_sample.sample_time_ms);
        latest_exception_time_ms_ = std::max(latest_exception_time_ms_, service_sample.sample_time_ms);

        ServiceRuntimeRecentSampleView sample_view;
        sample_view.trace_id = observation.trace_id;
        sample_view.time_ms = service_sample.sample_time_ms;
        sample_view.operation_name = service_sample.representative_operation_name;
        sample_view.summary = observation.trace_summary;
        sample_view.duration_ms = service_sample.duration_ms;
        sample_view.risk_level = service_sample.sample_risk_level;

        // 同一条 trace 在同一服务下只保留一条样本，后续 AI 补写时允许覆盖旧内容，避免重复卡片。
        auto& recent_samples = recent_service.recent_samples;
        recent_samples.erase(
            std::remove_if(recent_samples.begin(),
                           recent_samples.end(),
                           [&](const ServiceRuntimeRecentSampleView& existing)
                           {
                               return existing.trace_id == sample_view.trace_id;
                           }),
            recent_samples.end());

        recent_samples.push_back(std::move(sample_view));
        std::sort(recent_samples.begin(), recent_samples.end(), CompareRecentSample);
        if (recent_samples.size() > recent_sample_limit_)
        {
            recent_samples.resize(recent_sample_limit_);
        }
    }
}

void ServiceRuntimeAccumulator::OnTick()
{
    std::lock_guard<std::mutex> lock(mutex_);

    const int64_t now_minute = CurrentMinuteLocked();
    // 只有“已经结束的分钟”才允许进窗。当前活跃分钟可能还在不断写入，
    // 所以这里只推进到 now_minute - 1，避免把半分钟数据提前算进去。
    for (int64_t minute = sealed_minute_ + 1; minute < now_minute; ++minute)
    {
        // 先把这个刚刚封口的分钟并进窗口。
        if (const MinuteBucket* sealed_bucket = FindBucketForMinuteLocked(minute))
        {
            ApplyBucketToWindowLocked(*sealed_bucket, /*add_into_window*/true);
        }

        // 再把“窗口长度之前”的老分钟退掉。
        // 这样窗口里始终只保留最近 window_minutes 个已封口分钟。
        const int64_t expired_minute = minute - static_cast<int64_t>(window_minutes_);
        if (expired_minute >= 0)
        {
            if (const MinuteBucket* expired_bucket = FindBucketForMinuteLocked(expired_minute))
            {
                ApplyBucketToWindowLocked(*expired_bucket, /*add_into_window*/false);
            }
        }

        sealed_minute_ = minute;
    }

    active_minute_ = std::max(active_minute_, now_minute);
    published_snapshot_ = BuildSnapshotLocked();
}

ServiceRuntimeSnapshot ServiceRuntimeAccumulator::BuildSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    // 外部永远只拿已经发布好的快照，避免 handler 线程现场去跑排序和裁切。
    return published_snapshot_;
}

std::string ServiceRuntimeAccumulator::ComputeServiceRiskLevel(uint64_t exception_count)
{
    if (exception_count == 0)
    {
        return "safe";
    }
    if (exception_count <= 5)
    {
        return "warning";
    }
    return "error";
}

bool ServiceRuntimeAccumulator::CompareServiceView(const ServiceRuntimeServiceView& lhs,
                                                   const ServiceRuntimeServiceView& rhs)
{
    if (lhs.exception_count != rhs.exception_count)
    {
        return lhs.exception_count > rhs.exception_count;
    }
    if (lhs.latest_exception_time_ms != rhs.latest_exception_time_ms)
    {
        return lhs.latest_exception_time_ms > rhs.latest_exception_time_ms;
    }
    return lhs.service_name < rhs.service_name;
}

bool ServiceRuntimeAccumulator::CompareOperationView(const ServiceRuntimeOperationView& lhs,
                                                     const ServiceRuntimeOperationView& rhs)
{
    if (lhs.count != rhs.count)
    {
        return lhs.count > rhs.count;
    }
    if (lhs.avg_latency_ms != rhs.avg_latency_ms)
    {
        return lhs.avg_latency_ms > rhs.avg_latency_ms;
    }
    return lhs.operation_name < rhs.operation_name;
}

bool ServiceRuntimeAccumulator::CompareGlobalOperationView(const ServiceRuntimeGlobalOperationView& lhs,
                                                           const ServiceRuntimeGlobalOperationView& rhs)
{
    if (lhs.count != rhs.count)
    {
        return lhs.count > rhs.count;
    }
    if (lhs.avg_latency_ms != rhs.avg_latency_ms)
    {
        return lhs.avg_latency_ms > rhs.avg_latency_ms;
    }
    if (lhs.service_name != rhs.service_name)
    {
        return lhs.service_name < rhs.service_name;
    }
    return lhs.operation_name < rhs.operation_name;
}

bool ServiceRuntimeAccumulator::CompareRecentSample(const ServiceRuntimeRecentSampleView& lhs,
                                                    const ServiceRuntimeRecentSampleView& rhs)
{
    if (lhs.time_ms != rhs.time_ms)
    {
        return lhs.time_ms > rhs.time_ms;
    }
    return lhs.trace_id < rhs.trace_id;
}

int64_t ServiceRuntimeAccumulator::DefaultMonotonicNowMs()
{
    // 时间窗推进依赖“经过了多久”，不是依赖墙上时钟，所以这里用 steady_clock。
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void ServiceRuntimeAccumulator::AddUnsigned(uint64_t& target, uint64_t delta)
{
    target += delta;
}

void ServiceRuntimeAccumulator::SubtractUnsigned(uint64_t& target, uint64_t delta)
{
    // 退窗时所有统计都应该和之前进窗配平，但这里仍然做下溢保护，
    // 避免未来 observation 口径改动或极端边界导致 uint64_t 回卷成超大值。
    if (delta >= target)
    {
        target = 0;
        return;
    }
    target -= delta;
}

void ServiceRuntimeAccumulator::AddSigned(int64_t& target, int64_t delta)
{
    target += delta;
}

void ServiceRuntimeAccumulator::SubtractSigned(int64_t& target, int64_t delta)
{
    target -= delta;
    if (target < 0)
    {
        // 理论上窗口配平后不该出现负数，这里钳到 0 只是防御性保护。
        target = 0;
    }
}

size_t ServiceRuntimeAccumulator::BucketIndexForMinute(int64_t minute_id) const
{
    return static_cast<size_t>(minute_id % static_cast<int64_t>(buckets_.size()));
}

int64_t ServiceRuntimeAccumulator::CurrentMinuteLocked() const
{
    return monotonic_now_ms_fn_() / (60 * 1000);
}

ServiceRuntimeAccumulator::MinuteBucket&
ServiceRuntimeAccumulator::EnsureBucketForMinuteLocked(int64_t minute_id)
{
    MinuteBucket& bucket = buckets_[BucketIndexForMinute(minute_id)];
    if (bucket.minute_id == minute_id)
    {
        return bucket;
    }

    // 这里假设分钟推进不会停摆超过整个窗口长度。
    // 既然 tick 正常每 5 秒执行，那么旧 minute 在复用前应该已经完成了“进窗+退窗”。
    // 如果未来真的要容忍长时间停摆，再把这里扩成 pending-retire 慢路径。
    bucket = MinuteBucket{};
    bucket.minute_id = minute_id;
    return bucket;
}

const ServiceRuntimeAccumulator::MinuteBucket*
ServiceRuntimeAccumulator::FindBucketForMinuteLocked(int64_t minute_id) const
{
    const MinuteBucket& bucket = buckets_[BucketIndexForMinute(minute_id)];
    if (bucket.minute_id != minute_id)
    {
        return nullptr;
    }
    return &bucket;
}

void ServiceRuntimeAccumulator::ApplyBucketToWindowLocked(const MinuteBucket& bucket, bool add_into_window)
{
    // 这个函数是窗口系统的中心：同一份分钟原料既能“进窗”，也能“退窗”。
    // 所以 bucket 里只能存纯增量，不能存 summary、sample 这种不可逆的数据。
    if (add_into_window)
    {
        AddUnsigned(abnormal_trace_count_, bucket.abnormal_trace_count);
    }
    else
    {
        SubtractUnsigned(abnormal_trace_count_, bucket.abnormal_trace_count);
    }

    for (const auto& service_entry : bucket.service_deltas)
    {
        const std::string& service_name = service_entry.first;
        const ServiceDelta& delta = service_entry.second;

        WindowServiceState& window_service = window_services_[service_name];
        window_service.service_name = service_name;

        if (add_into_window)
        {
            AddUnsigned(window_service.exception_count, delta.exception_count);
            AddUnsigned(window_service.error_span_count, delta.error_span_count);
            AddSigned(window_service.error_span_duration_sum_ms, delta.error_span_duration_sum_ms);
        }
        else
        {
            SubtractUnsigned(window_service.exception_count, delta.exception_count);
            SubtractUnsigned(window_service.error_span_count, delta.error_span_count);
            SubtractSigned(window_service.error_span_duration_sum_ms, delta.error_span_duration_sum_ms);
        }

        for (const auto& operation_entry : delta.operations)
        {
            const std::string& operation_name = operation_entry.first;
            const OperationDelta& operation_delta = operation_entry.second;

            OperationState& operation_state = window_service.operations[operation_name];
            if (add_into_window)
            {
                AddUnsigned(operation_state.count, operation_delta.count);
                AddSigned(operation_state.duration_sum_ms, operation_delta.duration_sum_ms);
            }
            else
            {
                SubtractUnsigned(operation_state.count, operation_delta.count);
                SubtractSigned(operation_state.duration_sum_ms, operation_delta.duration_sum_ms);
            }

            if (operation_state.count == 0)
            {
                window_service.operations.erase(operation_name);
            }

            const std::string global_key = MakeGlobalOperationKey(service_name, operation_name);
            GlobalOperationState& global_state = global_operations_[global_key];
            global_state.service_name = service_name;
            global_state.operation_name = operation_name;
            if (add_into_window)
            {
                AddUnsigned(global_state.count, operation_delta.count);
                AddSigned(global_state.duration_sum_ms, operation_delta.duration_sum_ms);
            }
            else
            {
                SubtractUnsigned(global_state.count, operation_delta.count);
                SubtractSigned(global_state.duration_sum_ms, operation_delta.duration_sum_ms);
            }
            PruneGlobalOperationLocked(global_key);
        }

        PruneWindowServiceLocked(service_name);
    }
}

void ServiceRuntimeAccumulator::PruneWindowServiceLocked(const std::string& service_name)
{
    auto iter = window_services_.find(service_name);
    if (iter == window_services_.end())
    {
        return;
    }

    const WindowServiceState& service = iter->second;
    if (service.exception_count == 0 &&
        service.error_span_count == 0 &&
        service.error_span_duration_sum_ms == 0 &&
        service.operations.empty())
    {
        window_services_.erase(iter);
    }
}

void ServiceRuntimeAccumulator::PruneGlobalOperationLocked(const std::string& global_key)
{
    auto iter = global_operations_.find(global_key);
    if (iter == global_operations_.end())
    {
        return;
    }

    const GlobalOperationState& operation = iter->second;
    if (operation.count == 0 && operation.duration_sum_ms == 0)
    {
        global_operations_.erase(iter);
    }
}

ServiceRuntimeSnapshot ServiceRuntimeAccumulator::BuildSnapshotLocked() const
{
    ServiceRuntimeSnapshot snapshot;
    snapshot.overview.abnormal_trace_count = abnormal_trace_count_;
    snapshot.overview.latest_exception_time_ms = latest_exception_time_ms_;

    // 窗口态是“完整真相”，前端的 topk 和排行榜都在发布时现裁。
    // 这样写路径只做累加/退账，不需要长期维护一个会 stale 的在线 topk 结构。
    std::vector<ServiceRuntimeServiceView> service_views;
    service_views.reserve(window_services_.size());

    for (const auto& entry : window_services_)
    {
        const WindowServiceState& window_service = entry.second;
        if (window_service.exception_count == 0)
        {
            continue;
        }

        ++snapshot.overview.abnormal_service_count;

        ServiceRuntimeServiceView service_view;
        service_view.service_name = window_service.service_name;
        service_view.risk_level = ComputeServiceRiskLevel(window_service.exception_count);
        service_view.exception_count = window_service.exception_count;
        service_view.avg_latency_ms =
            SafeAverageLatency(window_service.error_span_duration_sum_ms, window_service.error_span_count);

        const auto recent_iter = recent_services_.find(window_service.service_name);
        if (recent_iter != recent_services_.end())
        {
            service_view.latest_exception_time_ms = recent_iter->second.latest_exception_time_ms;
            service_view.recent_samples = recent_iter->second.recent_samples;
        }

        std::vector<ServiceRuntimeOperationView> operation_views;
        operation_views.reserve(window_service.operations.size());
        for (const auto& operation_entry : window_service.operations)
        {
            ServiceRuntimeOperationView operation_view;
            operation_view.operation_name = operation_entry.first;
            operation_view.count = operation_entry.second.count;
            operation_view.avg_latency_ms =
                SafeAverageLatency(operation_entry.second.duration_sum_ms, operation_entry.second.count);
            operation_views.push_back(std::move(operation_view));
        }
        std::sort(operation_views.begin(), operation_views.end(), CompareOperationView);
        if (operation_views.size() > operation_top_k_)
        {
            operation_views.resize(operation_top_k_);
        }
        service_view.operation_ranking = std::move(operation_views);
        service_views.push_back(std::move(service_view));
    }

    std::sort(service_views.begin(), service_views.end(), CompareServiceView);
    if (service_views.size() > service_top_k_)
    {
        service_views.resize(service_top_k_);
    }
    snapshot.services_topk = std::move(service_views);

    std::vector<ServiceRuntimeGlobalOperationView> global_operation_views;
    global_operation_views.reserve(global_operations_.size());
    for (const auto& entry : global_operations_)
    {
        const GlobalOperationState& state = entry.second;
        if (state.count == 0)
        {
            continue;
        }

        ServiceRuntimeGlobalOperationView view;
        view.service_name = state.service_name;
        view.operation_name = state.operation_name;
        view.count = state.count;
        view.avg_latency_ms = SafeAverageLatency(state.duration_sum_ms, state.count);
        global_operation_views.push_back(std::move(view));
    }
    std::sort(global_operation_views.begin(), global_operation_views.end(), CompareGlobalOperationView);
    if (global_operation_views.size() > operation_top_k_)
    {
        global_operation_views.resize(operation_top_k_);
    }
    snapshot.global_operation_ranking = std::move(global_operation_views);
    return snapshot;
}
