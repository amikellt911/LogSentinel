# Benchmark Suite Overview

这份文档只负责把 benchmark 的 suite 分类、比较目标、观测指标和资源维度先钉住。

当前明确不写死：
- 具体 wrk 命令
- 具体 CPU 绑核方案
- 具体机器型号和最终服务器拓扑
- 最终论文图表样式

原因很简单：这轮先把“测什么”说清楚，真正跑 benchmark 要等功能收口后，在干净服务器上统一执行。

## 统一变量口径

后续 benchmark 不再笼统说“线程数”。

统一拆成 3 层：

- 进程资源层
  - `wrk_cpu_cores`
  - `backend_cpu_cores`
  - `ai_proxy_cpu_cores`
- 后端线程层
  - `server_io_threads`
  - `worker_threads`
- AI 代理层并发层
  - `ai_proxy_max_workers`

各自语义如下：

`wrk_cpu_cores`

压测机或单机压测进程分到的 CPU 核数。

`backend_cpu_cores`

后端服务进程实际可用的 CPU 核数。

`ai_proxy_cpu_cores`

Python AI proxy 进程分到的 CPU 核数。

`server_io_threads`

服务器 I/O 线程数，对应设置里的 `kernel_io_threads`。

`worker_threads`

C++ 主工作线程池线程数，对应设置里的 `kernel_worker_threads`。

`ai_proxy_max_workers`

Python AI proxy 允许同时执行的阻塞 provider 调用上限，对应 `main.py --max-workers`。
这个变量控制的是本地代理层并发，不等于外部模型服务真实配额。

当前文档里，`server_io_threads` 不再被全局写死。

当前语义改成：

- Suite A / Suite B：复用 Suite D 先校准出来的代表性拓扑
- Suite D：把 `server_io_threads` 作为线程拓扑校准的一部分

原因：

现在 `/logs/spans` 这条链里，I/O 线程不只收包，还会承担 HTTP 解析和 JSON 解析。
如果继续把它拍脑袋固定成 `1`，后面很多 benchmark 结论都会失真。

## 总体原则

本轮 benchmark 按 4 个实验块组织，不再把所有参数混成一个大杂烩。

目标不是一口气把所有变量做笛卡尔积，而是让每个 suite 只回答一种问题：

Suite A 回答：系统各个功能模块本身带来了多少额外代价。

Suite B 回答：Trace 生命周期防护在脏时序场景下到底有没有收益。

Suite D 回答：资源数量和线程配置变化后，系统扩展性如何。

补充实验可选保留：

- AI 可靠性策略实验
- 小型交互实验

其中 `Suite A-Interaction` 回答：同一个架构设计在不同资源区间里，收益会不会发生变化。

当前明确：

- 原来文档里的 `Suite C` 生命周期实验思路，上收并并入新的 `Suite B`
- 原来文档里的 “AI 可靠性型 Suite B”，下放成补充实验，不占主论文主图
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

当前先固定成单机 `16 核` 锚点：

- `wrk_cpu_cores = 2`
- `ai_proxy_cpu_cores = 2`
- `backend_cpu_cores = 12`

线程拓扑同样固定：

- `server_io_threads`
- `worker_threads`
- `ai_proxy_max_workers`

原因：

Suite A 的任务是回答“功能代价”，不是回答“扩展性”。

如果一开始就把功能开关和 CPU/worker 一起扫，最后就会分不清：

- 是开关本身贵
- 还是资源变化带来的
- 还是两者交互带来的

所以 Suite A 里的线程参数不单独扫描，而是直接复用 Suite D 先校准出来的那套代表性拓扑。

### Webhook 说明

Suite A 默认不需要 fake webhook。

如果实验目标不是“专门测 webhook 外发成本”，那么直接 `--disable-webhook` 就够了。

只有在后续真的要补“开 webhook vs 关 webhook”的外发对比时，才考虑接一个本地最小 sink。这个 sink 只需要稳定接收 HTTP POST，不需要复杂探针能力。

## Suite B：Trace 生命周期鲁棒性

### 目标

这组不是纯吞吐实验，而是当前论文主线里最该证明“系统设计优势”的实验。

它回答的问题是：

既然真实 span 会乱序、晚到、重复、断尾，那么当前这套生命周期防护相比朴素策略，到底有没有更稳。

### 当前策略档位

- `protected`
- `minimal`

### 实验入口

Suite B 不走正式 Settings 页面，统一走 benchmark CLI：

- `benchmark` runner 通过环境变量 `TRACE_LIFECYCLE_PROFILE=protected|minimal`
- 脚本再透传成后端启动参数 `--trace-lifecycle-profile protected|minimal`

这样做的原因很直接：

- 生命周期档位本身是冷启动语义
- benchmark 需要频繁切换对照组
- 如果每次都先改 SQLite，再重启服务，实验变量就会和产品配置混在一起

