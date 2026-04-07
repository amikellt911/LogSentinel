#include "handlers/DashboardHandler.h"

#include <iostream>

#include <nlohmann/json.hpp>

namespace
{
nlohmann::json BuildDashboardSnapshotJson(const SystemRuntimeSnapshot& snapshot)
{
    nlohmann::json body;
    body["overview"] = {
        {"total_logs", snapshot.overview.total_logs},
        {"ai_call_total", snapshot.overview.ai_call_total},
        {"ai_queue_wait_ms", snapshot.overview.ai_queue_wait_ms},
        {"ai_inference_latency_ms", snapshot.overview.ai_inference_latency_ms},
        {"memory_rss_mb", snapshot.overview.memory_rss_mb},
        {"backpressure_status", snapshot.overview.backpressure_status},
    };
    body["token_stats"] = {
        {"input_tokens", snapshot.token_stats.input_tokens},
        {"output_tokens", snapshot.token_stats.output_tokens},
        {"total_tokens", snapshot.token_stats.total_tokens},
        {"avg_tokens_per_call", snapshot.token_stats.avg_tokens_per_call},
    };

    nlohmann::json timeseries = nlohmann::json::array();
    for (const auto& point : snapshot.timeseries)
    {
        timeseries.push_back({
            {"time_ms", point.time_ms},
            {"ingest_rate", point.ingest_rate},
            {"ai_completion_rate", point.ai_completion_rate},
        });
    }
    body["timeseries"] = std::move(timeseries);
    return body;
}
} // namespace

DashboardHandler::DashboardHandler(std::shared_ptr<SystemRuntimeAccumulator> runtime_accumulator)
    : runtime_accumulator_(std::move(runtime_accumulator))
{
}

void DashboardHandler::handleGetStats(const HttpRequest& req,
                                      HttpResponse* resp,
                                      const MiniMuduo::net::TcpConnectionPtr& conn)
{
    (void)req;
    (void)conn;

    try
    {
        // Dashboard 现在只读内存快照，不再把一次简单查询丢进 SQLite + 线程池。
        // 这一刀的目标只是切换读源，让页面先拿到系统运行态真值；更复杂的埋点扩展留在后面补。
        const SystemRuntimeSnapshot snapshot = runtime_accumulator_->BuildSnapshot();
        const std::string json_str = BuildDashboardSnapshotJson(snapshot).dump();

        resp->setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
        resp->addCorsHeaders();
        resp->setHeader("Content-Type", "application/json");
        resp->setBody(json_str);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[DashboardHandler] build snapshot failed: " << e.what() << '\n';
        resp->setStatusCode(HttpResponse::HttpStatusCode::k500InternalServerError);
        resp->addCorsHeaders();
        resp->setHeader("Content-Type", "application/json");
        resp->setBody("{\"error\":\"Internal Dashboard Error\"}");
    }
}
