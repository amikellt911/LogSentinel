# Benchmark Suite Overview

这份文档只负责把 benchmark 的 suite 分类、比较目标、观测指标和资源维度先钉住。

当前明确不写死：
- 具体 wrk 命令
- 具体 CPU 绑核方案
- 具体机器型号和最终服务器拓扑
- 最终论文图表样式

原因很简单：这轮先把“测什么”说清楚，真正跑 benchmark 要等功能收口后，在干净服务器上统一执行。

## 总体原则

本轮 benchmark 按 4 个实验块组织，不再把所有参数混成一个大杂烩。

目标不是一口气把所有变量做笛卡尔积，而是让每个 suite 只回答一种问题：

Suite A 回答：系统各个功能模块本身带来了多少额外代价。

Suite B 回答：AI 可靠性策略在错误场景下到底有没有收益。

Suite D 回答：资源数量和线程配置变化后，系统扩展性如何。

额外保留一个小型交互实验：

Suite A-Interaction 回答：同一个架构设计在不同资源区间里，收益会不会发生变化。

当前明确：

- `Suite C` 暂不执行
- 不做 `A × CPU × worker` 全矩阵
- 只保留一个小而可解释的交互实验

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

### 资源约束

Suite A 不主动展开 CPU/worker 维度。

这里故意固定：

- 机器资源
- 后端可用 CPU
- `kernel_worker_threads`

原因：

Suite A 的任务是回答“功能代价”，不是回答“扩展性”。

如果一开始就把功能开关和 CPU/worker 一起扫，最后就会分不清：

- 是开关本身贵
- 还是资源变化带来的
- 还是两者交互带来的

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

## Suite D：资源扩展性

### 目标

这组不比较功能开关，也不比较可靠性策略。

它只回答一个问题：

在资源变化时，当前系统能不能稳定扩上去，以及拐点在哪。

### D1：固定 CPU，只扫 worker

这条线观察：

- `kernel_worker_threads` 增大后，QPS 是否继续上涨
- p95 / p99 是否先改善后恶化
- 是否出现线程过多导致上下文切换、锁竞争、SQLite 压力增大

这里的口径是：

- 固定后端可用 CPU 数量
- 固定 benchmark 流量模型
- 只改 `kernel_worker_threads`

### D2：固定 worker 策略，只扫 CPU

这条线观察：

- 后端可用 CPU 数量增加后，吞吐是否继续增长
- `BufferedTraceRepository` 这类架构收益是否在更高资源下更明显
- 背压和 SQLite 写热点是否在不同核数区间表现不同

这里的口径是：

- 固定一套代表性 worker 配置
- 固定 benchmark 流量模型
- 只改后端可用 CPU 数量

### 主要比较内容

- ingest QPS
- `/logs/spans` p50 / p95 / p99
- CPU 占用
- RSS
- 背压触发率
- SQLite flush 压力

### 约束

Suite D 不和 Suite A 主实验混成全矩阵。

原因：

如果你同时扫：

- 功能开关
- CPU 数量
- worker 数量

那最后虽然图很多，但解释会非常脏。

所以 D 的职责是：

先单独回答“系统扩不扩得起来”。

## Suite A-Interaction：架构收益与资源区间的交互

### 目标

这组不是主实验，只是一个小型交互实验。

它回答的问题是：

某个架构设计是不是在所有资源区间都更好，还是只在某些资源区间更有优势。

### 当前主线

当前优先选择：

- `baseline`
- `disable_buffered_trace_repo`
- `no_ai_no_buffer`

核心观察对象是：

`buffered` vs `no-buffer`

### 为什么优先选这条线

因为这条线更像系统架构本身，而不是功能裁剪。

它直接对应：

- 写入口是否双缓冲
- 前台 ingest 和后台 flush 是否解耦
- SQLite 写热点在不同资源区间里会不会被削峰

相对地，`AI on/off` 虽然也能对比，但它混入了：

- Python proxy
- provider 延迟
- 网络等待
- JSON 解析

所以它更适合留在 Suite A 主实验里当“功能成本”，不适合作为“架构优势”的主交互论证。

### 当前口径

这组不做大矩阵，只做小交互。

推荐形式：

- 选少量代表性资源点，例如低资源 / 高资源两档
- 只比较少量架构配置

原因：

这组的目标不是把图做大，而是把一句关键结论说清楚：

同一个架构设计，其收益可能随着资源区间变化而变化。

