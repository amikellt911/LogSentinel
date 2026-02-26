# 20260226 refactor-notification

## Git Commit Message
`refactor(notification): 移除notifyReport并收敛INotifier依赖边界`

## Modification
- `server/notification/INotifier.h`
- `server/notification/WebhookNotifier.h`
- `server/notification/WebhookNotifier.cpp`
- `server/core/LogBatcher.cpp`
- `docs/todo-list/Todo_WebhookNotifier.md`

## Learning Tips

### Newbie Tips
- 抽象接口文件应该尽量只暴露能力，不要直接引入下层实现或业务聚合结构，否则会出现“上层依赖下层”的反向耦合。
- 没有真实调用的接口要尽早删除，避免后续新功能继续围绕无效抽象扩展，导致重构成本持续上升。

### Function Explanation
- `virtual void notify(...) = 0;`：纯虚函数定义了通知能力的最小契约，让调用方只依赖接口，不依赖具体实现。
- `rg -n "notifyReport\(" ...`：用全局检索确认接口删除后没有遗漏调用点，这是删除公共接口时最关键的回归检查手段。

### Pitfalls
- 只删实现不删接口声明会导致“接口仍承诺能力，但实现已缺失”的编译错误或行为不一致。
- 只删 `INotifier` 不同步 `WebhookNotifier` 的 `override` 会直接触发签名不匹配编译失败。
- 删除接口后不做至少一次全量编译，容易把错误留到后续 unrelated 改动里才暴露，排查成本更高。

---

## 追加记录：新增通知域模型 TraceAlertEvent

## Git Commit Message
`feat(notification): 新增TraceAlertEvent通知数据结构`

## Modification
- `server/notification/NotifyTypes.h`
- `docs/todo-list/Todo_WebhookNotifier.md`

## Learning Tips

### Newbie Tips
- 通知层最好先定义“领域事件结构体”，再由具体渠道做序列化，这样业务代码不会被 JSON 细节反向污染。
- 字段优先保留“低争议核心信息”（trace 标识、时序、风险、摘要），能先跑通链路，再增量扩展字段。

### Function Explanation
- `size_t`：表达计数类字段（如 `span_count`、`token_count`）更符合语义，避免负数参与逻辑。
- `int64_t`：用于毫秒时间戳和时长，避免 32 位整数在长时间运行后溢出。

### Pitfalls
- 如果直接在 `INotifier` 里定义复杂业务结构，接口头会变重，后续任何字段调整都可能放大编译影响面。
- 如果先传 JSON 字符串再解析，容易出现字段拼写漂移，且缺少编译期约束。
- 若 `risk_level` 同时来源于多个结构体，必须提前约定优先级，否则通知内容会出现前后不一致。

---

## 追加记录：实现 TraceAlertEvent 的 webhook 发送能力

## Git Commit Message
`feat(notification): 支持TraceAlertEvent结构化webhook推送`

## Modification
- `server/notification/INotifier.h`
- `server/notification/WebhookNotifier.h`
- `server/notification/WebhookNotifier.cpp`
- `docs/todo-list/Todo_WebhookNotifier.md`

## Learning Tips

### Newbie Tips
- 先在接口层传“结构体”，再在发送层 `dump()` 成字符串，可以把业务语义和协议细节分开，后续改字段更稳。
- webhook 的响应码建议按 `2xx` 判成功，不要只盯 `200`，否则部分正常网关会被误判为失败。

### Function Explanation
- `notifyTraceAlert(const TraceAlertEvent&)`：通知抽象层新增的 trace 事件入口，用于承接 Trace 聚合链路输出。
- `buildTraceAlertPayload(...)`：把领域结构映射为统一 JSON 契约（`schema_version/event_type/event_id/sent_at_ms/source/data`）。
- `postJsonToWebhookUrls(...)`：抽出的公共发送逻辑，复用在旧 `notify` 和新 `notifyTraceAlert`，减少重复代码。

### Pitfalls
- 如果每个通知函数都各自写一套 Post 循环，后续超时、重试、日志策略很容易出现行为分叉。
- `event_id` 若不包含 `trace_id` 与发送时刻，网关侧做幂等去重会非常困难。
- 只加接口不编译全工程，纯虚函数变更很容易在其他模块实例化时才暴露编译错误。
