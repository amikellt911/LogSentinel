#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

// 这些结构体直接描述“服务监控原型页最终要吃的 JSON 形状”。
// 第一刀先把契约和排序规则锁死，后面再把分钟桶/时间窗逻辑接到同一套结构上。

struct ServiceRuntimeOverview
{
    uint64_t abnormal_service_count = 0;
    uint64_t abnormal_trace_count = 0;
    int64_t latest_exception_time_ms = 0;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ServiceRuntimeOverview,
                                   abnormal_service_count,
                                   abnormal_trace_count,
                                   latest_exception_time_ms);
};

struct ServiceRuntimeOperationView
{
    std::string operation_name;
    uint64_t count = 0;
    int64_t avg_latency_ms = 0;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ServiceRuntimeOperationView,
                                   operation_name,
                                   count,
                                   avg_latency_ms);
};

struct ServiceRuntimeRecentSampleView
{
    std::string trace_id;
    int64_t time_ms = 0;
    std::string operation_name;
    std::string summary;
    int64_t duration_ms = 0;
    std::string risk_level;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ServiceRuntimeRecentSampleView,
                                   trace_id,
                                   time_ms,
                                   operation_name,
                                   summary,
                                   duration_ms,
                                   risk_level);
};

struct ServiceRuntimeServiceView
{
    std::string service_name;
    std::string risk_level;
    uint64_t exception_count = 0;
    int64_t avg_latency_ms = 0;
    int64_t latest_exception_time_ms = 0;
    std::vector<ServiceRuntimeOperationView> operation_ranking;
    std::vector<ServiceRuntimeRecentSampleView> recent_samples;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ServiceRuntimeServiceView,
                                   service_name,
                                   risk_level,
                                   exception_count,
                                   avg_latency_ms,
                                   latest_exception_time_ms,
                                   operation_ranking,
                                   recent_samples);
};

struct ServiceRuntimeGlobalOperationView
{
    std::string service_name;
    std::string operation_name;
    uint64_t count = 0;
    int64_t avg_latency_ms = 0;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ServiceRuntimeGlobalOperationView,
                                   service_name,
                                   operation_name,
                                   count,
                                   avg_latency_ms);
};

struct ServiceRuntimeSnapshot
{
    ServiceRuntimeOverview overview;
    std::vector<ServiceRuntimeServiceView> services_topk;
    std::vector<ServiceRuntimeGlobalOperationView> global_operation_ranking;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ServiceRuntimeSnapshot,
                                   overview,
                                   services_topk,
                                   global_operation_ranking);
};

struct PrimaryOperationObservation
{
    std::string operation_name;
    uint64_t error_span_count = 0;
    int64_t error_span_duration_sum_ms = 0;
};

struct PrimaryServiceObservation
{
    std::string service_name;
    int64_t latest_exception_time_ms = 0;
    bool error_trace_hit = false;
    uint64_t error_span_count = 0;
    int64_t error_span_duration_sum_ms = 0;
    std::vector<PrimaryOperationObservation> operations;
};

struct PrimaryObservation
{
    std::string trace_id;
    int64_t trace_end_time_ms = 0;
    std::string trace_risk_level;
    std::vector<PrimaryServiceObservation> services;
};

struct AnalysisServiceSample
{
    std::string service_name;
    std::string representative_operation_name;
    int64_t sample_time_ms = 0;
    int64_t duration_ms = 0;
    std::string sample_risk_level;
};

struct AnalysisObservation
{
    std::string trace_id;
    int64_t trace_end_time_ms = 0;
    std::string trace_risk_level;
    std::string trace_summary;
    std::vector<AnalysisServiceSample> service_samples;
};

class ServiceRuntimeAccumulator
{
public:
    explicit ServiceRuntimeAccumulator(size_t service_top_k = 4,
                                       size_t operation_top_k = 6,
                                       size_t recent_sample_limit = 3);

    // 主链路提交成功后先更新结构化统计。
    void OnPrimaryCommitted(const PrimaryObservation& observation);
    // AI 分析回来后只补最近样本，不回写前面的统计计数，避免重复记账。
    void OnAnalysisReady(const AnalysisObservation& observation);
    // 第一刀先把 OnTick 收口成“从可变态裁出一个已发布快照”。
    // 后面分钟桶和窗口推进接进来时，仍然复用这一个发布入口。
    void OnTick();
    ServiceRuntimeSnapshot BuildSnapshot() const;

private:
    struct OperationState
    {
        uint64_t count = 0;
        int64_t duration_sum_ms = 0;
    };

    struct GlobalOperationState
    {
        std::string service_name;
        std::string operation_name;
        uint64_t count = 0;
        int64_t duration_sum_ms = 0;
    };

    struct ServiceState
    {
        std::string service_name;
        uint64_t exception_count = 0;
        uint64_t error_span_count = 0;
        int64_t error_span_duration_sum_ms = 0;
        int64_t latest_exception_time_ms = 0;
        std::unordered_map<std::string, OperationState> operations;
        std::vector<ServiceRuntimeRecentSampleView> recent_samples;
    };

    static std::string ComputeServiceRiskLevel(uint64_t exception_count);
    static bool CompareServiceView(const ServiceRuntimeServiceView& lhs,
                                   const ServiceRuntimeServiceView& rhs);
    static bool CompareOperationView(const ServiceRuntimeOperationView& lhs,
                                     const ServiceRuntimeOperationView& rhs);
    static bool CompareGlobalOperationView(const ServiceRuntimeGlobalOperationView& lhs,
                                           const ServiceRuntimeGlobalOperationView& rhs);
    static bool CompareRecentSample(const ServiceRuntimeRecentSampleView& lhs,
                                    const ServiceRuntimeRecentSampleView& rhs);
    ServiceRuntimeSnapshot BuildSnapshotLocked() const;

    mutable std::mutex mutex_;
    size_t service_top_k_ = 4;
    size_t operation_top_k_ = 6;
    size_t recent_sample_limit_ = 3;

    uint64_t abnormal_trace_count_ = 0;
    int64_t latest_exception_time_ms_ = 0;
    std::unordered_map<std::string, ServiceState> services_;
    std::unordered_map<std::string, GlobalOperationState> global_operations_;
    ServiceRuntimeSnapshot published_snapshot_;
};
