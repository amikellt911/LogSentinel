#include "core/TraceSessionManager.h"
#include "threadpool/ThreadPool.h"
#include "ai/TraceAiProvider.h"
#include "persistence/TraceRepository.h"
#include "notification/INotifier.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <limits>
#include <nlohmann/json.hpp>

namespace
{
std::string toLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

int64_t NowSteadyMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
}

TraceSession::TraceSession(size_t capacity)
    : capacity(capacity)
{
    spans.reserve(capacity);
}

TraceSessionManager::TraceSessionManager(ThreadPool* thread_pool,
                                         TraceRepository* trace_repo,
                                         TraceAiProvider* trace_ai,
                                         size_t capacity,
                                         size_t token_limit,
                                         INotifier* notifier,
                                         int64_t idle_timeout_ms,
                                         int64_t wheel_tick_ms,
                                         size_t wheel_size)
    : thread_pool_(thread_pool)
    , trace_repo_(trace_repo)
    , trace_ai_(trace_ai)
    , notifier_(notifier)
    , capacity_(capacity)
    , token_limit_(token_limit)
    , wheel_size_(wheel_size > 0 ? wheel_size : 512)
    , idle_timeout_ms_(idle_timeout_ms > 0 ? idle_timeout_ms : 5000)
    , wheel_tick_ms_(wheel_tick_ms > 0 ? wheel_tick_ms : 500)
{
    timeout_ticks_ = ComputeTimeoutTicks();
    time_wheel_.resize(wheel_size_);
}

TraceSessionManager::~TraceSessionManager() = default;

size_t TraceSessionManager::size() const
{
    return sessions_.size();
}

bool TraceSessionManager::Push(const SpanEvent& span)
{
    const int64_t now_ms = NowSteadyMs();
    auto iter = index_by_trace_.find(span.trace_key);
    if (iter == index_by_trace_.end()) {
        auto session = std::make_unique<TraceSession>(capacity_);
        session->trace_key = span.trace_key;
        session->created_at_ms = now_ms;
        session->last_update_ms = now_ms;
        session->session_epoch = ++session_epoch_seq_;
        sessions_.push_back(std::move(session));
        index_by_trace_[span.trace_key] = sessions_.size() - 1;
        iter = index_by_trace_.find(span.trace_key);
    }

    TraceSession& session = *sessions_[iter->second];
    if (!session.span_ids.insert(span.span_id).second) {
        // 发现重复 span_id 时先记录首个重复值，便于后续分发时标注异常来源。
        if (!session.duplicate_span_id.has_value()) {
            session.duplicate_span_id = span.span_id;
        }
        Dispatch(session.trace_key);
        return true;
    }

    // 先按到达顺序追加，后续聚合阶段再按 parent_id 重建结构。
    session.spans.push_back(span);
    session.token_count += token_estimator_.Estimate(span);
    session.last_update_ms = now_ms;

    if (span.trace_end.has_value() && span.trace_end.value()) {
        Dispatch(session.trace_key);
        return true;
    }

    if (token_limit_ > 0 && session.token_count >= token_limit_) {
        Dispatch(session.trace_key);
        return true;
    }

    if (session.capacity > 0 && session.spans.size() >= session.capacity) {
        // 达到容量上限即触发分发，避免单个 Trace 无限膨胀。
        Dispatch(session.trace_key);
        return true;
    }

    // 正常留在内存中的会话，需要更新“下一次超时计划”。
    // 这里采用懒删除策略：不回删旧槽，只给会话 version+1 并插入新节点。
    ScheduleTimeoutNode(session);

    // 当前仅做本地写入与分发占位，先返回成功；后续接入线程池/存储失败时再返回 false。
    return true;
}

