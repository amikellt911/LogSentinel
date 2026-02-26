# Todo_WebhookNotifier

- [x] 移除 INotifier 的 notifyReport 接口并去掉对持久化类型依赖
- [x] 同步删除 WebhookNotifier 的 notifyReport 声明与实现
- [x] 全量检索 notifyReport 残留引用并修正
- [x] 编译验证通知模块改动无回归
- [x] 追加本次通知接口收敛 dev-log 记录
- [x] 新建 NotifyTypes.h 并定义 TraceAlertEvent 基础字段
- [x] 在 INotifier 新增 notifyTraceAlert 接口并引入 NotifyTypes
- [x] 在 WebhookNotifier 实现 trace_alert v1 JSON 组装与发送
- [x] 保持旧 notify 兼容并抽取公共 webhook 发送逻辑
- [x] 编译验证通知模块改动无回归
- [x] 追加本次 TraceAlertEvent 发送实现 dev-log 记录
