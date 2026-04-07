#pragma once
#include <memory>

#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include <MiniMuduo/net/TcpConnection.h>
#include "core/SystemRuntimeAccumulator.h"

class DashboardHandler {
public:
    // Dashboard 这一刀已经改成直接读取系统运行态快照，不再经过 SQLite 和线程池。
    // 这样前端看到的是主链路埋点的最新值，而不是数据库里那套历史 dashboard 统计。
    explicit DashboardHandler(std::shared_ptr<SystemRuntimeAccumulator> runtime_accumulator);

    void handleGetStats(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);

private:
    std::shared_ptr<SystemRuntimeAccumulator> runtime_accumulator_;
};