void TraceSessionManager::SweepExpiredSessions(int64_t now_ms,
                                               int64_t idle_timeout_ms,
                                               size_t max_dispatch_per_tick)
{
    if (sessions_.empty()) {
        return;
    }
    if (idle_timeout_ms > 0 && idle_timeout_ms != idle_timeout_ms_) {
        // 运行中调整超时策略时重建时间轮，避免继续使用旧超时计划。
        idle_timeout_ms_ = idle_timeout_ms;
        timeout_ticks_ = ComputeTimeoutTicks();
        RebuildTimeWheel();
    }

    if (last_tick_now_ms_ == 0) {
        last_tick_now_ms_ = now_ms;
    }
    int64_t elapsed_ms = now_ms - last_tick_now_ms_;
    uint64_t advance_ticks = 1;
    if (elapsed_ms > 0 && wheel_tick_ms_ > 0) {
        advance_ticks = static_cast<uint64_t>(elapsed_ms / wheel_tick_ms_);
        if (advance_ticks == 0) {
            advance_ticks = 1;
        }
    }
    // 防止长时间阻塞后一次性补 tick 过多导致本轮回调卡死。
    if (advance_ticks > wheel_size_) {
        advance_ticks = wheel_size_;
    }
    last_tick_now_ms_ = now_ms;

    std::vector<size_t> expired_trace_keys;
    expired_trace_keys.reserve(max_dispatch_per_tick > 0 ? max_dispatch_per_tick : sessions_.size());
    std::unordered_set<size_t> scheduled_once;
    scheduled_once.reserve(expired_trace_keys.capacity() > 0 ? expired_trace_keys.capacity() : 8);

    for (uint64_t step = 0; step < advance_ticks; ++step) {
        ++current_tick_;
        const size_t slot = static_cast<size_t>(current_tick_ % wheel_size_);
        std::vector<TimeWheelNode> bucket = std::move(time_wheel_[slot]);
        time_wheel_[slot].clear();

        for (auto& node : bucket) {
            auto idx_iter = index_by_trace_.find(node.trace_key);
            if (idx_iter == index_by_trace_.end()) {
                // 会话已分发并从内存移除，旧节点自然失效。
                continue;
            }
            TraceSession& session = *sessions_[idx_iter->second];
            if (session.session_epoch != node.epoch || session.timer_version != node.version) {
                // 非当前版本节点（旧计划）直接丢弃。
                continue;
            }
            if (node.expire_tick > current_tick_) {
                // 补 tick 场景下，如果还没到期，放回目标槽等待后续 tick。
                time_wheel_[node.expire_tick % wheel_size_].push_back(node);
                continue;
            }
            if (max_dispatch_per_tick > 0 && expired_trace_keys.size() >= max_dispatch_per_tick) {
                // 本轮达到上限时，将当前有效节点顺延一 tick，避免被直接丢失。
                node.expire_tick = current_tick_ + 1;
                time_wheel_[node.expire_tick % wheel_size_].push_back(node);
                continue;
            }
            if (scheduled_once.insert(node.trace_key).second) {
                expired_trace_keys.push_back(node.trace_key);
            }
        }
    }

    for (size_t trace_key : expired_trace_keys) {
        Dispatch(trace_key);
    }
}

uint64_t TraceSessionManager::ComputeTimeoutTicks() const
{
    if (idle_timeout_ms_ <= 0 || wheel_tick_ms_ <= 0) {
        return 1;
    }
    return static_cast<uint64_t>((idle_timeout_ms_ + wheel_tick_ms_ - 1) / wheel_tick_ms_);
}

void TraceSessionManager::ScheduleTimeoutNode(TraceSession& session)
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

void TraceSessionManager::RebuildTimeWheel()
{
    for (auto& bucket : time_wheel_) {
        bucket.clear();
    }
    for (auto& session_ptr : sessions_) {
        if (!session_ptr) {
            continue;
        }
        // 配置变更后统一重排超时节点，避免继续沿用旧超时参数。
        ScheduleTimeoutNode(*session_ptr);
    }
}

