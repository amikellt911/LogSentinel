#include "handlers/TraceQueryHandler.h"

#include "persistence/SqliteTraceRepository.h"
#include "threadpool/ThreadPool.h"
#include "MiniMuduo/net/EventLoop.h"
#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace
{
std::optional<std::string> ParseOptionalStringField(const nlohmann::json& body,
                                                    const char* field,
                                                    std::string* error)
{
    if (!body.contains(field) || body.at(field).is_null()) {
        return std::nullopt;
    }
    if (!body.at(field).is_string()) {
        *error = std::string("Field must be string: ") + field;
        return std::nullopt;
    }
    const std::string value = body.at(field).get<std::string>();
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

bool ParseOptionalInt64Field(const nlohmann::json& body,
                             const char* field,
                             std::optional<int64_t>* out,
                             std::string* error)
{
    if (!body.contains(field) || body.at(field).is_null()) {
        return true;
    }
    const nlohmann::json& value = body.at(field);
    if (!value.is_number_integer() && !value.is_number_unsigned()) {
        *error = std::string("Field must be integer: ") + field;
        return false;
    }
    *out = value.get<int64_t>();
    return true;
}

bool ParseOptionalRiskLevels(const nlohmann::json& body,
                             const char* field,
                             std::vector<std::string>* out,
                             std::string* error)
{
    out->clear();
    if (!body.contains(field) || body.at(field).is_null()) {
        return true;
    }
    const nlohmann::json& value = body.at(field);
    if (!value.is_array()) {
        *error = std::string("Field must be string array: ") + field;
        return false;
    }
    for (const auto& item : value) {
        if (!item.is_string()) {
            *error = std::string("Field must be string array: ") + field;
            return false;
        }
        const std::string level = item.get<std::string>();
        if (!level.empty()) {
            out->push_back(level);
        }
    }
    return true;
}

bool ParseOptionalSizeField(const nlohmann::json& body,
                            const char* field,
                            size_t* out,
                            std::string* error)
{
    if (!body.contains(field) || body.at(field).is_null()) {
        return true;
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

std::string StripQueryString(const std::string& raw_path)
{
    const size_t query_pos = raw_path.find('?');
    if (query_pos == std::string::npos) {
        return raw_path;
    }
    return raw_path.substr(0, query_pos);
}

std::string ExtractTraceIdFromPath(const std::string& raw_path)
{
    static const std::string kPrefix = "/traces/";
    const std::string clean_path = StripQueryString(raw_path);
    if (clean_path.rfind(kPrefix, 0) != 0) {
        return "";
    }
    return clean_path.substr(kPrefix.size());
}

nlohmann::json BuildDebugSearchRequestJson(const TraceSearchRequest& request)
{
    nlohmann::json root;
    root["trace_id"] = request.trace_id.has_value() ? nlohmann::json(*request.trace_id) : nlohmann::json(nullptr);
    root["service_name"] = request.service_name.has_value() ? nlohmann::json(*request.service_name) : nlohmann::json(nullptr);
    root["start_time_ms"] = request.start_time_ms.has_value() ? nlohmann::json(*request.start_time_ms) : nlohmann::json(nullptr);
    root["end_time_ms"] = request.end_time_ms.has_value() ? nlohmann::json(*request.end_time_ms) : nlohmann::json(nullptr);
    root["risk_levels"] = request.risk_levels;
    root["page"] = request.page;
    root["page_size"] = request.page_size;
    return root;
}

nlohmann::json BuildSearchResultJson(const TraceSearchResult& result)
{
    nlohmann::json items = nlohmann::json::array();
    for (const auto& item : result.items) {
        nlohmann::json row;
        row["trace_id"] = item.trace_id;
        row["service_name"] = item.service_name;
        row["start_time_ms"] = item.start_time_ms;
        row["end_time_ms"] = item.end_time_ms.has_value() ? nlohmann::json(*item.end_time_ms) : nlohmann::json(nullptr);
        row["duration_ms"] = item.duration_ms;
        row["span_count"] = item.span_count;
        row["token_count"] = item.token_count;
        row["risk_level"] = item.risk_level;
        items.push_back(std::move(row));
    }

    nlohmann::json root;
    root["total"] = result.total;
    root["items"] = std::move(items);
    return root;
}

nlohmann::json BuildTraceDetailJson(const TraceDetailRecord& detail)
{
    nlohmann::json spans = nlohmann::json::array();
    for (const auto& span : detail.spans) {
        nlohmann::json row;
        row["span_id"] = span.span_id;
        row["parent_id"] = span.parent_id.has_value() ? nlohmann::json(*span.parent_id) : nlohmann::json(nullptr);
        row["service_name"] = span.service_name;
        row["operation"] = span.operation;
        row["start_time_ms"] = span.start_time_ms;
        row["duration_ms"] = span.duration_ms;
        row["raw_status"] = span.raw_status;
        spans.push_back(std::move(row));
    }

    nlohmann::json root;
    root["trace_id"] = detail.trace_id;
    root["service_name"] = detail.service_name;
    root["start_time_ms"] = detail.start_time_ms;
    root["end_time_ms"] = detail.end_time_ms.has_value() ? nlohmann::json(*detail.end_time_ms) : nlohmann::json(nullptr);
    root["duration_ms"] = detail.duration_ms;
    root["span_count"] = detail.span_count;
    root["token_count"] = detail.token_count;
    root["risk_level"] = detail.risk_level;
    root["tags"] = detail.tags;
    root["spans"] = std::move(spans);
    if (detail.analysis.has_value()) {
        root["analysis"] = {
            {"risk_level", detail.analysis->risk_level},
            {"summary", detail.analysis->summary},
            {"root_cause", detail.analysis->root_cause},
            {"solution", detail.analysis->solution},
            {"confidence", detail.analysis->confidence}
        };
    } else {
        root["analysis"] = nullptr;
    }
    return root;
}

void QueueJsonResponse(const std::weak_ptr<MiniMuduo::net::TcpConnection>& weak_conn,
                       HttpResponse::HttpStatusCode status,
                       nlohmann::json body)
{
    if (auto conn = weak_conn.lock()) {
        conn->getLoop()->queueInLoop([weak_conn, status, body = std::move(body)]() mutable {
            auto alive_conn = weak_conn.lock();
            if (!alive_conn) {
                return;
            }
            HttpResponse response;
            response.setStatusCode(status);
            response.setHeader("Content-Type", "application/json");
            response.addCorsHeaders();
            response.setBody(body.dump());

            MiniMuduo::net::Buffer buf;
            response.appendToBuffer(&buf);
            alive_conn->send(std::move(buf));
        });
    }
}
} // namespace

TraceQueryHandler::TraceQueryHandler(std::shared_ptr<SqliteTraceRepository> repo,
                                     ThreadPool* query_tpool)
    : repo_(std::move(repo))
    , query_tpool_(query_tpool)
{
}

void TraceQueryHandler::handleSearchTraces(const HttpRequest& req,
                                           HttpResponse* resp,
                                           const MiniMuduo::net::TcpConnectionPtr& conn)
{
    resp->setHeader("Content-Type", "application/json");
    resp->addCorsHeaders();

    if (!repo_ || !query_tpool_) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
        resp->body_ = "{\"error\":\"Trace query service is unavailable\"}";
        return;
    }

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body_);
    } catch (const nlohmann::json::parse_error&) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
        resp->body_ = "{\"error\":\"Invalid JSON body\"}";
        return;
    }

    if (!body.is_object()) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
        resp->body_ = "{\"error\":\"Body must be a JSON object\"}";
        return;
    }

    TraceSearchRequest request;
    std::string error;
    request.trace_id = ParseOptionalStringField(body, "trace_id", &error);
    if (!error.empty()) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
        resp->body_ = nlohmann::json{{"error", error}}.dump();
        return;
    }
    request.service_name = ParseOptionalStringField(body, "service_name", &error);
    if (!error.empty()) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
        resp->body_ = nlohmann::json{{"error", error}}.dump();
        return;
    }
    if (!ParseOptionalInt64Field(body, "start_time_ms", &request.start_time_ms, &error) ||
        !ParseOptionalInt64Field(body, "end_time_ms", &request.end_time_ms, &error) ||
        !ParseOptionalRiskLevels(body, "risk_levels", &request.risk_levels, &error) ||
        !ParseOptionalSizeField(body, "page", &request.page, &error) ||
        !ParseOptionalSizeField(body, "page_size", &request.page_size, &error)) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
        resp->body_ = nlohmann::json{{"error", error}}.dump();
        return;
    }

    // 这里先在 Handler 层把分页边界收口，避免后续 repo 层再到处写同样的防御代码。
    if (request.page < 1) {
        request.page = 1;
    }
    if (request.page_size < 1) {
        request.page_size = 20;
    }
    if (request.page_size > 100) {
        request.page_size = 100;
    }

    std::weak_ptr<MiniMuduo::net::TcpConnection> weak_conn = conn;
    if (!query_tpool_->submit([weak_conn, repo = repo_, request]() mutable {
        try {
            TraceSearchResult result = repo->SearchTraces(request);
            QueueJsonResponse(weak_conn,
                              HttpResponse::HttpStatusCode::k200Ok,
                              BuildSearchResultJson(result));
        } catch (const std::exception& e) {
            nlohmann::json response_body{
                {"error", "Trace search backend is not implemented yet"},
                {"detail", e.what()},
                {"parsed_request", BuildDebugSearchRequestJson(request)}
            };
            QueueJsonResponse(weak_conn,
                              HttpResponse::HttpStatusCode::k500InternalServerError,
                              std::move(response_body));
        }
    })) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
        resp->body_ = "{\"error\":\"Trace search query queue is overloaded\"}";
        return;
    }

    resp->isHandledAsync = true;
}

