#include "handlers/LogHandler.h"
#include "core/LogBatcher.h"
#include "core/AnalysisTask.h"
#include "core/TraceSessionManager.h"
#include "MiniMuduo/net/EventLoop.h"
#include "persistence/SqliteLogRepository.h"
#include "threadpool/ThreadPool.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
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

std::string JsonValueToAttributeString(const nlohmann::json& value)
{
    if (value.is_string()) {
        return value.get<std::string>();
    }
    return value.dump();
}

bool IsKnownTraceSpanField(const std::string& key)
{
    static const std::unordered_set<std::string> known_fields = {
        "trace_key",
        "trace_id",
        "span_id",
        "parent_span_id",
        "start_time_ms",
        "end_time_ms",
        "name",
        "service_name",
        "status",
        "kind",
        "trace_end",
        "attributes"
    };
    return known_fields.find(key) != known_fields.end();
}

void CollectUnknownTopLevelAttributes(const nlohmann::json& body,
                                      std::unordered_map<std::string, std::string>* attributes)
{
    for (auto it = body.begin(); it != body.end(); ++it) {
        if (IsKnownTraceSpanField(it.key())) {
            continue;
        }
        // 顶层未知字段统一收敛到 attributes，目的是让客户端新增指标时服务端无需立刻改协议。
        // 这里用 emplace 不覆盖已存在键，确保显式 attributes 的值优先级更高，避免歧义覆盖。
        attributes->emplace(it.key(), JsonValueToAttributeString(it.value()));
    }
}

bool ParseRequiredUint(const nlohmann::json& body, const char* field, size_t* out, std::string* error)
{
    if (!body.contains(field)) {
        *error = std::string("Missing required field: ") + field;
        return false;
    }
    const nlohmann::json& value = body.at(field);
    if (!value.is_number_integer() && !value.is_number_unsigned()) {
        *error = std::string("Field must be integer: ") + field;
        return false;
    }
    const int64_t parsed = value.get<int64_t>();
    if (parsed < 0) {
        *error = std::string("Field must be non-negative: ") + field;
        return false;
    }
    *out = static_cast<size_t>(parsed);
    return true;
}

bool ParseOptionalUint(const nlohmann::json& body, const char* field, std::optional<size_t>* out, std::string* error)
{
    if (!body.contains(field) || body.at(field).is_null()) {
        return true;
    }
    size_t value = 0;
    if (!ParseRequiredUint(body, field, &value, error)) {
        return false;
    }
    *out = value;
    return true;
}

bool ParseRequiredInt64(const nlohmann::json& body, const char* field, int64_t* out, std::string* error)
{
    if (!body.contains(field)) {
        *error = std::string("Missing required field: ") + field;
        return false;
    }
    const nlohmann::json& value = body.at(field);
    if (!value.is_number_integer() && !value.is_number_unsigned()) {
        *error = std::string("Field must be integer: ") + field;
        return false;
    }
    *out = value.get<int64_t>();
    return true;
}

bool ParseOptionalInt64(const nlohmann::json& body, const char* field, std::optional<int64_t>* out, std::string* error)
{
    if (!body.contains(field) || body.at(field).is_null()) {
        return true;
    }
    int64_t value = 0;
    if (!ParseRequiredInt64(body, field, &value, error)) {
        return false;
    }
    *out = value;
    return true;
}

bool ParseRequiredString(const nlohmann::json& body, const char* field, std::string* out, std::string* error)
{
    if (!body.contains(field)) {
        *error = std::string("Missing required field: ") + field;
        return false;
    }
    const nlohmann::json& value = body.at(field);
    if (!value.is_string()) {
        *error = std::string("Field must be string: ") + field;
        return false;
    }
    *out = value.get<std::string>();
    return true;
}

bool ParseOptionalBool(const nlohmann::json& body, const char* field, std::optional<bool>* out, std::string* error)
{
    if (!body.contains(field) || body.at(field).is_null()) {
        return true;
    }
    const nlohmann::json& value = body.at(field);
    if (!value.is_boolean()) {
        *error = std::string("Field must be bool: ") + field;
        return false;
    }
    *out = value.get<bool>();
    return true;
}

bool ParseOptionalStatus(const nlohmann::json& body, std::optional<SpanEvent::Status>* status, std::string* error)
{
    if (!body.contains("status") || body.at("status").is_null()) {
        return true;
    }
    const nlohmann::json& value = body.at("status");
    if (!value.is_string()) {
        *error = "Field must be string: status";
        return false;
    }

    const std::string lower = toLowerCopy(value.get<std::string>());
    if (lower == "unset") {
        *status = SpanEvent::Status::Unset;
        return true;
    }
    if (lower == "ok") {
        *status = SpanEvent::Status::Ok;
        return true;
    }
    if (lower == "error") {
        *status = SpanEvent::Status::Error;
        return true;
    }

    *error = "Invalid status, expected one of: UNSET/OK/ERROR";
    return false;
}