void TraceSessionManager::Dispatch(size_t trace_key)
{
    auto iter = index_by_trace_.find(trace_key);
    if (iter == index_by_trace_.end()) {
        return;
    }

    const size_t index = iter->second;
    std::unique_ptr<TraceSession> session = std::move(sessions_[index]);
    index_by_trace_.erase(iter);

    if (index < sessions_.size() - 1) {
        // swap+pop_back：用最后一个元素覆盖被移除位置，保持容器紧凑并避免线性搬移。
        sessions_[index] = std::move(sessions_.back());
        sessions_.pop_back();
        index_by_trace_[sessions_[index]->trace_key] = index;
    } else {
        sessions_.pop_back();
    }

    if (!thread_pool_) {
        return;
    }

    // ThreadPool 的任务类型是 std::function，要求可拷贝，因此先转为 shared_ptr 以便捕获。
    std::shared_ptr<TraceSession> session_shared = std::move(session);
    TraceSessionManager* manager = this;
    TraceRepository* trace_repo = trace_repo_;
    TraceAiProvider* trace_ai = trace_ai_;
    INotifier* notifier = notifier_;
    thread_pool_->submit([manager, trace_repo, trace_ai, notifier, session_shared]() {
        if (!manager || !trace_repo) {
            return;
        }

        TraceSessionManager::TraceIndex index = manager->BuildTraceIndex(*session_shared);
        std::vector<const SpanEvent*> order;
        std::string trace_payload = manager->SerializeTrace(index, &order);
        // 说明：SerializeTrace 在检测到环时会把异常写入 payload.anomalies。
        // 这份异常信息当前只通过 trace_payload 传给 AI 分析链路，不会单独落到 trace_span 表中。

        TraceRepository::TraceSummary summary =
            manager->BuildTraceSummary(*session_shared, order);
        std::vector<TraceRepository::TraceSpanRecord> span_records =
            manager->BuildSpanRecords(order);

        TraceRepository::TraceAnalysisRecord analysis_record;
        TraceRepository::TraceAnalysisRecord* analysis_ptr = nullptr;
        if (trace_ai) {
            try {
                LogAnalysisResult analysis = trace_ai->AnalyzeTrace(trace_payload);
                analysis_record = manager->BuildAnalysisRecord(summary.trace_id, analysis);
                analysis_ptr = &analysis_record;
            } catch (const std::exception& e) {
                // AI 调用失败时写入失败态 analysis，保证“失败可见”，便于后续在业务侧触发重试。
                analysis_record.trace_id = summary.trace_id;
                analysis_record.risk_level = "unknown";
                analysis_record.summary = "AI_ANALYSIS_FAILED";
                analysis_record.root_cause = e.what();
                analysis_record.solution = "请检查 AI 代理与模型服务状态，稍后重试该 trace 的分析。";
                analysis_record.confidence = 0.0;
                analysis_ptr = &analysis_record;
            } catch (...) {
                // 非标准异常同样写入失败态，避免出现“无分析记录”的黑盒状态。
                analysis_record.trace_id = summary.trace_id;
                analysis_record.risk_level = "unknown";
                analysis_record.summary = "AI_ANALYSIS_FAILED";
                analysis_record.root_cause = "Unknown non-std exception";
                analysis_record.solution = "请检查 AI 代理与模型服务状态，稍后重试该 trace 的分析。";
                analysis_record.confidence = 0.0;
                analysis_ptr = &analysis_record;
            }
        }

        // prompt_debug 现状说明：
        // 1) 持久层已具备 prompt_debug 表与 SaveSinglePromptDebug/Atomic 写入能力；
        // 2) 当前先不在主链路写入（传 nullptr），避免在 MVP 阶段引入额外字段组装复杂度；
        // 3) 后续做“分析重试”时，优先把当次 trace_payload 作为 input_json 落到 prompt_debug，
        //    这样可直接重放请求，不需要再次从 trace_span 重新组装树结构。
        bool saved = trace_repo->SaveSingleTraceAtomic(summary, span_records, analysis_ptr, nullptr);
        if (!saved || !notifier || !analysis_ptr) {
            return;
        }

        const std::string risk_level = toLowerCopy(analysis_ptr->risk_level);
        // 告警策略放在聚合调度层，当前仅对 critical 发通知，避免 notifier 混入业务策略判断。
        if (risk_level != "critical") {
            return;
        }

        TraceAlertEvent event;
        event.trace_id = summary.trace_id;
        event.service_name = summary.service_name;
        event.start_time_ms = summary.start_time_ms;
        event.duration_ms = summary.duration_ms;
        event.span_count = summary.span_count;
        event.token_count = summary.token_count;
        event.risk_level = analysis_ptr->risk_level;
        event.summary = analysis_ptr->summary;
        event.root_cause = analysis_ptr->root_cause;
        event.solution = analysis_ptr->solution;
        event.confidence = analysis_ptr->confidence;
        notifier->notifyTraceAlert(event);
    });
}

TraceSessionManager::TraceIndex TraceSessionManager::BuildTraceIndex(const TraceSession& session)
{
    TraceIndex index;
    index.span_map.reserve(session.spans.size());

    for (const auto& span : session.spans) {
        index.span_map[span.span_id] = &span;
    }

    for (const auto& span : session.spans) {
        if (span.parent_span_id.has_value()) {
            const size_t parent_id = span.parent_span_id.value();
            if (index.span_map.find(parent_id) != index.span_map.end()) {
                index.children[parent_id].push_back(span.span_id);
                continue;
            }
        }
        // 找不到父节点或没有 parent_id 时，视为 root。
        index.roots.push_back(span.span_id);
    }

    return index;
}

