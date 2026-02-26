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

---

## 追加记录：TraceAlertEvent 仅 critical 触发发送

## Git Commit Message
`feat(notification): 限制trace告警仅critical等级发送`

## Modification
- `server/notification/WebhookNotifier.cpp`
- `docs/todo-list/Todo_WebhookNotifier.md`

## Learning Tips

### Newbie Tips
- 告警系统第一版必须先控噪，宁可先漏掉低等级，也不要让通道被信息洪水淹没。
- 风险等级判断尽量做成独立函数，后续从“硬编码阈值”升级到“配置化阈值”时改动最小。

### Function Explanation
- `shouldSendTraceAlert(...)`：封装告警门槛策略，当前版本规则是仅 `critical` 返回 true。
- `toLowerCopy(...)`：统一风险等级大小写，避免上游传 `CRITICAL` 时被门槛误判。

### Pitfalls
- 如果直接在发送函数里散落字符串比较，后续阈值策略扩展会出现逻辑分叉。
- 若大小写不统一处理，不同 AI Provider 的输出风格会导致告警行为不稳定。
- 过滤逻辑没有单点入口时，很难在日志中排查“为什么某条告警没有发”。

---

## 追加记录：将 Trace 告警过滤策略上移到 TraceSessionManager

## Git Commit Message
`refactor(core): 将trace告警阈值判断上移到聚合调度层`

## Modification
- `server/core/TraceSessionManager.h`
- `server/core/TraceSessionManager.cpp`
- `docs/todo-list/Todo_TraceSessionManager.md`

## Learning Tips

### Newbie Tips
- “是否该发送告警”属于业务策略，应该放在业务编排层（TraceSessionManager），而不是放在通知适配层（WebhookNotifier）。
- 把阈值判断留在发送层会导致多渠道实现（webhook/企业机器人）策略分叉，后续很难统一维护。

### Function Explanation
- `TraceSessionManager(..., INotifier* notifier = nullptr)`：通过可选依赖注入 notifier，让聚合链路能在不影响单测构造的前提下接入通知。
- `SaveSingleTraceAtomic(...)` 返回值：先确认落库成功，再决定是否发送通知，避免出现“通知已发但数据未落库”的不一致。
- `notifyTraceAlert(const TraceAlertEvent&)`：Trace 层只在满足阈值时触发，通知层只负责序列化和发送。

### Pitfalls
- 若在 notifier 内做 risk 过滤，后续新增通知渠道时容易忘记同步规则，出现同一事件不同渠道行为不一致。
- 若不在发送前判断落库成功，发生数据库失败时会造成“外部告警有记录、系统内无记录”的排障混乱。
- 引入新构造参数时不做默认值，会放大测试改动范围并降低重构效率。