bool ParseOptionalKind(const nlohmann::json& body, std::optional<SpanEvent::Kind>* kind, std::string* error)
{
    if (!body.contains("kind") || body.at("kind").is_null()) {
        return true;
    }
    const nlohmann::json& value = body.at("kind");
    if (!value.is_string()) {
        *error = "Field must be string: kind";
        return false;
    }

    const std::string lower = toLowerCopy(value.get<std::string>());
    if (lower == "internal") {
        *kind = SpanEvent::Kind::Internal;
        return true;
    }
    if (lower == "server") {
        *kind = SpanEvent::Kind::Server;
        return true;
    }
    if (lower == "client") {
        *kind = SpanEvent::Kind::Client;
        return true;
    }
    if (lower == "producer") {
        *kind = SpanEvent::Kind::Producer;
        return true;
    }
    if (lower == "consumer") {
        *kind = SpanEvent::Kind::Consumer;
        return true;
    }

    *error = "Invalid kind, expected one of: INTERNAL/SERVER/CLIENT/PRODUCER/CONSUMER";
    return false;
}

bool ParseOptionalAttributes(const nlohmann::json& body,
                             std::unordered_map<std::string, std::string>* attributes,
                             std::string* error)
{
    if (!body.contains("attributes") || body.at("attributes").is_null()) {
        return true;
    }
    const nlohmann::json& value = body.at("attributes");
    if (!value.is_object()) {
        *error = "Field must be object: attributes";
        return false;
    }

    for (auto it = value.begin(); it != value.end(); ++it) {
        // 统一转字符串是为了让 TraceSessionManager 的最小模型稳定，后续再按类型升级。
        (*attributes)[it.key()] = JsonValueToAttributeString(it.value());
    }
    return true;
}
} // namespace

LogHandler::LogHandler(ThreadPool *tpool,
                       std::shared_ptr<SqliteLogRepository> repo,
                       std::shared_ptr<LogBatcher> batcher,
                       TraceSessionManager* trace_session_manager)
    : tpool_(tpool),
      batcher_(batcher),
      repo_(repo),
      trace_session_manager_(trace_session_manager)
{
}

void LogHandler::handlePost(const HttpRequest &req, HttpResponse *resp, const MiniMuduo::net::TcpConnectionPtr &conn)
{
    AnalysisTask task;
    // 没用，因为const(为了通用性)，会降级为拷贝
    //  task.trace_id = std::move(req.trace_id);
    //  task.raw_request_body = std::move(req.body_);
    task.trace_id = req.trace_id;
    task.raw_request_body = req.body_;
    task.start_time = std::chrono::steady_clock::now();

    if (batcher_->push(std::move(task)))
    {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k202Acceptd);
        resp->setHeader("Content-Type", "application/json");
        resp->addCorsHeaders();
        nlohmann::json j;
        j["trace_id"] = req.trace_id;
        resp->body_ = j.dump();
    }
    else
    {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
        resp->addCorsHeaders();
        resp->body_ = "{\"error\": \"Server is overloaded\"}";
    }
}

void LogHandler::handleTracePost(const HttpRequest &req, HttpResponse *resp, const MiniMuduo::net::TcpConnectionPtr &conn)
{
    (void)conn;
    resp->setHeader("Content-Type", "application/json");
    resp->addCorsHeaders();

    if (!trace_session_manager_) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
        resp->body_ = "{\"error\": \"Trace pipeline is unavailable\"}";
        return;
    }

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body_);
    } catch (const nlohmann::json::parse_error&) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
        resp->body_ = "{\"error\": \"Invalid JSON body\"}";
        return;
    }

    if (!body.is_object()) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
        resp->body_ = "{\"error\": \"Body must be a JSON object\"}";
        return;
    }

    SpanEvent span;
    std::string error;
    if (body.contains("trace_key")) {
        if (!ParseRequiredUint(body, "trace_key", &span.trace_key, &error)) {
            resp->setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
            resp->body_ = nlohmann::json{{"error", error}}.dump();
            return;
        }
    } else if (body.contains("trace_id")) {
        // 兼容 trace_id 别名是为了降低接入方切换成本，避免首版就要求全量改字段名。
        if (!ParseRequiredUint(body, "trace_id", &span.trace_key, &error)) {
            resp->setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
            resp->body_ = nlohmann::json{{"error", error}}.dump();
            return;
        }
    } else {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
        resp->body_ = "{\"error\": \"Missing required field: trace_key\"}";
        return;
    }

    if (!ParseRequiredUint(body, "span_id", &span.span_id, &error) ||
        !ParseRequiredInt64(body, "start_time_ms", &span.start_time_ms, &error) ||
        !ParseRequiredString(body, "name", &span.name, &error) ||
        !ParseRequiredString(body, "service_name", &span.service_name, &error) ||
        !ParseOptionalUint(body, "parent_span_id", &span.parent_span_id, &error) ||
        !ParseOptionalInt64(body, "end_time_ms", &span.end_time, &error) ||
        !ParseOptionalBool(body, "trace_end", &span.trace_end, &error) ||
        !ParseOptionalStatus(body, &span.status, &error) ||
        !ParseOptionalKind(body, &span.kind, &error) ||
        !ParseOptionalAttributes(body, &span.attributes, &error)) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
        resp->body_ = nlohmann::json{{"error", error}}.dump();
        return;
    }
    CollectUnknownTopLevelAttributes(body, &span.attributes);

    const TraceSessionManager::PushResult push_result = trace_session_manager_->Push(span);
    if (push_result == TraceSessionManager::PushResult::RejectedUnavailable) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
        resp->body_ = nlohmann::json{
            {"accepted", false},
            {"error", "Trace pipeline is unavailable"}
        }.dump();
        return;
    }
    if (push_result == TraceSessionManager::PushResult::RejectedOverload) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
        resp->setHeader("Retry-After", "1");
        resp->body_ = nlohmann::json{
            {"accepted", false},
            {"error", "Trace pipeline is overloaded"},
            {"retry_after_seconds", 1}
        }.dump();
        return;
    }

    resp->setStatusCode(HttpResponse::HttpStatusCode::k202Acceptd);
    nlohmann::json response_body{
        {"accepted", true},
        {"trace_key", span.trace_key},
        {"span_id", span.span_id}
    };
    if (push_result == TraceSessionManager::PushResult::AcceptedDeferred) {
        // 这里明确告诉调用方：数据已经被服务端接住，但由于下游暂时拥堵，内部会稍后重投。
        response_body["deferred"] = true;
        response_body["message"] = "Trace accepted; internal dispatch deferred";
    }
    resp->body_ = response_body.dump();
}