void TraceQueryHandler::handleGetTraceDetail(const HttpRequest& req,
                                             HttpResponse* resp,
                                             const MiniMuduo::net::TcpConnectionPtr& conn)
{
    resp->setHeader("Content-Type", "application/json");
    resp->addCorsHeaders();

    if (!repo_ || !query_tpool_) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
        resp->body_ = "{\"error\":\"Trace query service is unavailable\"}";
        return;
    }

    const std::string trace_id = ExtractTraceIdFromPath(req.path());
    if (trace_id.empty()) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
        resp->body_ = "{\"error\":\"Missing trace_id in path\"}";
        return;
    }

    std::weak_ptr<MiniMuduo::net::TcpConnection> weak_conn = conn;
    if (!query_tpool_->submit([weak_conn, repo = repo_, trace_id]() mutable {
        try {
            std::optional<TraceDetailRecord> detail = repo->GetTraceDetail(trace_id);
            if (!detail.has_value()) {
                QueueJsonResponse(weak_conn,
                                  HttpResponse::HttpStatusCode::k404NotFound,
                                  nlohmann::json{{"error", "Trace ID not found"}, {"trace_id", trace_id}});
                return;
            }
            QueueJsonResponse(weak_conn,
                              HttpResponse::HttpStatusCode::k200Ok,
                              BuildTraceDetailJson(*detail));
        } catch (const std::exception& e) {
            nlohmann::json response_body{
                {"error", "Trace detail backend is not implemented yet"},
                {"detail", e.what()},
                {"trace_id", trace_id}
            };
            QueueJsonResponse(weak_conn,
                              HttpResponse::HttpStatusCode::k500InternalServerError,
                              std::move(response_body));
        }
    })) {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
        resp->body_ = "{\"error\":\"Trace detail query queue is overloaded\"}";
        return;
    }

    resp->isHandledAsync = true;
}