std::string TraceSessionManager::SerializeTrace(const TraceIndex& index, std::vector<const SpanEvent*>* order)
{
    nlohmann::json output;
    std::unordered_set<size_t> visited;
    visited.reserve(index.span_map.size());
    bool has_cycle = false;
    std::vector<size_t> cycle_spans;

    auto build_node = [&index, order, &visited, &has_cycle, &cycle_spans](size_t span_id, const auto& self_ref) -> nlohmann::json {
        nlohmann::json node;
        if (!visited.insert(span_id).second) {
            // 发现环时仅记录异常，避免继续递归导致无限循环。
            has_cycle = true;
            cycle_spans.push_back(span_id);
            return node;
        }
        auto span_iter = index.span_map.find(span_id);
        if (span_iter == index.span_map.end()) {
            return node;
        }
        const SpanEvent& span = *span_iter->second;
        if (order) {
            order->push_back(&span);
        }

        node["trace_id"] = span.trace_key;
        node["span_id"] = span.span_id;
        if (span.parent_span_id.has_value()) {
            node["parent_id"] = span.parent_span_id.value();
        } else {
            node["parent_id"] = nullptr;
        }
        node["name"] = span.name;
        node["service_name"] = span.service_name;
        node["start_time_ms"] = span.start_time_ms;
        if (span.end_time.has_value()) {
            node["end_time_ms"] = span.end_time.value();
        } else {
            node["end_time_ms"] = nullptr;
        }

        if (span.status.has_value()) {
            switch (span.status.value()) {
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
        } else {
            node["status"] = nullptr;
        }

        if (span.kind.has_value()) {
            switch (span.kind.value()) {
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
        } else {
            node["kind"] = nullptr;
        }

        node["attributes"] = span.attributes;

        auto child_iter = index.children.find(span_id);
        const std::vector<size_t>* children_ids = nullptr;
        if (child_iter != index.children.end()) {
            children_ids = &child_iter->second;
        }

        node["children"] = nlohmann::json::array();
        if (children_ids) {
            std::vector<size_t> ordered_children = *children_ids;
            std::sort(ordered_children.begin(), ordered_children.end(), [&index](size_t left, size_t right) {
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
                return left_span.span_id < right_span.span_id;
            });
            for (size_t child_id : ordered_children) {
                nlohmann::json child_node = self_ref(child_id, self_ref);
                if (!child_node.is_null() && !child_node.empty()) {
                    node["children"].push_back(std::move(child_node));
                }
            }
        }

        return node;
    };

    output["spans"] = nlohmann::json::array();
    for (size_t root_id : index.roots) {
        nlohmann::json root_node = build_node(root_id, build_node);
        if (!root_node.is_null() && !root_node.empty()) {
            output["spans"].push_back(std::move(root_node));
        }
    }
    if (has_cycle) {
        // 记录位置（第一处）：
        // 1) 异常会写入当前序列化结果 output["anomalies"]，并随 trace_payload 继续流转。
        // 2) 当前实现不单独持久化 anomalies 表，因此排障时应优先看 AI 输入/日志中的 payload 内容。
        //因为有环所以没有root所以就是dfs根本不会跑，导致order为空，ai无法具有分析性
        nlohmann::json anomalies = nlohmann::json::array();
        for (size_t span_id : cycle_spans) {
            nlohmann::json item;
            item["type"] = "cycle_detected";
            item["span_id"] = span_id;
            anomalies.push_back(std::move(item));
        }
        output["anomalies"] = std::move(anomalies);
    }

    return output.dump();
}

TraceRepository::TraceSummary TraceSessionManager::BuildTraceSummary(const TraceSession& session,
                                                                     const std::vector<const SpanEvent*>& order)
{
    TraceRepository::TraceSummary summary;
    summary.trace_id = std::to_string(session.trace_key);
    summary.service_name = order.empty()
                               ? (session.spans.empty() ? "" : session.spans.front().service_name)
                               : order.front()->service_name;

    int64_t min_start = std::numeric_limits<int64_t>::max();
    int64_t max_end = std::numeric_limits<int64_t>::min();
    bool has_end = false;
    for (const auto& span : session.spans) {
        min_start = std::min(min_start, span.start_time_ms);
        if (span.end_time.has_value()) {
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
    const std::vector<const SpanEvent*>& order)
{
    std::vector<TraceRepository::TraceSpanRecord> span_records;
    span_records.reserve(order.size());

    for (const SpanEvent* span_ptr : order) {
        if (!span_ptr) {
            continue;
        }
        const SpanEvent& span = *span_ptr;
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

        if (!span.status.has_value()) {
            record.status = "UNSET";
        } else {
            switch (span.status.value()) {
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

TraceRepository::TraceAnalysisRecord TraceSessionManager::BuildAnalysisRecord(const std::string& trace_id,
                                                                              const LogAnalysisResult& analysis)
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
