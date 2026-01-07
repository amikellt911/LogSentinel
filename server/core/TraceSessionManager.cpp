#include "core/TraceSessionManager.h"
#include "threadpool/ThreadPool.h"
#include <algorithm>
#include <nlohmann/json.hpp>

TraceSession::TraceSession(size_t capacity)
    : capacity(capacity)
{
    spans.reserve(capacity);
}

TraceSessionManager::TraceSessionManager(ThreadPool* thread_pool, size_t capacity)
    : thread_pool_(thread_pool)
    , capacity_(capacity)
{
}

TraceSessionManager::~TraceSessionManager() = default;

size_t TraceSessionManager::size() const
{
    return sessions_.size();
}

void TraceSessionManager::Push(const SpanEvent& span)
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
        return;
    }

    // 先按到达顺序追加，后续聚合阶段再按 parent_id 重建结构。
    session.spans.push_back(span);

    if (session.capacity > 0 && session.spans.size() >= session.capacity) {
        // 达到容量上限即触发分发，避免单个 Trace 无限膨胀。
        Dispatch(session.trace_key);
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
    thread_pool_->submit([session_shared]() {
        // TODO: 后续接入聚合逻辑，当前仅占位以验证分发链路。
        (void)session_shared;
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

    auto build_node = [&index, order, &visited](size_t span_id, const auto& self_ref) -> nlohmann::json {
        nlohmann::json node;
        if (!visited.insert(span_id).second) {
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

    return output.dump();
}
