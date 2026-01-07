#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ThreadPool;

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
    // 扩展字段先用字符串承载，保持解析与落库路径简单，后续可升级为 variant。
    std::unordered_map<std::string, std::string> attributes;
};

struct TraceSession
{
    // capacity 作为每条 Trace 的最大 span 容量，用于触发提前分发。
    explicit TraceSession(size_t capacity);
    // 先用 size_t 作为 trace_id 的紧凑标识，减少基础结构的负担，后续再视需求调整为原始 ID。
    size_t trace_key = 0;
    size_t capacity = 0;
    // spans 采用到达顺序存储，便于后续批量遍历与一次性序列化。
    std::vector<SpanEvent> spans;
    // span_ids 用于去重与快速检测异常输入，避免重复 span 破坏聚合逻辑。
    std::unordered_set<size_t> span_ids;
    // duplicate_span_id 记录首个重复 span_id，便于后续告警或异常处理。
    std::optional<size_t> duplicate_span_id;
};

class TraceSessionManager
{
public:
    explicit TraceSessionManager(ThreadPool* thread_pool, size_t capacity);
    ~TraceSessionManager();

    size_t size() const;
    void Push(const SpanEvent& span);
    void Dispatch(size_t trace_key);

private:
    ThreadPool* thread_pool_ = nullptr;
    size_t capacity_ = 0;

    struct TraceIndex
    {
        std::unordered_map<size_t, const SpanEvent*> span_map;
        std::unordered_map<size_t, std::vector<size_t>> children;
        std::vector<size_t> roots;
    };

    // 构建 trace 的父子关系索引，后续用于树形遍历与序列化。
    TraceIndex BuildTraceIndex(const TraceSession& session);
    // 将 trace 按树形结构序列化为可传递的字符串，同时产出 DFS 顺序缓存。
    std::string SerializeTrace(const TraceIndex& index, std::vector<const SpanEvent*>* order);
    // 使用 unique_ptr 保证对象地址稳定，后续可安全转移所有权给线程池处理。
    std::vector<std::unique_ptr<TraceSession>> sessions_;
    // 通过 trace_key 快速定位到 vector 下标，避免线性扫描带来的开销。
    std::unordered_map<size_t, size_t> index_by_trace_;
};
