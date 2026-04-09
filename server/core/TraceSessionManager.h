#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/TokenEstimator.h"
#include "ai/AiTypes.h"
#include "persistence/TraceRepository.h"

class ThreadPool;
class BufferedTraceRepository;
class TraceAiProvider;
class INotifier;
class ServiceRuntimeAccumulator;
class SystemRuntimeAccumulator;

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
    enum class SealReason
    {
        // 显式 trace_end：更像“最后一个包可能先到”，允许给一个略长的乱序缓冲窗。
        TraceEnd,
        // 容量打满：目的是及时截断内存增长，所以只给最短 grace。
        Capacity,
        // token 打满：和容量一样，属于保护性封口，不应该再等太久。
        TokenLimit,
        // 发送重复span
        DuplicateSpan,
    };

    enum class LifecycleState
    {
        // 仍处于 span 收集阶段，时间轮语义是“等后续 span 是否继续到达”。
        Collecting,
        // 已命中 trace_end/capacity/token_limit，接下来只再等一个很短的乱序窗口。
        Sealed,
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
    // seal_reason 记录本次封口由谁触发，后面时间轮调度要按 reason 决定 grace 长短。
    SealReason seal_reason = SealReason::TraceEnd;
    // sealed_deadline_tick 是 sealed 会话真正允许 dispatch 的最晚 tick。
    // 注意它不是“每来一个 span 就续命”的 timeout，而是一个固定短窗口。
    uint64_t sealed_deadline_tick = 0;
    // retry_count 只统计连续 submit 失败次数，用于计算指数退避的下次重试间隔。
    size_t retry_count = 0;
    // next_retry_tick 表示 ready trace 下一次最早允许重试投递的 tick。
    uint64_t next_retry_tick = 0;
    // 主数据（summary + spans）一旦已经送进缓冲写入器，submit 失败回滚后就不能重复送。
    bool primary_enqueued = false;
    // 第一步先只缓存最值钱的两样：AI 输入 payload 和 webhook/analysis 还会复用的 summary。
    // 既然 ready retry 会话已经不再吸收新 span，那么这两份 prepared 数据可以安全复用。
    std::optional<std::string> prepared_trace_payload;
    std::optional<TraceRepository::TraceSummary> prepared_summary;
};

class TraceSessionManager
{
public:
    struct RuntimeStatsSnapshot
    {
        uint64_t dispatch_count = 0;
        uint64_t submit_ok_count = 0;
        uint64_t submit_fail_count = 0;
        uint64_t worker_begin_count = 0;
        uint64_t worker_done_count = 0;
        uint64_t ai_calls = 0;
        uint64_t ai_total_ns = 0;
        uint64_t analysis_enqueue_calls = 0;
        uint64_t analysis_enqueue_total_ns = 0;
    };

