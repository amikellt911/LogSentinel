#pragma once
#include<string>
#include<nlohmann/json.hpp>
#include "NotifyTypes.h"
class INotifier {
public:
    virtual ~INotifier() = default;
    virtual void notify(const std::string& trace_id,const nlohmann::json& content) = 0;
    // 新增 Trace 聚合链路通知入口：先传领域结构，序列化细节下沉到具体 notifier 实现层。
    virtual void notifyTraceAlert(const TraceAlertEvent& event) = 0;
};
