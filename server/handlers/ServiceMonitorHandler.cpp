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

    // 这里直接同步返回，因为读的是 OnTick 已经原子发布好的成品快照；
    // handler 线程不再进服务监控那把窗口锁，也不再现场排序服务榜和操作榜。
    resp->setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
    resp->addCorsHeaders();
    resp->setHeader("Content-Type", "application/json");
    resp->setBody(nlohmann::json(snapshot).dump());
}