所以这组实验明确按“CLI override > SQLite 冷启动值”执行。

### 档位语义

`protected`

就是当前主线语义：

- `trace_end / capacity / token_limit` 命中后，不立刻最终关单，而是先进入 sealed
- sealed grace window 继续吸收短暂乱序 span
- dispatch 完成后继续保留 tombstone / TIME_WAIT 防护
- 用来拦截已完成 trace 的晚到尾巴和重复写

`minimal`

当前已经定死为：

- 仍然统一走 `sweep -> dispatch queue -> dispatch worker` 主路径
- 不做 sealed grace
- 不保留 completed tombstone

原因很直接：

- `protected` 和 `minimal` 继续共享同一套 dispatch 执行路径
- 实验变量真正只落在“有没有生命周期防护”，不会把“调度模型也换了”一起搅进去
- 后面如果测出差异，更容易说明是 sealed/tombstone 起了作用，而不是 direct dispatch 换了线程时间线

### 当前状态流对照

下面这份对照，已经按当前落地实现来理解。
也就是说，`protected` 和 `minimal` 都仍然走同一套：

- `Push`
- `SweepExpiredSessions`
- `dispatch queue`
- `dispatch worker`

差异只放在生命周期防护层本身。

`protected`

- span 进入后，正常累计到 session
- 命中 `trace_end / capacity / token_limit` 时，不直接进入 ready，而是先进入 sealed
- sealed 期间继续吸收短暂晚到 span
- sweep 看到 sealed grace 到期，才真正摘走 session，送进 dispatch queue
- dispatch 完成后，继续保留 completed tombstone
- 如果这时又来了晚到 span，会先被 tombstone 拦住，不让它错误复活成一条新 trace

`minimal`

- span 进入后，正常累计到 session
- 命中 `trace_end / capacity / token_limit` 时，不进入 sealed，直接标成 ready-to-dispatch
- idle timeout 命中时，同样直接标成 ready-to-dispatch
- 但是 ready 之后仍然不现场 direct dispatch，而是等下一次 sweep 统一摘走 session，送进 dispatch queue
- dispatch 完成后，不保留 completed tombstone
- 如果这时又来了晚到 span，就按新的输入继续处理，不再有 completed 防护层

所以当前预期最核心的实验差异是：

- `protected` 能吸收 sealed grace 窗口内的短暂乱序
- `protected` 能拦截已完成 trace 的晚到尾巴和重复 replay
- `minimal` 的执行线程时间线与 `protected` 尽量保持一致，但会暴露更多误拆分、误复活和重复落库风险

### 场景设计原则

这组不该用 wrk 做主实验。

原因：

- wrk 适合打吞吐
- 生命周期鲁棒性实验需要精确控制一条 trace 内各个 span 的到达顺序和延迟
- 所以这里更适合 paced sender / 自定义脚本，专门造“脏时序”

### 当前目标场景

- `trace_end` 先到，子 span 晚到
- 子 span 先到，父 span 晚到
- 重复 span / replay
- 一条 trace 刚完成，短时间又来尾巴
- 多条 trace 相邻到达，容易误混或误拆

### 当前讨论中的场景建模路线

为了避免后面又把 `Suite B` 的场景设计口径说乱，这里把已经讨论过的两条路线先记下来。

路线 A：按 trace 维度做“脏场景注入”

- 思路：
  - 大多数 trace 保持正常；
  - 少量 trace 按概率命中 `late_child_before_grace`；
  - 少量 trace 按概率命中 `late_tail_after_completion`。
- 优点：
  - 解释最直接，答辩时容易把每个场景和生命周期机制一一对应；
  - 归因更稳，不容易把随机噪声和机制差异混在一起；
  - 更适合做确定性探针或附录里的“机制自证图”。
- 风险：
  - 如果所有异常都按“整条 trace 选场景”来造，真实感会弱一点；
  - 容易被追问“是不是为了打中某个 case 专门喂毒流量”。

路线 B：按 span 维度做“随机延迟注入”

- 思路：
  - 不是先给整条 trace 指定一个固定场景；
  - 而是对每个 span 都注入随机延迟；
  - 大多数 span 不延迟或只带很小抖动；
  - 少量 span 会进入“短时乱序桶”；
  - 更少量 span 会进入“迟到污染桶”；
  - 必要时再给极少量 span 叠加 replay / duplicate。
- 进一步约束：
  - 这里不是“每个 span 完全独立无脑乱延迟”；
  - 更合理的是按 span 角色分桶，例如：
    - child / parent span 更容易命中短时乱序；
    - tail / replay span 更容易命中 completed 之后的迟到污染。
- 优点：
  - 更像真实微服务系统里的异步回调、跨线程上报、网络抖动和重试重传；
  - 同一轮实验里可以自然同时出现“短时乱序”和“完成后污染”；
  - 论文或答辩里更容易说成“以正常流量为主、随机混入少量脏时序”。