    enum class PushResult
    {
        // 正常收下当前 span，请求层可以返回 202。
        Accepted,
        // 入口因过载拒绝当前请求，请求层应返回 503/429 并提示稍后重试。
        RejectedOverload,
        // 核心异步依赖缺失（如线程池不可用），请求层应返回 503，避免误报 accepted。
        RejectedUnavailable,
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
                                 BufferedTraceRepository* buffered_trace_repo,
                                 TraceAiProvider* trace_ai,
                                 size_t capacity,
                                 size_t token_limit,
                                 INotifier* notifier = nullptr,
                                 int64_t idle_timeout_ms = 5000,
                                 int64_t wheel_tick_ms = 500,
                                 // sealed_grace_window_ms 控制 session 命中 trace_end/capacity/token_limit
                                 // 之后还能继续吸收 late span 的短窗口；当前先统一成一个值，不再按 reason 分多档。
                                 int64_t sealed_grace_window_ms = 1000,
                                 // retry_base_delay_ms 是 dispatch 失败后的第一档重试等待时间，
                                 // 后续连续失败仍然沿用指数退避，只是把“起始 tick”改成配置驱动。
                                 int64_t retry_base_delay_ms = 500,
                                 size_t wheel_size = 512,
                                 size_t buffered_span_hard_limit = 4096,
                                 size_t active_session_hard_limit = 1024,
                                 // 三组水位阈值当前只开放 overload/critical 两档百分比；
                                 // low 不单独暴露给 Settings，而是由内部自动派生一个更低的回滞阈值。
                                 int active_session_overload_percent = 75,
                                 int active_session_critical_percent = 90,
                                 int buffered_spans_overload_percent = 75,
                                 int buffered_spans_critical_percent = 90,
                                 int pending_tasks_overload_percent = 75,
                                 int pending_tasks_critical_percent = 90,
                                 ServiceRuntimeAccumulator* service_runtime_accumulator = nullptr,
                                 // system_runtime_accumulator 只负责系统运行态埋点，
                                 // 不参与 Trace 聚合/分发语义判断，所以和服务监控累加器一样保持可选注入。
                                 SystemRuntimeAccumulator* system_runtime_accumulator = nullptr,
                                 // ai_analysis_enabled 是“用户主动允许不允许”这层总开关。
                                 // 关闭后 worker 仍要正常收尾，只是不再调用模型，而是把 ai_status 标成 skipped_manual。
                                 bool ai_analysis_enabled = true);
    ~TraceSessionManager();

    size_t size() const;
    PushResult Push(const SpanEvent& span);
    // 当前主链路不依赖这个公开 Dispatch 入口。
    // 现在推荐的正常路径是：Push/Seal -> 时间轮 sweep -> dispatch queue -> ProcessDispatchJob。
    // 这里暂时保留，主要是为了兼容少量旧测试/手工触发入口，后续若彻底无调用方再考虑继续收口。
    bool Dispatch(size_t trace_key);
    RuntimeStatsSnapshot SnapshotRuntimeStats() const;
    std::string DescribeRuntimeStats() const;
    // 由 EventLoop 定期调用，扫描长时间未更新的 session 并触发分发。
    // now_ms 使用 steady_clock 毫秒时间戳，idle_timeout_ms<=0 表示关闭。
    // max_dispatch_per_tick=0 表示不限制本轮分发数量。
    void SweepExpiredSessions(int64_t now_ms, int64_t idle_timeout_ms, size_t max_dispatch_per_tick = 0);

private:
    // 为了单元测试验证内部索引与序列化逻辑，开放特定测试友元访问，避免引入仅测试用途的公共接口。
    friend class TraceSessionManagerTest_BuildTraceIndexRootsAndChildren_Test;
    friend class TraceSessionManagerTest_SerializeTraceSortsChildrenByStartTime_Test;

