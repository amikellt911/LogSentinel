#include "core/ServiceRuntimeAccumulator.h"

#include <algorithm>

namespace
{
int64_t SafeAverageLatency(int64_t duration_sum_ms, uint64_t count)
{
    if (count == 0)
    {
        return 0;
    }
    return duration_sum_ms / static_cast<int64_t>(count);
}

std::string MakeGlobalOperationKey(const std::string& service_name, const std::string& operation_name)
{
    return service_name + "\n" + operation_name;
}
} // namespace

ServiceRuntimeAccumulator::ServiceRuntimeAccumulator(size_t service_top_k,
                                                     size_t operation_top_k,
                                                     size_t recent_sample_limit)
    : service_top_k_(std::max<size_t>(1, service_top_k)),
      operation_top_k_(std::max<size_t>(1, operation_top_k)),
      recent_sample_limit_(std::max<size_t>(1, recent_sample_limit))
{
    // 构造期先发布一个空快照，避免 HTTP 比主链路更早起来时返回缺字段 JSON。
    published_snapshot_ = BuildSnapshotLocked();
}

void ServiceRuntimeAccumulator::OnPrimaryCommitted(const PrimaryObservation& observation)
{
    std::lock_guard<std::mutex> lock(mutex_);

    bool trace_has_error = false;
    for (const auto& service_observation : observation.services)
    {
        if (service_observation.service_name.empty())
        {
            continue;
        }

        ServiceState& service_state = services_[service_observation.service_name];
        service_state.service_name = service_observation.service_name;

        if (service_observation.error_trace_hit)
        {
            ++service_state.exception_count;
            trace_has_error = true;
        }
        service_state.error_span_count += service_observation.error_span_count;
        service_state.error_span_duration_sum_ms += service_observation.error_span_duration_sum_ms;
        service_state.latest_exception_time_ms =
            std::max(service_state.latest_exception_time_ms, service_observation.latest_exception_time_ms);
        latest_exception_time_ms_ =
            std::max(latest_exception_time_ms_, service_observation.latest_exception_time_ms);

        for (const auto& operation_observation : service_observation.operations)
        {
            if (operation_observation.operation_name.empty())
            {
                continue;
            }

            OperationState& operation_state = service_state.operations[operation_observation.operation_name];
            operation_state.count += operation_observation.error_span_count;
            operation_state.duration_sum_ms += operation_observation.error_span_duration_sum_ms;

            const std::string global_key =
                MakeGlobalOperationKey(service_observation.service_name, operation_observation.operation_name);
            GlobalOperationState& global_state = global_operations_[global_key];
            global_state.service_name = service_observation.service_name;
            global_state.operation_name = operation_observation.operation_name;
            global_state.count += operation_observation.error_span_count;
            global_state.duration_sum_ms += operation_observation.error_span_duration_sum_ms;
        }
    }

    if (trace_has_error)
    {
        ++abnormal_trace_count_;
    }
}

void ServiceRuntimeAccumulator::OnAnalysisReady(const AnalysisObservation& observation)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& service_sample : observation.service_samples)
    {
        if (service_sample.service_name.empty())
        {
            continue;
        }

        ServiceState& service_state = services_[service_sample.service_name];
        service_state.service_name = service_sample.service_name;

        ServiceRuntimeRecentSampleView sample_view;
        sample_view.trace_id = observation.trace_id;
        sample_view.time_ms = service_sample.sample_time_ms;
        sample_view.operation_name = service_sample.representative_operation_name;
        sample_view.summary = observation.trace_summary;
        sample_view.duration_ms = service_sample.duration_ms;
        sample_view.risk_level = service_sample.sample_risk_level;

        // 同一条 trace 在同一服务下只保留一条样本，后续 AI 补写时允许覆盖旧内容，避免重复卡片。
        auto& recent_samples = service_state.recent_samples;
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
    // 第一刀先不把分钟桶塞进来，OnTick 只做“把可变态裁成稳定快照”这一件事。
    // 这样 handler 永远只读发布态，后面接窗口推进时也不用改 HTTP 层。
    published_snapshot_ = BuildSnapshotLocked();
}

ServiceRuntimeSnapshot ServiceRuntimeAccumulator::BuildSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
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

ServiceRuntimeSnapshot ServiceRuntimeAccumulator::BuildSnapshotLocked() const
{
    ServiceRuntimeSnapshot snapshot;
    snapshot.overview.abnormal_trace_count = abnormal_trace_count_;
    snapshot.overview.latest_exception_time_ms = latest_exception_time_ms_;

    std::vector<ServiceRuntimeServiceView> service_views;
    service_views.reserve(services_.size());

    for (const auto& entry : services_)
    {
        const ServiceState& service_state = entry.second;
        if (service_state.exception_count > 0)
        {
            ++snapshot.overview.abnormal_service_count;
        }

        ServiceRuntimeServiceView service_view;
        service_view.service_name = service_state.service_name;
        service_view.risk_level = ComputeServiceRiskLevel(service_state.exception_count);
        service_view.exception_count = service_state.exception_count;
        service_view.avg_latency_ms =
            SafeAverageLatency(service_state.error_span_duration_sum_ms, service_state.error_span_count);
        service_view.latest_exception_time_ms = service_state.latest_exception_time_ms;
        service_view.recent_samples = service_state.recent_samples;

        std::vector<ServiceRuntimeOperationView> operation_views;
        operation_views.reserve(service_state.operations.size());
        for (const auto& operation_entry : service_state.operations)
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