- 风险：
  - 如果延迟桶设计太散，最后虽然更像真实世界，但机制归因会变差；
  - 所以后续仍建议保留少量确定性探针，用来证明 `sealed grace` 和 `tombstone/TIME_WAIT` 这两把刀确实被打中了。

当前判断：

- `Suite B` 可以继续优先沿路线 B 收口，也就是“按 span 角色分桶的随机延迟注入”；
- 路线 A 不删除，保留为机制探针/附录/内部校验思路；
- 这两条路线当前都只服务 `Suite B`；
- `Suite A / Suite D` 的目标分别是功能成本和资源扩展性，不适合直接复用这套脏时序建模。

### Sender 建模口径

`Suite B` 的正式发生器不再沿用 `wrk`。

这里统一收口成：

- 用独立的 Python sender / scheduler 负责发包；
- sender 先生成“理想顺序”的 trace 模板；
- 然后按 span 角色抽样延迟桶；
- 最后按目标发送时间调度成真实 HTTP 请求。

这样做的原因很直接：

- `Suite B` 要证明的是生命周期防护收益，不是纯 QPS；
- 如果还继续让 `wrk` 充当主发生器，就很难精确控制一条 trace 内部的乱序、晚到和 replay；
- sender 自己必须能把“这条脏样本是怎么造出来的”记录下来，后面才能复现和解释。

### Sender 内部结构

当前 sender 先固定成 3 层：

- `TraceTemplateGenerator`
- `DelaySampler`
- `Scheduler`

`TraceTemplateGenerator`

- 负责生成“理想顺序”的一条 trace；
- 先给每个 span 一个基础发送时间 `base_emit_at_ms`；
- 同一条 trace 内，span 默认仍按正常拓扑顺序排开。

`DelaySampler`

- 不先按“整条 trace 命中一个场景”造数据；
- 而是按 span 角色给每个 span 抽一个延迟桶；
- 大多数 span 保持正常，只带很小抖动；
- 少量 span 进入短时乱序；
- 更少量 span 进入完成后晚到；
- 极少量 span 再叠一层 replay clone。

`Scheduler`

- 只负责按最终的目标发送时间把 span 推出去；
- 同时记录实际抽样结果，保证复现和归因。

### 调度数据结构

当前明确选 `min-heap`，不选 timing wheel。

原因：

- `Suite B` 需要的是“尽量精确的目标发送时间”，而不是粗粒度 tick 调度；
- 如果 sender 自己也再做一层时间轮，很多 span 会先被发生器的人造 tick 对齐，反而把后端 `sealed grace / tombstone` 的真实效果冲淡；
- `min-heap` 更容易表达“某个 span 比另一个 span 晚 120ms 或 780ms”这种细粒度时间关系；
- sender 的规模不会大到必须靠时间轮省调度开销，这里优先保真，不优先极限性能。

### 调度事件字段

当前 sender 的最小调度单元统一叫 `ScheduledSpanEvent`。

建议至少包含下面这些字段：

- `trace_id`
- `span_id`
- `parent_span_id`
- `role`
- `base_emit_at_ms`
- `planned_emit_at_ms`
- `delay_bucket`
- `is_replay_clone`
- `rng_seed_fragment`

字段语义：

- `base_emit_at_ms` 表示理想正常顺序下本来该什么时候发；
- `planned_emit_at_ms` 表示注入延迟桶之后，最终准备什么时候发；
- `delay_bucket` 用来复盘这条 span 为什么晚了；
- `is_replay_clone` 用来区分“原始 span 晚到”和“旧 span 被复制后重放”；
- `rng_seed_fragment` 用来把随机过程钉死，方便复现实验。

### 时间窗口口径

后续所有延迟桶都绑定后端真实生命周期参数，不再拍脑袋写死绝对毫秒。

统一定义：

- `tick = wheel_tick_ms`
- `grace = sealed_grace_window_ms`
- `tombstone_window = completed_trace_tombstone_ticks * wheel_tick_ms`

以当前常见默认口径举例：

- `tick = 500ms`
- `grace = 1000ms`
- `tombstone_window = 25 * 500ms = 12.5s`

如果 benchmark profile 覆盖了 `wheel_tick_ms / grace`，sender 的桶范围也跟着一起重新计算。

### 延迟桶定义

当前先固定 4 个桶，够支撑主实验，不再继续发散。

`clean_jitter`

- 范围：`[0, min(0.2 * tick, 40ms)]`
- 语义：正常网络小抖动，不刻意制造生命周期问题。

`reorder_in_grace`

- 范围：`[max(2 * base_gap, 0.5 * tick), grace - 0.25 * tick]`
- 语义：span 会比预期更晚，但仍然落在 sealed grace 窗口内；
- 主要用于证明 `protected` 能补全，而 `minimal` 更容易误拆或缺 span。