    ThreadPool* thread_pool_ = nullptr;
    BufferedTraceRepository* buffered_trace_repo_ = nullptr;
    TraceAiProvider* trace_ai_ = nullptr;
    INotifier* notifier_ = nullptr;
    ServiceRuntimeAccumulator* service_runtime_accumulator_ = nullptr;
    // 系统监控只吃主链埋点快照，不应该反过来驱动 TraceSessionManager 的状态机。
    SystemRuntimeAccumulator* system_runtime_accumulator_ = nullptr;
    bool ai_analysis_enabled_ = true;
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
    struct DispatchingInflightState
    {
        // inflight 只负责拦截“这条 trace 正在分发中”，第一步先只记最小 epoch，
        // 避免晚到 span 在 session 已摘出、tombstone 还没写入的窗口里把旧 trace 复活。
        uint64_t session_epoch = 0;
    };
    struct DispatchJob
    {
        // 第二步先把 queue 骨架搭起来，job 直接持有 session 所有权，避免后面又回 manager 取一次。
        std::unique_ptr<TraceSession> session;
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
    // 毫秒配置在进入状态机前统一折算成 tick。
    // 既然时间轮真正调度的是 tick 而不是毫秒，那么这里必须向上取整并至少保留 1 tick，
    // 否则配置值小于 wheel_tick_ms_ 时会被截成 0，最终表现成“设置了但完全不生效”。
    uint64_t ComputeDelayTicks(int64_t delay_ms) const;
    // 按 hard limit 和 overload/critical 百分比预计算水位。
    // low 不单独开放配置，而是从 overload 百分比向下退一小段，专门给 back_to_low 回滞使用。
    static Watermark BuildWatermark(size_t hard_limit,
                                    int overload_percent,
                                    int critical_percent);
    // sealed 窗口现在改成配置驱动：所有 seal reason 共用同一个短窗口，
    // 目的是先把 Settings 里的单个 sealed_grace_window_ms 真正落地，后面若要细分 reason 再扩。
    uint64_t ComputeSealDelayTicks(TraceSession::SealReason reason) const;
    // 根据连续失败次数计算退避 tick，起点来自 retry_base_delay_ms 配置。
    // 第一次失败先等 base tick，后面按 2 倍递增，但仍保留一个有限上限避免无限拉长。
    uint64_t ComputeRetryDelayTicks(size_t retry_count) const;
    // completed tombstone 进入 TIME_WAIT：写入精确查找表并挂进独立 wheel，避免晚到 span 复活旧 trace。
    void AddCompletedTombstoneLocked(size_t trace_key);
    // 命中且仍未过期时返回 true；若发现只是 map 里的过期脏数据，会在这里顺手清掉。
    bool IsCompletedTombstoneAliveLocked(size_t trace_key);
    // 扫描当前 tick 的 completed tombstone 桶：到期则删除，未到期则按真实 expire_tick 重新挂回。
    void SweepCompletedTombstonesLocked(size_t slot);
    // Push/Dispatch 的共享状态会同时被 IO loop 和主 loop 定时器线程访问，这里拆出持锁版本，
    // 避免公开入口互相调用时重复加锁导致死锁。
    PushResult PushLocked(const SpanEvent& span, int64_t now_ms);
    bool DispatchLocked(size_t trace_key);
    // 从 manager 主容器中摘出一条 session，后续交给 dispatch 线程锁外处理。
    std::unique_ptr<TraceSession> DetachSessionLocked(size_t trace_key, size_t* span_count);
    // dispatch 失败时把 session 放回 manager，并切到 ReadyRetryLater 语义等待后续重投。
    void RestoreSessionLocked(std::unique_ptr<TraceSession> session, size_t span_count);
    // 有界 dispatch queue 的最小入队入口；第一步先只表达“能否抢到分发通道”。
    bool EnqueueDispatchJobLocked(DispatchJob* job);
    // dispatch 线程消费 job 后继续沿用现有主链路逻辑；后续再逐步拆成更细阶段。
    void ProcessDispatchJob(DispatchJob job);
    // 基于当前积压指标刷新 overload_state_，统一收口新老 trace 的准入门禁状态。
    void RefreshOverloadState();
    // 当前请求是否应该在入口被拒绝：Overload 拒新 trace，Critical 新老都拒。
    bool ShouldRejectIncomingTrace(bool trace_exists) const;
    // 在时间轮中为会话安排最新超时节点（旧节点不删，靠 version/epoch 失效）。
    void ScheduleTimeoutNode(TraceSession& session);
    // sealed 会话使用独立的 deadline tick；后续 late span 可以并入，但不能续命。
    void ScheduleSealedNode(TraceSession& session);
    // ready trace 投递失败后，安排一个更短的“尽快重试”时点，避免继续沿用收集超时语义。
    void ScheduleRetryNode(TraceSession& session);
    // 按会话当前生命周期选择调度语义：Collecting 走收集超时，ReadyRetryLater 走快速重投。
    void ScheduleSessionNode(TraceSession& session);
    // 将 collecting 会话收口到 sealed，后续只等固定短窗口，不再被新 span 延长。
    void SealSessionLocked(TraceSession& session, TraceSession::SealReason reason);
    // timeout 参数变化时重建时间轮，避免旧参数下的节点继续误导触发时机。
    void RebuildTimeWheel();
    // dispatch 线程第一步只做生命周期与队列骨架占位，后面再逐步接业务逻辑。
    void DispatchLoop();
    void StopDispatchThread();
    // 使用 unique_ptr 保证对象地址稳定，后续可安全转移所有权给线程池处理。
    std::vector<std::unique_ptr<TraceSession>> sessions_;
    // 通过 trace_key 快速定位到 vector 下标，避免线性扫描带来的开销。
    std::unordered_map<size_t, size_t> index_by_trace_;
    // 时间轮主状态：按槽位存储超时候选节点。
    std::vector<std::vector<TimeWheelNode>> time_wheel_;
    // completed tombstone 只记“最近刚完成过的 trace_key”，用于短时间内拦截 late span 复活旧 trace。
    // map 负责 O(1) 判断是否仍在 tombstone 窗口内，value 是它的过期 tick。
    std::unordered_map<size_t, uint64_t> completed_trace_expire_tick_;
    // dispatching_inflight_ 记录“已离开 manager、但尚未进入 tombstone”的 trace。
    // 第一步先只做轻量占位，不在这里再次持有 session 所有权，避免状态双持有。
    std::unordered_map<size_t, DispatchingInflightState> dispatching_inflight_;
    // 和活跃 session 时间轮分开存，避免把“等待 dispatch”和“等待遗忘”两套语义塞进同一种节点。
    std::vector<std::vector<size_t>> completed_trace_wheel_;
    size_t wheel_size_ = 512;
    int64_t idle_timeout_ms_ = 5000;
    int64_t wheel_tick_ms_ = 500;
    // 冷启动配置在构造时先换算成 tick，后面状态机只读缓存值，不再每次临时做毫秒到 tick 的折算。
    uint64_t sealed_grace_ticks_ = 2;
    uint64_t retry_base_delay_ticks_ = 1;
    // completed tombstone 默认保留 25 tick；当前 tick=200ms 时大约是 5s。
    uint64_t completed_trace_tombstone_ticks_ = 25;
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
    Watermark dispatch_queue_watermark_;
    size_t dispatch_queue_hard_limit_ = 1;
    // overload_state_ 先作为背压状态机占位，后续由多指标水位共同驱动。
    OverloadState overload_state_ = OverloadState::Normal;
    // 这批统计只服务“后链路是否真的跑了、各阶段墙钟耗时多少”，
    // 不参与业务判断，因此第一版直接用全局原子累加，先把账记清楚。
    std::atomic<uint64_t> dispatch_count_{0};
    std::atomic<uint64_t> submit_ok_count_{0};
    std::atomic<uint64_t> submit_fail_count_{0};
    std::atomic<uint64_t> worker_begin_count_{0};
    std::atomic<uint64_t> worker_done_count_{0};
    std::atomic<uint64_t> ai_calls_{0};
    std::atomic<uint64_t> ai_total_ns_{0};
    std::atomic<uint64_t> analysis_enqueue_calls_{0};
    std::atomic<uint64_t> analysis_enqueue_total_ns_{0};
    // 独立 dispatch 线程的有界队列：第一步先占好结构，后面再把 sweep/dispatch 逐步接过来。
    std::mutex dispatch_queue_mutex_;
    std::condition_variable dispatch_queue_cv_;
    std::queue<DispatchJob> dispatch_queue_;
    bool dispatch_stopping_ = false;
    std::thread dispatch_thread_;
    // TraceSessionManager 当前会被 HTTP 处理线程和主 loop 定时器线程同时访问，
    // 这把锁先用最保守的方式把内部状态机串行化，优先保证正确性。
    mutable std::mutex mutex_;
};
