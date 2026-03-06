#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/TokenEstimator.h"
#include "ai/AiTypes.h"
#include "persistence/TraceRepository.h"

class ThreadPool;
class TraceRepository;
class TraceAiProvider;
class INotifier;

struct SpanEvent
{
    // trace_key 与 span_id 先作为最小占位字段，保证接口能承载后续的聚合需求。
    size_t trace_key = 0;
    size_t span_id = 0;
    // parent_span_id 缺失时代表 root span，用 optional 避免 0 与真实值混淆。
    std::optional<size_t> parent_span_id;
    // start_time 采用 Unix 毫秒时间戳，便于跨语言与存储层统一对齐。
    int64_t start_time_ms = 0;
    // end_time 缺失表示 span 尚未结束或上报不完整，使用 optional 以保留语义。
    std::optional<int64_t> end_time;
    // name 与 service_name 是聚合与可视化的核心维度，先作为必填字段保留。
    std::string name;
    std::string service_name;
    // status 只区分三种状态，与 OpenTelemetry SpanStatus 对齐：UNSET（未显式标记成功或失败）、OK（成功）、ERROR（失败）。
    enum class Status
    {
        Unset,
        Ok,
        Error
    };
    // status 是可选字段，未提供时说明未显式标记成功或失败。
    std::optional<Status> status;
    // kind 描述 span 的角色类型，与 OpenTelemetry SpanKind 对齐，未提供表示未知或未标注。
    enum class Kind
    {
        Internal,
        Server,
        Client,
        Producer,
        Consumer
    };
    std::optional<Kind> kind;
    // trace_end 作为显式结束标志，便于在未构建树前触发提前分发。
    std::optional<bool> trace_end;
    // 扩展字段先用字符串承载，保持解析与落库路径简单，后续可升级为 variant。
    std::unordered_map<std::string, std::string> attributes;
};

struct TraceSession
{
    enum class LifecycleState
    {
        // 仍处于 span 收集阶段，时间轮语义是“等后续 span 是否继续到达”。
        Collecting,
        // 业务上已经 ready，但由于下游拥堵暂未成功投递，后续应走重试投递语义。
        ReadyRetryLater
    };

    // capacity 作为每条 Trace 的最大 span 容量，用于触发提前分发。
    explicit TraceSession(size_t capacity);
    // 先用 size_t 作为 trace_id 的紧凑标识，减少基础结构的负担，后续再视需求调整为原始 ID。
    size_t trace_key = 0;
    size_t capacity = 0;
    // // token_limit 作为单条 Trace 的 token 上限占位，用于后续触发提前分发。
    // size_t token_limit = 0;
    // token_count 作为累计 token 计数占位，后续由 TokenEstimator 驱动更新。
    size_t token_count = 0;
    // spans 采用到达顺序存储，便于后续批量遍历与一次性序列化。
    std::vector<SpanEvent> spans;
    // span_ids 用于去重与快速检测异常输入，避免重复 span 破坏聚合逻辑。
    std::unordered_set<size_t> span_ids;
    // duplicate_span_id 记录首个重复 span_id，便于后续告警或异常处理。
    std::optional<size_t> duplicate_span_id;
    // 时间戳用于“超时分发”判定：trace 长时间未补齐时也能被强制刷盘。
    int64_t created_at_ms = 0;
    int64_t last_update_ms = 0;
    // timer_version 用于时间轮懒删除：每次“续命重排”就递增，旧槽节点会自然失效。
    uint64_t timer_version = 0;
    // session_epoch 用于防止 trace_key 复用误命中旧节点。
    uint64_t session_epoch = 0;
    // lifecycle_state 用来区分“仍在收集”和“已 ready 但等待重投”，避免复用同一套超时语义。
    LifecycleState lifecycle_state = LifecycleState::Collecting;
};

class TraceSessionManager
{
public:
    enum class PushResult
    {
        // 正常收下当前 span，请求层可以返回 202。
        Accepted,
        // 入口因过载拒绝当前请求，请求层应返回 503/429 并提示稍后重试。
        RejectedOverload,
        // 当前 span 已收下，但 ready trace 暂未成功投递，下游会在服务端内部延后重试。
        AcceptedDeferred
    };

    enum class OverloadState
    {
        // 所有指标都处于安全区，请求按正常路径放行。
        Normal,
        // 已进入过载区，优先拒绝新 trace，尽量保留老 trace 的完整性。
        Overload,
        // 已接近硬上限，连老 trace 也允许拒绝，优先保护进程存活。
        Critical
    };

    explicit TraceSessionManager(ThreadPool* thread_pool,
                                 TraceRepository* trace_repo,
                                 TraceAiProvider* trace_ai,
                                 size_t capacity,
                                 size_t token_limit,
                                 INotifier* notifier = nullptr,
                                 int64_t idle_timeout_ms = 5000,
                                 int64_t wheel_tick_ms = 500,
                                 size_t wheel_size = 512,
                                 size_t buffered_span_hard_limit = 4096,
                                 size_t active_session_hard_limit = 1024);
    ~TraceSessionManager();