`late_after_dispatch`

- 范围：`[grace + 1 * tick, min(grace + 3 * tick, 0.35 * tombstone_window)]`
- 语义：span 已经明显晚到，目标是打到“dispatch 之后”的完成态防护；
- 主要用于观察 `tombstone/TIME_WAIT` 是否真的拦住已完成 trace 的尾巴。

`replay_after_dispatch`

- 这不是原始 span 的普通延迟桶，而是“复制一份旧 span，再晚发一次”；
- 范围：`[grace + 1.5 * tick, min(grace + 4 * tick, 0.5 * tombstone_window)]`
- 语义：模拟上游重传、补发、重复上报。

### Span 角色与默认抽样

当前角色先固定成 3 类：

- `head/root`
- `body`
- `tail/end`

默认抽样口径如下：

- `head/root`：`95% clean_jitter`，`4% reorder_in_grace`，`1% late_after_dispatch`
- `body`：`84% clean_jitter`，`10% reorder_in_grace`，`5% late_after_dispatch`，`1% replay clone`
- `tail/end`：`98% clean_jitter`，`2% reorder_in_grace`

这里故意让 `tail/end` 更容易准时。

原因：

- 真实系统里，尾部完成事件往往更接近业务线程的主收尾点；
- 一旦 `body` 比 `tail/end` 更容易被拖慢，就能更自然地形成“end 先到、body 后到”；
- 这样打出来的是“像真实系统偶发脏时序”的流量，不是明显的人造毒样本。

### Suite B 收口后的场景矩阵

为了避免后面把场景越拉越多，`Suite B` 现在正式收口成 `3 x 2` 矩阵：

- 发送侧 3 个 profile
- 生命周期侧 2 个 profile：`protected` / `minimal`

也就是总共 `6` 组运行。

发送侧 profile 固定成下面 3 档：

| Sender Profile | 语义 | 发送侧脏流量口径 | 主要用途 |
| --- | --- | --- | --- |
| `clean_baseline` | 近正常流量 | 只保留 `clean_jitter`，关闭 `late_after_dispatch` 和 `replay` | 校验 `protected` 不会在正常流量下带来明显副作用 |
| `mixed_realistic` | 主实验流量 | 使用“默认抽样口径”，也就是大多数正常、少量短时乱序、更少量晚到和 replay | 主论文主图优先候选，最接近真实微服务偶发脏时序 |
| `late_replay_stress` | 放大差异流量 | 在默认口径上显著抬高 `late_after_dispatch` 和 `replay` 的占比，但仍保留多数正常 span | 放大 tombstone/TIME_WAIT 防护收益，适合附图或补充图 |

当前推荐的展示优先级：

- 主图优先看 `mixed_realistic`
- `clean_baseline` 作为 sanity check
- `late_replay_stress` 作为机制放大图或附录图

也就是说，答辩时真正重点讲的不是“我造了很多极端毒流量”，而是：

- 正常流量下，两套策略都不该明显跑偏；
- 混合脏流量下，`protected` 开始体现收益；
- 当晚到和 replay 稍微放大时，这种收益会被更清楚地看见。

### 主要比较内容

当前不再把 `Suite B` 的指标平铺成一长串。

这里正式收口成 3 层：

- 主指标
- 辅助正确性指标
- 辅助护栏指标

原因：

- `Suite B` 的主任务是证明生命周期防护有没有换来“正确性收益”；
- 但如果完全不看代价，答辩时很容易被追问“那你这个保护是不是很贵”；
- 所以这里既不能把性能指标抢成主角，也不能完全不留性能护栏。

主指标固定成下面 3 个：

- `trace_completeness_rate`
- `trace_pollution_rate`
- `duplicate_persistence_rate`

各自语义如下：

`trace_completeness_rate`

- 一条 trace 最终落库后，是否保住了本该属于它的 span；
- 这是 `sealed grace` 最该打出的主收益指标；
- 主图里优先展示这一项。

`trace_pollution_rate`

- 一条已经完成的 trace，后续晚到 span 或 replay 是否把它错误复活，或者污染成新旧混杂的结果；
- 这是 `tombstone/TIME_WAIT` 最该打出的主收益指标；
- 如果答辩老师问“为什么不能只靠 trace_end”，这项最有说服力。

`duplicate_persistence_rate`

- 同一语义上的 span 或 trace 结果，是否被重复落库；
- 这项主要补齐 replay / duplicate 这条线。

辅助正确性指标保留 2 个：

- `final_query_correctness`
- `misclassification_rate_for_late_spans`

`final_query_correctness`

- 指从最终查询视角看，用户能不能查到正确的 trace 结果；
- 这项更偏“产品视角”的正确性，不一定进主图，但适合做补充表。

