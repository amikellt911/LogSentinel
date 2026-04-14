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

- `run_bench.sh` / `run_flamegraph.sh` 通过环境变量 `TRACE_LIFECYCLE_PROFILE=protected|minimal`
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

### 主要比较内容

- trace 完整率
- 误拆分率
- 重复落库率
- 晚到 span 误判率
- 最终可查询正确率
- 在脏流量下的 ingest 稳定性

### 固定资源口径

这组同样不把资源变量一起扫开。

这里沿用单机 `16 核` 锚点：

- `wrk_cpu_cores = 2`
- `ai_proxy_cpu_cores = 2`
- `backend_cpu_cores = 12`

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

- `minimal` 最终选“候选 A 直接 dispatch”还是“候选 B 仍走 sweep 主路径”
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
