#include "core/TraceSessionManager.h"
#include "threadpool/ThreadPool.h"
#include "ai/TraceAiProvider.h"
#include "persistence/TraceRepository.h"
#include "notification/INotifier.h"
#include <algorithm>
#include <cctype>
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
                                         INotifier* notifier)
    : thread_pool_(thread_pool)
    , trace_repo_(trace_repo)
    , trace_ai_(trace_ai)
    , notifier_(notifier)
    , capacity_(capacity)
    , token_limit_(token_limit)
{
}

TraceSessionManager::~TraceSessionManager() = default;

size_t TraceSessionManager::size() const
{
    return sessions_.size();
}

bool TraceSessionManager::Push(const SpanEvent& span)
{
    auto iter = index_by_trace_.find(span.trace_key);
    if (iter == index_by_trace_.end()) {
        auto session = std::make_unique<TraceSession>(capacity_);
        session->trace_key = span.trace_key;
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
    }

    // 当前仅做本地写入与分发占位，先返回成功；后续接入线程池/存储失败时再返回 false。
    return true;
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
            LogAnalysisResult analysis = trace_ai->AnalyzeTrace(trace_payload);
            analysis_record = manager->BuildAnalysisRecord(summary.trace_id, analysis);
            analysis_ptr = &analysis_record;
        }

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