`misclassification_rate_for_late_spans`

- 指晚到 span 被错误吸收、错误丢弃或错误归入新 trace 的比例；
- 这项更像诊断指标，适合内部分析或附录解释，不一定放到主论文图。

辅助护栏指标固定成下面 2 个：

- `ingest_p95_latency_delta`
- `backend_cpu_delta`

`ingest_p95_latency_delta`

- 比较同一档 sender profile 下，`protected` 相比 `minimal` 的 `/logs/spans` p95 延迟增量；
- 这是 `Suite B` 最重要的代价护栏；
- 如果只能留 1 个护栏指标，优先保这个。

`backend_cpu_delta`

- 比较同一档 sender profile 下，两种生命周期档位的后端 CPU 增量；
- 这项不抢主图，但可以防止别人追问“是不是靠吃更多 CPU 换来的正确性”。

当前答辩展示口径建议固定成：

- 主图：`trace_completeness_rate`、`trace_pollution_rate`、`duplicate_persistence_rate`
- 补充图或表：`final_query_correctness`
- 护栏图或表：`ingest_p95_latency_delta`
- 需要时再补：`backend_cpu_delta`、`misclassification_rate_for_late_spans`

也就是说：

- `Suite A` 继续负责回答“总体性能成本高不高”；
- `Suite B` 只保留最少量的性能护栏，用来证明“正确性收益不是白拿，但代价也没失控”。

### 指标计算口径

为了避免后面跑 benchmark 时同一个词各说各话，`Suite B` 的指标统一按：

- sender manifest
- 最终 SQLite 快照
- 后端运行日志

这 3 份材料联合计算。

也就是说：

- 不能只看 SQLite 最终表，因为有些错误会体现成持久化冲突或错误尝试；
- 也不能只看日志，因为最后用户真正查到什么，同样要回到最终查询结果和最终落库状态。

### Sender 必须输出的真值标签

`Suite B` 的 sender 不能只负责发包，还必须给每个事件打真值标签。

每个 `ScheduledSpanEvent` 至少要带下面这些评估字段：

- `logical_trace_id`
- `span_id`
- `event_kind = original | replay_clone`
- `delay_bucket`
- `expected_final_action = merge_into_final_trace | ignore_after_cutoff`

这里把“到底什么算正确”先钉死：

- `clean_jitter` 下的原始 span：`merge_into_final_trace`
- `reorder_in_grace` 下的原始 span：`merge_into_final_trace`
- `late_after_dispatch` 下的原始 span：`ignore_after_cutoff`
- `replay_after_dispatch` 下的 replay clone：`ignore_after_cutoff`

原因：

- `reorder_in_grace` 代表的是“应该被 sealed grace 吸收的短时乱序”；
- `late_after_dispatch / replay_after_dispatch` 代表的是“已经超过当前生命周期 cutoff，不该再改变最终 trace 结果”的事件；
- 不把这条边界先写死，后面 `completeness`、`pollution`、`misclassification` 三个指标一定互相打架。

### 统一观测集合

对每个 `logical_trace_id = t`，统一构造下面几份集合：

`ExpectedMergeSet(t)`

- sender manifest 里，所有 `expected_final_action = merge_into_final_trace` 的原始 `span_id` 去重集合；
- 它表示“这条 trace 最终本该保住的 span”。

`ExpectedIgnoreEvents(t)`

- sender manifest 里，所有 `expected_final_action = ignore_after_cutoff` 的事件集合；
- 它表示“这条 trace 上那些本不该再改变最终结果的晚到或 replay 事件”。

`PersistedSpanSet(t)`

- SQLite `trace_span` 表里，`trace_id = t` 的 `span_id` 去重集合。

`PersistedSummarySpanCount(t)`

- SQLite `trace_summary.span_count` 在 `trace_id = t` 上的最终值。

`RuntimeConflictLog(t)`

- 后端运行日志里，和 `trace_id = t` 对应的唯一键冲突、重复持久化尝试、异常落库告警等记录。

### 主指标的计算方式

`trace_completeness_rate`

- 分母：本轮 measurement window 内发出的全部 `logical_trace_id`
- 分子：满足 `ExpectedMergeSet(t)` 被 `PersistedSpanSet(t)` 完整覆盖的 trace 数

补一层约束：

- 这项只看“该保住的 span 有没有缺”
- 不因为存在污染或 replay 就直接判 incomplete
- 也就是说，额外脏数据由 `trace_pollution_rate` 单独负责

`trace_pollution_rate`

- 分母：本轮 measurement window 内发出的全部 `logical_trace_id`
- 分子：最终结果被“本不该进入最终 trace 的事件”污染过的 trace 数

当前判定 trace 被污染，满足任意一条即可：

