# Benchmark Suite Overview

这份文档只负责把 benchmark 的 suite 分类、比较目标、观测指标和资源维度先钉住。

当前明确不写死：
- 具体 wrk 命令
- 具体 CPU 绑核方案
- 具体机器型号和最终服务器拓扑
- 最终论文图表样式

原因很简单：这轮先把“测什么”说清楚，真正跑 benchmark 要等功能收口后，在干净服务器上统一执行。

## 总体原则

本轮 benchmark 按 3 个 suite 组织，不再把所有参数混成一个大杂烩。

目标不是一口气把所有变量做笛卡尔积，而是让每个 suite 只回答一种问题：

Suite A 回答：系统各个性能模块本身带来了多少额外代价。

Suite B 回答：AI 可靠性策略在错误场景下到底有没有收益。

Suite C 回答：Trace 聚合生命周期策略不同，会带来什么吞吐、时延和正确性差异。

## Suite A：性能型开关

### 目标

这组是主性能实验，核心问题是：

在同一套流量、同一套机器、同一套线程参数下，只切功能开关时，系统代价分别来自哪里。

### 当前开关

- `baseline`
- `disable_ai`
- `disable_buffered_trace_repo`
- `disable_webhook`
- `no_ai_no_buffer`

可选保留项，当前先不默认纳入主实验：

- `disable_service_runtime_metrics`

原因：

服务监控埋点本身后面还可能参与 benchmark 观测，如果过早把它关掉，容易把“采样工具”和“实验变量”搅在一起。

### 主要比较内容

- ingest QPS
- `/logs/spans` p50 / p95 / p99
- CPU 占用
- RSS
- SQLite flush 压力
- 背压触发率

### 解释口径

这组实验主要证明：

- AI 是否是主链额外开销的大头
- `BufferedTraceRepository` 是否真的有性能收益
- webhook 外发对主链影响是否显著
- “功能全开”和“裁剪模式”之间的代价差距有多大

### Webhook 说明

Suite A 默认不需要 fake webhook。

如果实验目标不是“专门测 webhook 外发成本”，那么直接 `--disable-webhook` 就够了。

只有在后续真的要补“开 webhook vs 关 webhook”的外发对比时，才考虑接一个本地最小 sink。这个 sink 只需要稳定接收 HTTP POST，不需要复杂探针能力。

## Suite B：AI 可靠性型开关

### 目标

这组不是纯吞吐实验，而是论文里很重要的“可靠性设计收益”实验。

它回答的问题是：

在 provider 出错、超时、限流这些场景下，重试、自动降级、熔断到底有没有让系统更稳。

### 当前开关

- `ai_retry_enabled`
- `ai_auto_degrade`
- `ai_circuit_breaker`

### 主要比较内容

- AI 完成率
- `completed / failed_primary / failed_both / skipped_circuit / skipped_manual` 状态分布
- 平均分析延迟
- 降级后的恢复能力
- 错误场景下的稳定性

### 故障注入原则

这组实验默认不接真实云 API。

应该优先使用本地可控故障注入，例如：

- `429`
- `5xx`
- 超时
- 主路失败、备路成功
- 连续失败触发熔断

原因：

如果接真实云 API，那么测出来的就会混入外网抖动、配额、第三方服务负载，最后论文里很难解释“到底是系统设计导致的，还是外部 API 今天正好慢”。

## Suite C：Trace 生命周期 / 聚合策略

### 目标

这组回答的问题不是“单个参数改多少更快”，而是：

不同 Trace 收口策略作为一个整体，会怎么影响吞吐、时延和正确性。

### 当前策略档位

- `strict_end_only`
- `two_stage_safe`
- `aggressive_timeout`

### 档位语义

`strict_end_only`

更偏保守，尽量依赖明确结束信号，不主动做太多超时收口。

`two_stage_safe`

也就是当前主线方案：

- sealed
- grace window
- TIME_WAIT tombstone

目标是兼顾乱序吸收、重复写防护和主链稳定性。

`aggressive_timeout`

更偏激进，优先追求更快收口和更低挂起时长，但可能增加晚到 span 丢失或错误提前收口的风险。

### 主要比较内容

- Trace 完成率
- 晚到 span 吸收能力
- 重复写 / 错误复活风险
- dispatch 时延
- 主链吞吐
- 正确性与性能之间的取舍

### 约束

这组不要直接把下面这些裸参数拆开乱跑：

- `sealed_grace_window_ms`
- `collecting_idle_timeout_ms`
- `trace_end`
- `token_limit`
- `span_capacity`

原因：

这些参数之间有明显耦合。如果直接散装 benchmark，最后很容易不知道到底是哪一个参数在起主要作用。

所以 Suite C 应该按“策略档位”做，而不是按“裸参数表”做。

## CPU 与线程条件

CPU 数量和线程数是 benchmark 的资源维度，但当前先记录为“条件轴”，不写死最终数值。

### 资源条件一：后端可用 CPU 数量

后端可用 CPU 数量是一个独立实验条件。

后续可以作为扩展性维度，用来观察：

- 吞吐是否随资源增加而提升
- `BufferedTraceRepository` 的收益是否随 CPU 数量变化
- worker 竞争、SQLite 压力和调度开销是否在更高资源下出现新的拐点

### 资源条件二：`kernel_worker_threads`

`kernel_worker_threads` 是另一个独立实验条件。

后续重点观察：

- worker 数增加是否真的提升吞吐
- 是否出现线程过多导致上下文切换、锁竞争、SQLite 压力变大
- 在不同 suite 下，最合适的 worker 数是否一致

### 当前口径

这两个条件先作为“实验轴”记录，暂时不写死最终矩阵。

原因：

- 单机 benchmark 时，还要考虑 `wrk`、AI proxy、本地最小外部服务和系统杂项的核占用
- 双机 benchmark 时，资源口径又会变化
- 如果现在把数值先写死，后面换服务器或换拓扑时，文档会先失真

## 单机 / 双机拓扑说明

### 单机场景

如果 benchmark 在同一台机器上同时运行：

- 后端
- `wrk`
- AI proxy / 本地故障注入服务

那么后端“可用 CPU”不能直接等于整机总核数。

这时需要把客户端和辅助服务的核占用一起考虑进去。

### 双机场景

如果后续改成：

- 一台压测机跑 `wrk`
- 一台服务机跑后端

那么后端的 CPU 口径会更干净，也更适合做最终论文数据。

所以当前 benchmark 方案不预设单机或双机，而是先把 suite 语义独立下来，后续执行时再绑定具体拓扑。

## 执行顺序建议

当前建议的执行顺序：

先收口 Suite A

再收口 Suite B

最后再收口 Suite C

原因：

Suite A 最适合做主性能图，而且当前开关已经都实现好了。

Suite B 很适合做论文里的可靠性收益图，但它依赖本地可控故障注入条件。

Suite C 更像策略设计对比，应该放在前两组之后，避免一开始就把变量搅复杂。

## 当前状态

当前已经 ready 的前置条件：

- `--disable-ai`
- `--disable-buffered-trace-repo`
- `--disable-webhook`
- `ai_retry_enabled / ai_retry_max_attempts`
- `ai_auto_degrade`
- `ai_circuit_breaker`

当前还没有写死的内容：

- 每个 suite 最终跑哪些 profile
- 每个 suite 最终跑几组线程 / CPU
- 具体 wrk 参数
- 最终结果模板和截图模板

这部分要等功能完全收口后，再在干净服务器上统一敲定。
