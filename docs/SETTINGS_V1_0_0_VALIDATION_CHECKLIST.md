# v1.0.0 Settings 真实生效联调清单

## 1. 目的

这份清单只服务 `v1.0.0` 第一刀：

把已经接进 Trace 主链的 Settings 字段，按“保存 -> 重启/生效 -> 前端可见变化 / 后端行为变化”做一轮固定验收。

这份清单不负责讨论字段设计对不对，也不负责继续扩新功能。
它只回答一件事：

当前页面上这些配置，到底哪些是真的，应该怎么验，看到什么现象才算通过。

---

## 2. 使用边界

纳入这份清单的前提：

1. 字段已经能从 `SettingsPrototype` 保存到后端。
2. 后端当前已经真实消费，或者已经明确有真实运行态行为。
3. 这轮联调能在本地手工复现，不依赖额外的大改。

这轮先不纳入的字段：

1. `ai_retry_enabled`
2. `ai_retry_max_attempts`
3. benchmark 专用实验开关
4. 还没接入的 `GLM`
5. Docker / 一键演示启动

原因很直接：
这些要么还只是“可存不可用”，要么属于后续 `v1.0.0` 任务，不应该混进第一轮设置验收。

---

## 3. 验收前置条件

建议先固定一套最小联调环境：

1. 后端：`./server/build/LogSentinel`
2. 前端：`cd client && npm run dev`
3. AI proxy：`cd server/ai/proxy && python3 main.py`
4. Settings 页面：`http://localhost:5173/settings-prototype`
5. Trace 演示页面：`http://localhost:5173/service-prototype`

建议准备两组辅助材料：

1. 一组稳定可复现的 span/trace 发送脚本
2. 一个可用的飞书测试机器人 webhook

---

## 4. 验收方式分组

### 4.1 热更新

保存后，不要求重启后端；刷新页面或触发下一次轮询后，应能直接看到变化。

### 4.2 冷启动

保存后，只验证“写库成功 + 重启后端后新配置生效”。
这类字段当前不承诺运行时热切换。

### 4.3 外部联调

除了前后端以外，还要看 AI proxy、飞书机器人或真实 Trace 行为是否变化。

---

## 5. 执行顺序

建议按下面顺序跑，避免多个设置互相污染：

1. 先跑纯前端/展示类：`app_language`
2. 再跑纯冷启动类：端口、线程数、Trace 聚合参数
3. 再跑 AI 总开关 / 熔断 / 自动降级
4. 再跑 Prompt 与 AI 输出语言
5. 最后跑 Webhook 和 retention

这样做的原因是：
越往后面的配置，越容易依赖前面的基础链路。
先把基础口径跑通，后面的异常现象才不容易误判。

---

## 6. 核心验收清单

## 6.1 热更新类

| 配置项 | 当前口径 | 验证动作 | 通过标准 | 备注 |
| --- | --- | --- | --- | --- |
| `app_language` | 热更新，前端 UI 文案切换 | 在 Settings 页面把语言从中文切英文或反向切换，点击保存，然后刷新根路由 `/` 和设置页 | 页面主导航、设置页标题、按钮文案切到目标语言；刷新页面后仍保持 | 这里只验证 UI 语言，不验证 AI 输出语言 |

## 6.2 冷启动类