- `PersistedSpanSet(t)` 里出现了不属于 `ExpectedMergeSet(t)` 的 span
- 某个 `ExpectedIgnoreEvents(t)` 对应的晚到或 replay 事件，最终改变了该 trace 的查询结果
- 或者该 trace 因这类事件触发了异常复活/错误再持久化，并在运行日志中留下对应冲突证据

`duplicate_persistence_rate`

- 分母：sender manifest 里全部 `event_kind = replay_clone` 或语义上属于 duplicate 注入的事件数
- 分子：这些事件里，最终引发了“重复持久化副作用”的事件数

这里的“重复持久化副作用”满足任意一条即可：

- 让最终结果多出了一条本不该存在的 span
- 让 `PersistedSummarySpanCount(t)` 与 `|ExpectedMergeSet(t)|` 明显偏离，并且偏离原因来自 replay/duplicate 注入
- 触发了唯一键冲突、重复写尝试或等价的持久化异常日志

也就是说，这项不只盯“数据库里最后有没有两行完全重复的数据”。

它更关心的是：

- replay / duplicate 到底有没有把系统推向“错误再写一次”的方向。

### 辅助指标的计算方式

`final_query_correctness`

- 分母：本轮 measurement window 内发出的全部 `logical_trace_id`
- 分子：通过最终查询接口拿到的 trace 详情，和 sender manifest 里的期望结果一致的 trace 数

一致的最小判定口径：

- span 集合与 `ExpectedMergeSet(t)` 一致
- summary 里的 `span_count` 与 `|ExpectedMergeSet(t)|` 一致
- parent / child 结构没有被错误污染

这项强调的是“最终用户查到的结果对不对”，所以它优先以查询接口口径为准，而不是只看底层 SQLite。

`misclassification_rate_for_late_spans`

- 分母：全部 `ExpectedIgnoreEvents(t)` 事件数
- 分子：这些事件里，被错误吸收、错误复活、错误触发再持久化的事件数

这项是诊断指标。

它主要帮助解释：

- 到底是哪类晚到事件最容易把 `minimal` 打穿。

### 护栏指标的计算方式

`ingest_p95_latency_delta`

- 在相同 sender profile、相同发送速率、相同 run duration、相同 CPU 绑核条件下；
- 分别测 `protected` 和 `minimal` 的 `/logs/spans` HTTP 响应 p95；
- 结果统一报：
  - 绝对增量：`p95(protected) - p95(minimal)`
  - 相对增量：`(p95(protected) - p95(minimal)) / p95(minimal)`

`backend_cpu_delta`

- 在相同 sender profile、相同发送速率和相同绑核条件下；
- 只统计 sender 正式 measurement window 内的后端进程 CPU；
- 结果统一报：
  - 平均 CPU 增量：`avg_cpu(protected) - avg_cpu(minimal)`
  - 可选补一条峰值 CPU 增量

当前原则：

- `Suite B` 的护栏指标只报增量，不和 `Suite A` 一样去卷极限吞吐；
- 这样老师问“代价多大”时，你有数字；
- 但整张图的主叙事，仍然是正确性收益，而不是性能比武。

### 固定资源口径

这组同样不把资源变量一起扫开。

当前先把 `Suite B` 正式冻结成下面这套单机拓扑：

- 发送端 / 压流脚本：`3` 核
- 后端：`13` 核
- `kernel_io_threads = 5`
- `dispatch_worker_threads = 4`
- `kernel_worker_threads = 16`
- `disable_ai = true`
- `disable_webhook = true`

补一层语义，避免后面再把线程名字看串：

- `kernel_io_threads = 5` 指的是 `5` 个 sub-reactor / I/O loop；
- 主 reactor 仍然单独存在，所以事件循环线程总数实际是 `1 + 5`；
- `dispatch_worker_threads = 4` 负责 dispatch queue 消费和主数据准备；
- `kernel_worker_threads = 16` 这里只承担第二阶段收尾，不再把 AI / webhook 这种外部阻塞一并算进去。

这里故意不再给 `ai_proxy` 预留 CPU。

原因很直接：

- `Suite B` 的目标是证明“生命周期防护有没有收益”，不是证明“带着外部 AI 阻塞时还能不能顶住”
- 既然这组主实验已经明确关闭 `AI/webhook`，那就不该再让 `ai_proxy` 进来制造额外变量
- 既然 AI/webhook 关闭后，worker 不再是主要阻塞池，那么这里把 `worker_threads` 收到中等规模即可，不需要走 `64/128` 这种阻塞线程思路

这套固定拓扑只服务 `Suite B`。

也就是说：

- 它不是 `Suite A` 的固定参数
- 也不是 `Suite D` 的线程拓扑答案
- 后面如果重新打开 `AI/webhook`，那 `worker_threads / ai_proxy_max_workers` 的权重还要重新评估

原因：