## Suite C：Trace 生命周期 / 聚合策略（暂不执行）

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

### 当前决定

这组保留在文档里，但 `v1.0.0` 暂不执行。

原因：

- 实现和实验成本都高
- 变量耦合重
- 对当前论文主线不是必须项

## 单机 / 双机拓扑说明

### 单机场景

如果 benchmark 在同一台机器上同时运行：

- 后端
- `wrk`
- AI proxy / 本地故障注入服务

那么后端“可用 CPU”不能直接等于整机总核数。

这时需要把客户端和辅助服务的核占用一起考虑进去。

### 单机场景下的默认资源点

当前推荐的资源点先固定为：

- `4 核`
- `8 核`
- `16 核`
- `24 核`
- `32 核`

如果最终租到的机器没有 `32` 核，就以 `24` 核作为最高资源点收口。

这里的目的不是覆盖所有机器，而是让扩展性曲线先具备：

- 低资源点
- 中间资源点
- 高资源点

这样后面无论是做 `Suite D` 还是做 `Suite A-Interaction`，都能拿到一条能解释“拐点”的曲线。

### 单机场景下的默认进程级分核建议

当前只做进程级分核，不继续细分：

- `wrk`
- `ai-mock`
- `backend`

后端内部线程暂时不单独绑核。

原因：

这轮 benchmark 主问题还是：

- 功能代价
- 可靠性收益
- 扩展性拐点

如果现在继续把 `MiniMuduo IO`、`worker`、`flush`、`dispatch` 全拆开绑核，变量会立刻爆炸，反而不利于解释。

推荐默认分配如下：

`4 核`

- `wrk = 1`
- `ai-mock = 1`
- `backend = 2`

`8 核`

- `wrk = 2`
- `ai-mock = 1`
- `backend = 5`

`16 核`

- `wrk = 2`
- `ai-mock = 1`
- `backend = 13`

`24 核`

- `wrk = 3`
- `ai-mock = 2`
- `backend = 19`

`32 核`

- `wrk = 4`
- `ai-mock = 2`
- `backend = 26`

### 为什么 `ai-mock` 不是一直按比例涨

当前 `ai-mock` 的行为更接近：

- `FastAPI async 路由`
- `run_in_threadpool`
- `MockProvider.analyze_trace`
- `time.sleep`

所以它不是持续 CPU 密集型负载。

既然主要瓶颈不是纯计算，那么在 `4/8/16` 这种资源区间里，给它 `1` 个核通常足够。

但是到了 `24/32` 这类更高资源点，如果仍然只给 `1` 个核，`ai-mock` 自己的：

- event loop 调度
- 线程池切换
- JSON 序列化 / 反序列化
- socket 收发

就可能先变成假瓶颈，反而把后端 benchmark 做歪。

所以当前默认规则收口成：

- `<=16 核`：`ai-mock = 1`
- `>=24 核`：`ai-mock = 2`

### 这份分配表的语义

这不是“绝对最优绑定”，而是当前 benchmark 的默认起步配置。

如果后续实测发现：

- `wrk` 本身打不满后端
- `ai-mock` 提前成为瓶颈
- 或者双机场景不再需要给 `wrk` 留这么多核

那么可以在执行阶段调整，但调整必须记录在实验表里，不能一边跑一边默默改。

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

再收口 Suite D

最后如果时间还够，再决定要不要补 Suite A-Interaction

原因：

Suite A 最适合做主性能图，而且当前开关已经都实现好了。

Suite B 很适合做论文里的可靠性收益图，但它依赖本地可控故障注入条件。

Suite D 负责把“系统扩展性”单独讲清楚，不跟功能成本混讲。

Suite A-Interaction 更像锦上添花，应该放在主图之后；这样即使时间不够，也不会影响 benchmark 主线成立。

## 当前状态

当前已经 ready 的前置条件：

- `--disable-ai`
- `--disable-buffered-trace-repo`
- `--disable-webhook`
- `ai_retry_enabled / ai_retry_max_attempts`
- `ai_auto_degrade`
- `ai_circuit_breaker`

当前还没有写死的内容：

- Suite D 每个资源点下最终选哪几个 `kernel_worker_threads`
- Suite A-Interaction 最终取哪两个资源点
- 具体 wrk 参数
- 最终结果模板和截图模板

这部分要等功能完全收口后，再在干净服务器上统一敲定。