| 配置项 | 当前口径 | 验证动作 | 通过标准 | 备注 |
| --- | --- | --- | --- | --- |
| `http_port` | 冷启动 | 在设置页改端口，保存；重启后端；访问新端口 | 新端口可访问，旧端口不可访问 | 前端 dev proxy 也要跟着确认目标端口 |
| `kernel_worker_threads` | 冷启动 | 改线程数，保存；重启后端；观察启动日志 | 启动日志里出现新的 worker 数 | 这一步先看启动日志，不做吞吐对比 |
| `trace_end_field` | 冷启动 | 把结束字段改成新名字，例如 `end_flag`；保存并重启；发送带 `end_flag=true` 的 span | Trace 能正常完成并进入查询列表 | 这是解析层配置，不是 AI 层配置 |
| `trace_end_aliases` | 冷启动 | 增加一个别名，例如 `end`；保存并重启；发送带 `end=true` 的 span | Trace 能正常完成 | 要验证“主字段不变、别名补充生效” |
| `span_capacity` | 冷启动 | 把容量改小，例如 `2`；保存并重启；发送同一 trace 的多个 span，但不带结束字段 | 到达容量后 trace 能 seal 并最终落库 | 这里看的是提前封口，不是立即 AI 成功 |
| `token_limit` | 冷启动 | 把 token 限制改成一个很小值；保存并重启；发送较大的 trace | 命中 token 限制后 trace 能 seal 并最终落库 | 前提是当前 token 估算链路已经能触发该条件 |
| `collecting_idle_timeout_ms` | 冷启动 | 把空闲超时改短，例如 `1000`；保存并重启；发送不带结束字段的 trace 后停止上报 | 大约 1s 后 trace 自动完成并可查询 | 要和 `sweep_tick_ms` 一起看，允许有少量 tick 误差 |
| `sealed_grace_window_ms` | 冷启动 | 把窗口改成一个明显值，例如 `3000`；保存并重启；先发送结束 span，再补一个晚到 span | 在 grace window 内的晚到 span 仍能并入同一 trace | 这一步是两阶段延迟关闭语义的关键证明 |
| `retry_base_delay_ms` | 冷启动 | 改成一个明显较大的值；保存并重启；人为制造 AI / dispatch 首次失败 | 重试间隔体现出新的起始等待时间 | 这项更偏日志观察，先不追求前端直接可见 |
| `sweep_tick_ms` | 冷启动 | 把 tick 改成较大或较小；保存并重启；重新跑 idle timeout / grace window 场景 | 超时触发节奏明显跟 tick 变化一致 | 这项本质是全局时序底盘参数 |
| `wm_active_sessions_overload/critical` | 冷启动 | 把阈值调低；保存并重启；发送大量并发 trace | 背压状态更容易进入 overload / critical | 当前先看后端状态和前端监控变化 |
| `wm_buffered_spans_overload/critical` | 冷启动 | 把阈值调低；保存并重启；快速压入大量 spans | 背压状态更容易进入 overload / critical | 与 buffered spans 指标联动看 |
| `wm_pending_tasks_overload/critical` | 冷启动 | 把阈值调低；保存并重启；制造任务堆积 | 背压状态更容易进入 overload / critical | 与 pending tasks 指标联动看 |
| `log_retention_days` | 冷启动 | 改保留天数；保存并重启；准备一批过期 trace；观察启动清理和后续定时清理 | 过期 trace 被删，未过期 trace 保留 | 当前语义是按 `trace_summary.updated_at` 判过期 |

## 6.3 AI 主链与外部联调类

| 配置项 | 当前口径 | 验证动作 | 通过标准 | 备注 |
| --- | --- | --- | --- | --- |
| `ai_analysis_enabled` | 冷启动，总开关 | 关闭后保存并重启；发送一条新 trace | `ai_status=skipped_manual`，不再发起 AI 分析 | 这是用户主动关闭，不是熔断 |
| `ai_provider` | 冷启动 | 改成 `mock` 或 `gemini`；保存并重启；发送新 trace | 启动日志与实际调用都落到目标 provider | 当前真实 provider 以 `mock / gemini` 为准 |
| `ai_model` | 冷启动 | 改模型名并重启；发送新 trace | 启动日志或 proxy 请求参数体现新模型名 | 要和 provider 对应起来看 |
| `ai_api_key` | 冷启动 | 改 key 并重启；发送真实 AI trace | 正确 key 可成功，错误 key 可稳定失败 | 这步主要证明配置真实传到了 provider |
| `ai_language` | 冷启动 | 改成中文或英文；保存并重启；发送同类 trace | AI 输出语言随之变化 | 这里看 `summary/root_cause/solution` 的语言，不看 UI |
| `active_prompt_id` | 冷启动 | 切换 active prompt；保存并重启；发送同类 trace | 新 trace 的 AI 输出口径明显按新 prompt 变化 | 要准备两套差异明显的业务 prompt |
| `prompts` | 冷启动 | 修改 prompt 内容；保存并重启；发送同类 trace | AI 输出内容体现新的业务约束 | 推荐修改领域词汇或输出偏好，方便观察 |
| `ai_circuit_breaker` | 冷启动 | 开启后保存并重启；人为制造连续失败直到达到阈值 | 后续 trace 进入 `skipped_circuit` | 关闭后同样场景不应进入 `skipped_circuit` |
| `ai_failure_threshold` | 冷启动 | 改成 `1` 或 `2`；保存并重启；重复制造失败 | 熔断触发次数与新阈值一致 | 这是熔断触发门槛，不是重试次数 |
| `ai_cooldown_seconds` | 冷启动 | 改成一个较短值；保存并重启；触发熔断后等待冷却时间 | 冷却窗口内 `skipped_circuit`，窗口后恢复尝试 | 要带绝对时间观察，不要凭感觉 |
| `ai_auto_degrade` | 冷启动 | 开启自动降级；保存并重启；让主 provider 失败、备 provider 成功 | trace 最终 `completed`，且能看出是 fallback 成功 | 关闭时同场景应直接失败 |
| `ai_fallback_provider` | 冷启动 | 改备 provider 并重启；制造主路失败 | fallback 实际走到目标 provider | 当前如果还是 `mock`，要明确这是最小降级链 |
| `ai_fallback_model` | 冷启动 | 改备模型并重启；制造主路失败 | fallback 请求参数体现新模型名 | 和主模型要区分开 |
| `ai_fallback_api_key` | 冷启动 | 改备 key 并重启；制造主路失败 | 正确 key 时 fallback 成功，错误 key 时主备都失败 | 这一步证明降级链路有独立凭证 |