这组要回答的是“生命周期策略有没有收益”，不是“资源调大以后吞吐能不能继续涨”。
如果把脏时序场景和 CPU/线程一起混扫，最后就会分不清到底是策略本身稳，还是只是资源更多把问题吞掉了。

## 补充实验：AI 可靠性策略

这组从主实验降级为补充实验。

原因：

- 它能证明工程兜底做得是否完整
- 但它不是 Trace 主链路最核心的架构优势
- 放在附录或补充图更合适，不应该抢主图位置

如果后面要做，这组仍然推荐使用本地 fake AI server 注入：

- `429`
- `5xx`
- 超时
- 主路失败、备路成功
- 连续失败后恢复

主要看：

- `completed / failed_primary / failed_both / skipped_circuit / skipped_manual`
- 平均分析延迟
- 恢复时间
- 降级是否真的提升完成率

## Suite D：资源扩展性

### 目标

这组不比较功能开关，也不比较可靠性策略。

它只回答一个问题：

在资源变化时，当前系统能不能稳定扩上去，以及拐点在哪。

### 当前关键判断

当前 D 还没有完全定死，主要卡在“后端线程拓扑要不要先补能力”。

现在后端进程里至少有这些长期线程角色：

- `server_io_threads`
- `dispatch_thread`
- `flush_thread`
- `worker_threads`
- `query_tpool` 的查询线程

其中：

- `server_io_threads` 不是纯网络收包，它还会吃 HTTP 解析和 JSON 解析
- `dispatch_thread` 现在也不是纯搬运，它会做建树、序列化、summary/span record 组装
- `flush_thread` 更偏 SQLite 阻塞写
- `worker_threads` 才更像“混合阻塞型”，因为会等 AI HTTP / webhook

### 当前代码职责硬约束

为了避免后面再把 `dispatch` 和 `worker` 说反，这里把当前实现直接钉死：

- `server_io_threads`
  - 负责 HTTP 收包、JSON 解析、字段校验、组装 `SpanEvent`，然后调用 `TraceSessionManager::Push`
- `dispatch_thread`
  - 从 `dispatch_queue_` 取 `DispatchJob`
  - 在 `thread_pool_->submit(...)` 之前完成：
    - `BuildTraceIndex`
    - `SerializeTrace`
    - `BuildTraceSummary`
    - `BuildSpanRecords`
    - `AppendPrimary`
- `worker_threads`
  - 只负责 `thread_pool_->submit(...)` 进去之后的第二阶段
  - 开 AI 时主要承担 provider 调用、analysis 写入、webhook 外发
  - 关 AI 时主要剩余 `UpdateTraceAiState` 这类收尾动作

所以当前真实瓶颈风险不是“worker 一定先满”，而是：

- `server_io_threads` 可能先被入口解析压住
- `dispatch_thread` 可能先被单线程准备阶段压住
- `flush_thread` 可能先被 SQLite 写入压住

### 为什么不能直接复用现有 ThreadPool 做 dispatch_tpool

当前项目里的通用 `ThreadPool` 任务类型是 `std::function<void()>`。

而 `DispatchJob` 里直接持有 `std::unique_ptr<TraceSession>`，是 move-only 对象。

这意味着如果后面想写成：

- `dispatch_tpool.submit([job = std::move(job)]() mutable { ... })`

在当前 C++17 实现下会直接卡在 `std::function` 的“目标必须可拷贝”这条限制上。

所以“把 dispatch 改成线程池”不是简单加一个 `dispatch_thread_pool` 变量，而是二选一：

- 路线 A：保留当前 `dispatch_queue_`，把单个 `dispatch_thread_` 扩成多个 consumer 线程
- 路线 B：先升级通用 `ThreadPool`，让它支持 move-only task，再把 dispatch 正式收口成 `dispatch_tpool`

当前判断：

- 如果目标是最小改动、尽快验证瓶颈，优先走路线 A
- 如果目标是统一线程模型、减少两套调度实现并存，优先走路线 B
- 但路线 B 的前置条件不是“新增一个变量”，而是“先重构 ThreadPool 的任务抽象”

所以如果 `dispatch_thread` 继续保持单线程，那么 D 测出来的很可能只是“单线程 dispatch 的上限”。

### 当前实现倾向

在正式做 Suite D 前，优先考虑把 `dispatch_thread` 收口成可配置的 `dispatch_worker_threads`：

- 默认值仍然是 `1`
- benchmark 时允许把它抬高，观察 dispatch 这层会不会先成为结构性瓶颈
- 这样 D 测到的是“可扩展的当前架构”，而不是“人为卡死的一条串行链”

`query_tpool` 当前不作为主变量。

原因：

- 只要 benchmark 不混入查询流量，并且 retention 线程关掉或不触发，它基本不在主热路径上
- 所以 D 不需要专门把 query 线程数拿出来单独扫

### 当前资源锚点