    size_t size() const;
    PushResult Push(const SpanEvent& span);
    void Dispatch(size_t trace_key);
    // 由 EventLoop 定期调用，扫描长时间未更新的 session 并触发分发。
    // now_ms 使用 steady_clock 毫秒时间戳，idle_timeout_ms<=0 表示关闭。
    // max_dispatch_per_tick=0 表示不限制本轮分发数量。
    void SweepExpiredSessions(int64_t now_ms, int64_t idle_timeout_ms, size_t max_dispatch_per_tick = 0);

private:
    // 为了单元测试验证内部索引与序列化逻辑，开放特定测试友元访问，避免引入仅测试用途的公共接口。
    friend class TraceSessionManagerTest_BuildTraceIndexRootsAndChildren_Test;
    friend class TraceSessionManagerTest_SerializeTraceSortsChildrenByStartTime_Test;

    ThreadPool* thread_pool_ = nullptr;
    TraceRepository* trace_repo_ = nullptr;
    TraceAiProvider* trace_ai_ = nullptr;
    INotifier* notifier_ = nullptr;
    size_t capacity_ = 0;
    size_t token_limit_ = 0;
    TokenEstimator token_estimator_;

    struct TraceIndex
    {
        std::unordered_map<size_t, const SpanEvent*> span_map;
        std::unordered_map<size_t, std::vector<size_t>> children;
        std::vector<size_t> roots;
    };
    struct TimeWheelNode
    {
        // trace_key + version + epoch 共同定位“当前有效超时计划”。
        size_t trace_key = 0;
        uint64_t version = 0;
        uint64_t epoch = 0;
        uint64_t expire_tick = 0;
    };
    struct Watermark
    {
        size_t low = 0;
        size_t high = 0;
        size_t critical = 0;
    };

    // 构建 trace 的父子关系索引，后续用于树形遍历与序列化。
    TraceIndex BuildTraceIndex(const TraceSession& session);
    // 将 trace 按树形结构序列化为可传递的字符串，同时产出 DFS 顺序缓存。
    std::string SerializeTrace(const TraceIndex& index, std::vector<const SpanEvent*>* order);

    // 将复杂组装逻辑拆分为独立步骤，避免在 Dispatch 中堆叠细节，便于单测与迭代。
    TraceRepository::TraceSummary BuildTraceSummary(const TraceSession& session,
                                                    const std::vector<const SpanEvent*>& order);
    std::vector<TraceRepository::TraceSpanRecord> BuildSpanRecords(const std::vector<const SpanEvent*>& order);
    TraceRepository::TraceAnalysisRecord BuildAnalysisRecord(const std::string& trace_id,
                                                             const LogAnalysisResult& analysis);
    // 计算当前 idle_timeout 对应的 tick 数；至少返回 1，避免 0 tick 导致不触发。
    uint64_t ComputeTimeoutTicks() const;
    // 按 hard limit 预计算 low/high/critical 三档阈值，构造期缓存后供准入门禁直接读取。
    static Watermark BuildWatermark(size_t hard_limit);
    // 基于当前积压指标刷新 overload_state_，统一收口新老 trace 的准入门禁状态。
    void RefreshOverloadState();
    // 当前请求是否应该在入口被拒绝：Overload 拒新 trace，Critical 新老都拒。
    bool ShouldRejectIncomingTrace(bool trace_exists) const;
    // 在时间轮中为会话安排最新超时节点（旧节点不删，靠 version/epoch 失效）。
    void ScheduleTimeoutNode(TraceSession& session);
    // timeout 参数变化时重建时间轮，避免旧参数下的节点继续误导触发时机。
    void RebuildTimeWheel();
    // 使用 unique_ptr 保证对象地址稳定，后续可安全转移所有权给线程池处理。
    std::vector<std::unique_ptr<TraceSession>> sessions_;
    // 通过 trace_key 快速定位到 vector 下标，避免线性扫描带来的开销。
    std::unordered_map<size_t, size_t> index_by_trace_;
    // 时间轮主状态：按槽位存储超时候选节点。
    std::vector<std::vector<TimeWheelNode>> time_wheel_;
    size_t wheel_size_ = 512;
    int64_t idle_timeout_ms_ = 5000;
    int64_t wheel_tick_ms_ = 500;
    uint64_t timeout_ticks_ = 10;
    uint64_t current_tick_ = 0;
    int64_t last_tick_now_ms_ = 0;
    uint64_t session_epoch_seq_ = 0;
    // active_sessions_ 与 total_buffered_spans_ 直接反映入口聚合态积压，用于实时背压门禁。
    size_t active_sessions_ = 0;
    size_t total_buffered_spans_ = 0;
    // 第一版先硬编码接入，后续再迁移到配置层；这里存每个指标自己的硬上限基数。
    size_t buffered_span_hard_limit_ = 4096;
    size_t active_session_hard_limit_ = 1024;
    // watermark_ 在构造期预计算，避免每次 Push/Dispatch 都重复按比例换算阈值。
    Watermark buffered_span_watermark_;
    Watermark active_session_watermark_;
    Watermark pending_task_watermark_;
    // overload_state_ 先作为背压状态机占位，后续由多指标水位共同驱动。
    OverloadState overload_state_ = OverloadState::Normal;
};
