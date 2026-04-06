#include "handlers/ServiceMonitorHandler.h"

ServiceMonitorHandler::ServiceMonitorHandler(std::shared_ptr<ServiceRuntimeAccumulator> accumulator)
    : accumulator_(std::move(accumulator))
{
}

void ServiceMonitorHandler::handleGetRuntimeSnapshot(const HttpRequest&,
                                                     HttpResponse* resp,
                                                     const MiniMuduo::net::TcpConnectionPtr&)
{
    ServiceRuntimeSnapshot snapshot;
    if (accumulator_)
    {
        snapshot = accumulator_->BuildSnapshot();
    }

    // 这里直接同步返回，因为读的是已经发布好的内存快照；
    // 既没有 SQLite，也没有线程池排队，走 IO 线程就够了。
    resp->setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
    resp->addCorsHeaders();
    resp->setHeader("Content-Type", "application/json");
    resp->setBody(nlohmann::json(snapshot).dump());
}