void LogHandler::handleGetResult(const HttpRequest &req, HttpResponse *resp, const MiniMuduo::net::TcpConnectionPtr &conn)
{
    std::string trace_id = req.path().substr(9);

    // 使用 weak_ptr 防止 conn 提前销毁
    std::weak_ptr<MiniMuduo::net::TcpConnection> weakConn = conn;
    auto querywork = [trace_id, repo = repo_, weakConn]()
    {
        // --- 这部分在 Worker 线程 ---
        auto result = repo->queryStructResultByTraceId(trace_id);
        auto conn = weakConn.lock();
        if (conn)
        {
            auto loop = conn->getLoop();
            loop->queueInLoop([weakConn, trace_id, result]()
                              {
            // --- 这部分在 IO 线程 ---
            auto conn = weakConn.lock();
            if (conn) { // 检查连接是否还存活
                HttpResponse resp;
                resp.addCorsHeaders();
                if (result) { // result 是 optional<string>
                    resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
                    resp.setHeader("Content-Type", "application/json");
                        // 注意：JSON 字符串中的引号需要转义
                        // 3. 修正：使用 json 库拼接，而不是手动拼字符串
                        // 注意：result 已经是 json 字符串了，不要重复序列化，
                        // 我们需要构造 {"trace_id": "...", "result": JSON_OBJECT}
                        // 这里的 result.value() 是一个字符串形式的 JSON 对象
                        // 为了避免转义问题，我们手动拼这一块是合理的，但要确保 result 本身是合法 JSON
                        // 或者更安全的做法：
                        // resp.body_ = "{\"trace_id\": \"" + trace_id + "\", \"result\": " + *result + "}"; 
                        // 鉴于 *result 来自数据库且原本就是合法 JSON，你原来的写法是可以接受的。
                        // 如果想更稳妥，可以这样：
                        try {
                             nlohmann::json root;
                             root["trace_id"] = trace_id;
                             root["result"] = *result; // 解析后再放入，保证嵌套正确
                             resp.body_ = root.dump();
                        } catch (...) {
                             // 极端情况：数据库里的 json 坏了
                             resp.body_ = "{\"trace_id\": \"" + trace_id + "\", \"result\": null, \"error\": \"Data corrupted\"}";
                        }
                } else {
                    resp.setStatusCode(HttpResponse::HttpStatusCode::k404NotFound);
                    resp.body_ = "{\"error\": \"Trace ID not found\"}";
                }

                MiniMuduo::net::Buffer buf;
                resp.appendToBuffer(&buf);
                conn->send(std::move(buf));
            } });
        }
    };

    if (tpool_->submit(std::move(querywork)))
    {
        resp->isHandledAsync = true;
        // 对于 GET 请求，我们不立即返回任何东西，因为响应是异步的
        // 如果想给客户端一个即时反馈，可以考虑接受请求后立即返回一个空的 202，但这不常用
    }
    else
    {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
        resp->addCorsHeaders();
        resp->body_ = "{\"error\": \"Server is overloaded\"}";

        // 只有在这种同步失败的情况下，才需要在这里发送响应
        // MiniMuduo::net::Buffer buf;
        // resp->appendToBuffer(&buf);
        // isHandledAsync = false会自动发送
        // conn->send(std::move(buf));
    }
}
