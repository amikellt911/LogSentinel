#pragma once

#include <memory>

#include "core/ServiceRuntimeAccumulator.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include <MiniMuduo/net/TcpConnection.h>

class ServiceMonitorHandler
{
public:
    // 服务监控接口当前只读已发布快照，不做数据库和 AI 调用，所以不需要再绕线程池。
    explicit ServiceMonitorHandler(std::shared_ptr<ServiceRuntimeAccumulator> accumulator);

    void handleGetRuntimeSnapshot(const HttpRequest& req,
                                  HttpResponse* resp,
                                  const MiniMuduo::net::TcpConnectionPtr& conn);

private:
    std::shared_ptr<ServiceRuntimeAccumulator> accumulator_;
};