Suite D 当前也先以单机 `16 核` 为设计锚点：

- `wrk_cpu_cores = 2`
- `ai_proxy_cpu_cores = 2`
- `backend_cpu_cores = 12`

### 当前口径

D 不再先假定 `server_io_threads = 1` 固定不动。

更合理的顺序是：

Step 0：先做一次后端线程拓扑校准

- 固定 `16 核 / 2-2-12` 资源锚点
- 固定 benchmark 流量模型
- 当前默认把容量硬上限略微放大，避免沿用开发态默认值过早触发背压：
  - `trace_active_session_limit = 2048`
  - `trace_buffered_span_limit = 16384`
  - `worker_queue_size = max(20000, worker_threads * 512)`
- 校准：
  - `server_io_threads`
  - `worker_threads`
  - `dispatch_worker_threads`（如果这刀实现了）
  - `ai_proxy_max_workers`

Step 1：冻结一套代表性线程拓扑

- 把这套拓扑直接复用给 Suite A
- 也作为后续 CPU 扩展实验的基准拓扑

Step 2：再做 CPU 资源扩展

- 资源点当前倾向：
  - `2 / 4 / 8 / 16 / 32 核`
- 但单机场景里真正给后端的核数，要扣掉 `wrk` 和 `ai-proxy`

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

## 单机 / 双机拓扑说明

### 单机场景

如果 benchmark 在同一台机器上同时运行：

- 后端
- `wrk`
- AI proxy / 本地故障注入服务

那么后端“可用 CPU”不能直接等于整机总核数。

这时需要把客户端和辅助服务的核占用一起考虑进去。

### 单机场景下的默认资源点

当前资源点只先冻结一个设计锚点：

- `16 核`
  - `wrk = 2`
  - `ai-proxy = 2`
  - `backend = 12`

更低或更高核数的进程级分核比例，暂时不在这份文档里写死。

原因：

- 这轮还在先收口 suite 语义和线程拓扑
- 如果现在把 `2 / 4 / 8 / 16 / 32` 的进程级分核表全写死，后面很可能又要推翻
- 当前真正已经被讨论确认的，只有 `16 核 / 2-2-12` 这一个锚点

### `ai_proxy_max_workers` 的位置

除了进程级分核，还要固定代理层自己的并发上限：

- 默认值：`128`
- 推荐扫描：`64 / 128 / 256 / 512`

这里的意义不是声称“外部模型服务一定能承受这么高并发”，
而是避免本地 Python proxy 自己先因为线程 limiter 太小而成为假瓶颈。

如果外部模型服务的单 key 配额更低，那么限制会体现为：

- `429`
- timeout
- 限流/服务端错误

这类外部上游约束后续可以通过：

- 多 key
- 多账号
- 多 provider
- AI Gateway / 负载均衡代理

继续扩展；它不应该反过来把本地代理层的 benchmark 变量定义抹掉。

### 这份分配表的语义

这不是“绝对最优绑定”，而是当前 benchmark 的默认起步配置。

如果后续实测发现：

- `wrk` 本身打不满后端
- `ai-proxy` 提前成为瓶颈
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

先收口 Suite B

再收口 Suite D

再收口 Suite A

最后如果时间还够，再决定要不要补 Suite A-Interaction

原因：

Suite B 现在更像论文里的主特色图，因为它直接证明生命周期防护相比朴素策略的收益。

Suite D 负责把“当前架构扩到哪会撞墙”单独讲清楚，也顺手给 Suite A 提供固定线程拓扑。

Suite A 虽然最适合做主性能图，但它依赖 Suite D 先给出一套代表性线程配置，否则功能代价会测脏。

Suite A-Interaction 更像锦上添花，应该放在主图之后；这样即使时间不够，也不会影响 benchmark 主线成立。

## 当前状态

当前已经 ready 的前置条件：

- `--disable-ai`
- `--disable-buffered-trace-repo`
- `--disable-webhook`
- `server_io_threads`
- `worker_threads`
- `server/ai/proxy/main.py --max-workers`
- `ai_retry_enabled / ai_retry_max_attempts`
- `ai_auto_degrade`
- `ai_circuit_breaker`

当前还没有写死的内容：

- `dispatch_thread` 是否先改造成 `dispatch_worker_threads`
- Suite D 线程拓扑校准时，最终选哪几个 `server_io_threads / worker_threads / dispatch_worker_threads / ai_proxy_max_workers`
- benchmark 最终要不要继续上调这三项容量硬上限：
  - `trace_active_session_limit`
  - `trace_buffered_span_limit`
  - `worker_queue_size`
- Suite A-Interaction 最终取哪两个资源点
- `2 / 4 / 8 / 32` 这些资源点下的进程级分核比例
- 具体 wrk 参数
- 最终结果模板和截图模板

这部分要等功能完全收口后，再在干净服务器上统一敲定。