## 6.4 Webhook 外发类

| 配置项 | 当前口径 | 验证动作 | 通过标准 | 备注 |
| --- | --- | --- | --- | --- |
| `channel.enabled` | 冷启动 | 关闭某个飞书 channel，保存并重启，发送 `critical` trace | 该 channel 不再收到消息 | 如果有多个 channel，要确认只影响目标项 |
| `channel.name` | 展示字段 | 修改名称，保存并刷新设置页 | 页面能正确回填新名称 | 名称本身不影响外发逻辑 |
| `channel.webhook_url` | 冷启动 | 改成新的有效 webhook，保存并重启，发送 `critical` trace | 消息发到新的群，不再发到旧群 | 这是最直接的外发证明 |
| `channel.secret` | 冷启动 | 对开启签名校验的飞书机器人填正确或错误 secret，保存并重启 | 正确 secret 发送成功，错误 secret 发送失败 | 当前 secret 只服务飞书签名 |
| `channel.threshold` | 冷启动 | 把阈值从 `critical` 改成 `warning` 或 `error`，保存并重启 | 低于阈值的告警不发，达到阈值的告警发 | 这是“从哪个风险等级开始外发”，不是展示标签 |

---

## 7. 建议的最小演示场景

如果只准备一轮最小答辩演示，建议固定这 8 条：

1. `app_language` 切中文/英文
2. `http_port` 改端口后重启生效
3. `trace_end_aliases` 改别名后重启生效
4. `ai_analysis_enabled=false` 后新 trace 进入 `skipped_manual`
5. `ai_circuit_breaker + ai_failure_threshold=1` 后快速进入 `skipped_circuit`
6. `ai_auto_degrade=true` 后主失败、备成功
7. 切换 `active_prompt_id` 后 AI 输出口径变化
8. 调整飞书 `threshold` 或 `webhook_url` 后外发行为变化

这 8 条已经足够支撑“设置不是假页面，后端主链真的在吃”的核心论点。

---

## 8. 联调记录模板

建议每跑完一项，就直接补一行结果，别等最后再回忆：

| 日期 | 配置项 | 操作 | 结果 | 备注 |
| --- | --- | --- | --- | --- |
| 2026-04-11 | `app_language` | `zh -> en` 保存后刷新页面 | 通过/失败 | 具体现象 |

---

## 9. 本轮完成标准

这份清单跑完后，至少要达到下面这个收口标准：

1. 能明确区分哪些字段是热更新，哪些字段是冷启动。
2. 能拿出 5 到 8 条已经跑通过的设置联调证据。
3. 能说清楚未纳入本轮的字段为什么暂时不验。
4. 能把这份清单直接复用到后续 `GLM`、benchmark 和答辩演示里。
