#pragma once

#include <functional>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

// 这些结构体直接描述“服务监控原型页最终要吃的 JSON 形状”。
// 当前已经接入“30 分钟单窗口统计 + 最近态样本”这套实现，
// 所以后端内部怎么维护分钟桶，对前端契约都不应该再有影响。

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
    using MonotonicNowMsFn = std::function<int64_t()>;

    explicit ServiceRuntimeAccumulator(size_t service_top_k = 4,
                                       size_t operation_top_k = 6,
                                       size_t recent_sample_limit = 3,
                                       size_t window_minutes = 30,
                                       MonotonicNowMsFn monotonic_now_ms_fn = {});

    // 主链路提交成功后，先把这条 trace 的统计增量写进“当前活跃分钟桶”。
    // 这里故意不直接改窗口累计态，因为当前分钟还没结束；只有分钟封口后，
    // OnTick 才会把这个桶真正并进最近 30 分钟窗口。
    void OnPrimaryCommitted(const PrimaryObservation& observation);
    // AI 分析回来后，只补最近样本和最近时间，不回写窗口统计。
    // 这样同一条 trace 不会在“主链路成功”和“AI 后补”两个时机被重复记账。
    void OnAnalysisReady(const AnalysisObservation& observation);
    // 定时推进已封口分钟，把分钟桶并进窗口累计态，再发布一份稳定快照。
    // handler 永远只读这份发布态，避免请求线程临时参与排序和窗口计算。
    void OnTick();
    // HTTP handler 只读最近一次已经发布好的快照，不在请求线程里现算窗口。
    ServiceRuntimeSnapshot BuildSnapshot() const;

private:
    struct OperationState
    {
        uint64_t count = 0;
        int64_t duration_sum_ms = 0;
    };

    struct OperationDelta
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

    struct ServiceDelta
    {
        uint64_t exception_count = 0;
        uint64_t error_span_count = 0;
        int64_t error_span_duration_sum_ms = 0;
        std::unordered_map<std::string, OperationDelta> operations;
    };

    struct WindowServiceState
    {
        std::string service_name;
        uint64_t exception_count = 0;
        uint64_t error_span_count = 0;
        int64_t error_span_duration_sum_ms = 0;
        std::unordered_map<std::string, OperationState> operations;
    };

    struct RecentServiceState
    {
        std::string service_name;
        int64_t latest_exception_time_ms = 0;
        std::vector<ServiceRuntimeRecentSampleView> recent_samples;
    };

    struct MinuteBucket
    {
        int64_t minute_id = -1;
        uint64_t abnormal_trace_count = 0;
        std::unordered_map<std::string, ServiceDelta> service_deltas;
    };

    // 风险等级目前仍按异常链路数绝对阈值分档，避免和排序规则绑死。
    static std::string ComputeServiceRiskLevel(uint64_t exception_count);
    // 服务榜排序：先看异常链路数，再看最近异常时间，最后按名字稳定排序。
    static bool CompareServiceView(const ServiceRuntimeServiceView& lhs,
                                   const ServiceRuntimeServiceView& rhs);
    // 操作榜排序：先看异常 span 次数，再看平均耗时，最后按名字稳定排序。
    static bool CompareOperationView(const ServiceRuntimeOperationView& lhs,
                                     const ServiceRuntimeOperationView& rhs);
    // 全局操作榜排序和服务内操作榜类似，只是还要带上 service_name 打破平局。
    static bool CompareGlobalOperationView(const ServiceRuntimeGlobalOperationView& lhs,
                                           const ServiceRuntimeGlobalOperationView& rhs);
    // 最近样本只保留最近几条，所以这里按时间倒序排，同 trace 覆盖后再截断。
    static bool CompareRecentSample(const ServiceRuntimeRecentSampleView& lhs,
                                    const ServiceRuntimeRecentSampleView& rhs);
    // 生产环境默认从 steady_clock 取单调毫秒；测试里可以注入假时间。
    static int64_t DefaultMonotonicNowMs();
    // 加法没有业务逻辑，只是为了让“进窗/退窗”的代码长得对称。
    static void AddUnsigned(uint64_t& target, uint64_t delta);
    static void SubtractUnsigned(uint64_t& target, uint64_t delta);
    static void AddSigned(int64_t& target, int64_t delta);
    static void SubtractSigned(int64_t& target, int64_t delta);
    // minute_id 通过 ring 取模命中槽位；槽下标只是地址，minute_id 才是真身份。
    size_t BucketIndexForMinute(int64_t minute_id) const;
    // 当前分钟只由单调时钟决定，不能靠“tick 响了多少次”自己数拍子。
    int64_t CurrentMinuteLocked() const;
    // 写 observation 时命中当前 minute 的槽；如果槽位被旧 minute 占用，就复用成新 minute。
    MinuteBucket& EnsureBucketForMinuteLocked(int64_t minute_id);
    // sweep 进窗/退窗时按 minute_id 找桶；如果槽里已经不是那个 minute，就说明这分钟不存在。
    const MinuteBucket* FindBucketForMinuteLocked(int64_t minute_id) const;
    // 把一个完整分钟的增量并进窗口，或者从窗口里减出去。
    // add_into_window=true 表示“进窗”，false 表示“退窗”。
    void ApplyBucketToWindowLocked(const MinuteBucket& bucket, bool add_into_window);
    // 当某个服务在窗口里的统计全退成 0 后，把它从窗口态里清掉，避免空服务长期挂着。
    void PruneWindowServiceLocked(const std::string& service_name);
    // 全局操作统计同理，退窗后如果次数和耗时都归零，就把这条 operation 清掉。
    void PruneGlobalOperationLocked(const std::string& global_key);
    // 这里只负责把“窗口累计态 + 最近态”裁成前端 JSON 结构，不再修改内部状态。
    ServiceRuntimeSnapshot BuildSnapshotLocked() const;

    mutable std::mutex mutex_;
    size_t service_top_k_ = 4;
    size_t operation_top_k_ = 6;
    size_t recent_sample_limit_ = 3;
    size_t window_minutes_ = 30;
    MonotonicNowMsFn monotonic_now_ms_fn_;

    // active/sealed 共同描述分钟推进位置：
    // active 表示当前还允许写 observation 的分钟，
    // sealed 表示最后一个已经被并进窗口累计态的分钟。
    int64_t active_minute_ = 0;
    int64_t sealed_minute_ = -1;
    // buckets_ 只存“某一分钟新发生了什么”的增量，不直接存前端视图。
    std::vector<MinuteBucket> buckets_;

    // abnormal_trace_count_、window_services_、global_operations_ 共同组成“当前窗口累计态”。
    // 这些字段会随着 OnTick 的进窗/退窗一起增减，是服务榜和 overview 的真相来源。
    uint64_t abnormal_trace_count_ = 0;
    // latest_exception_time_ms_ 和 recent_services_ 保留“最近态”语义，不做退窗。
    int64_t latest_exception_time_ms_ = 0;
    std::unordered_map<std::string, WindowServiceState> window_services_;
    std::unordered_map<std::string, RecentServiceState> recent_services_;
    std::unordered_map<std::string, GlobalOperationState> global_operations_;
    ServiceRuntimeSnapshot published_snapshot_;
};
