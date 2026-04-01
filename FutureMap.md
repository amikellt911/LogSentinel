# FutureMap

不影响主线交付，时间允许再做。

## 方向（占位）
### Redis（优先级高，收益最直观，hiredis 封装）
- []/results/* 结果短期缓存：trace_id -> analysis_result，TTL 数分钟，减少 SQLite 读压力。
- []/history 分页结果缓存：page/pageSize/level/search 组成缓存 key，TTL 30-120s，降低高频查询成本。
- []告警去重与节流：告警指纹（rule_id + trace_id + time_bucket）做短 TTL，避免重复通知与风暴。
- []AI 结果复用缓存：日志内容 hash/simhash -> 分析结果或摘要，减少重复推理成本。
- []Trace 聚合临时态：短期 Span 状态落 Redis + TTL，支撑乱序/超时补齐（后置）。

### 端侧预筛选（小模型）
### simhash 相似度缓存/结果复用（分级处理与投机采样,L1h和L2）
### MiniMuduo 增加 gRPC/Protobuf 支持
### pybind11 代替 C++ 与 Python 之间的 HTTP 交流
### 服务监控快照作为 AI 上下文（后置增强）
- []目标：把服务监控页当前窗口内的热点服务、热点操作、最近异常样本整理成结构化快照，作为 AI 分析的附加上下文，而不是只看单条 trace。
- []第一步不改主链路统计，只在 snapshot 已经稳定后，再评估是否把这份状态通过 JSON 传给 AI。
- []风险点：当前 AI 分析是 trace 级总结，不天然等于服务级总结；如果一个 trace 同时涉及多个异常服务，直接复用现有摘要会让“当前服务问题摘要”产生串味。
- []后续方向：如果要做服务级摘要，需要调整提示词和输出 JSON 结构，至少支持按 service_name 分段说明问题，而不是只返回整条 trace 的一段总述。

### LangGraph/Agent
### SQLite/B-Tree 迁移到 RocksDB/LSM-Tree
### 线程池任务队列升级为无锁 MPSC Ring Buffer

### 自适应背压联动控制（后置增强）
- []目标：把 active sessions、buffered spans、dispatch queue pending、worker pending、flush backlog 等中间态接成一条联动水位线，而不是各自孤立判断。
- []第一阶段先做静态联动：根据不同闸门的积压情况，提前收缩入口放行阈值，或让 ready trace 更多进入 retry，而不是继续向下游猛推。
- []后续再评估是否做自适应控制：根据队列水位、耗时和失败率动态调整阈值甚至线程数，但这一步需要重点防振荡、防误判，不能简单按“积压高就加线程”。
- []风险点：反馈延迟、阈值耦合、线程盲目增多导致上下文切换和锁竞争加重，都可能让系统比静态策略更差。
- []补一个更轻的过渡优化：`RefreshOverloadState()` 不必在每次高频 `Push` 路径都完整刷新；可以考虑把 `pendingTasks` 改成原子计数缓存，再叠加“累计若干次操作后再精刷一次”的降频策略。
- []这类降频刷新最好和实时回滚场景分开看：像 `RestoreSessionLocked()` 这种低频状态回补仍可继续即时刷新，真正要优化的是高频入口路径里的重复取样与锁竞争。

### 高性能指标监控系统（TLS 分散-汇总）
- []目标：实现“高频指标统计不拖慢主链路”，用于 QPS、字节吞吐、错误数、处理延迟等实时指标。
- []核心思路：写路径 `thread_local` 零竞争，读路径定时汇总（Gather），避免全局原子计数热点。
- []适用场景：高并发 Reactor 线程模型下，指标采集频率远高于普通业务日志写入频率。

#### 设计草案
Step 1: Scatter（分散写入）
业务线程只更新本线程局部指标，不加锁、不跨核写共享变量。

Step 2: Registry（线程局部指标注册）
通过 RAII 在 TLS 初始化时把本线程指标节点注册到全局 registry（例如 `std::vector<MetricsNode*>`，由轻量锁保护注册过程）。

Step 3: Gather（定时汇总）
在主事件循环中使用时间轮（TraceSessionManager的抽出）和定时器配合（`runEvery` / `timerfd`）每秒触发：
遍历 registry 汇总各线程指标，计算 QPS、吞吐、错误率、P50/P95 延迟（后续可扩展）。
汇总完成后执行“窗口重置”或“差分计算”。

Step 4: Export（输出）
先输出到内存快照与 `/dashboard`；后续按需追加 SQLite 持久化、Webhook 或 Prometheus exporter。

#### 预期收益
- 写路径零锁竞争：避免全局原子热点导致的 cache line bouncing。
- 统计实时且低损耗：汇总路径集中在定时回调，业务线程开销接近常数。
- 对简历/面试有明确技术亮点：可解释“为何不用全局原子计数器”。

### 配置与请求校验体系整理（后置）
- []目标：在不阻塞主链路交付的前提下，后续统一收口 `Handler` 入参校验、`Repository` 业务校验、前后端配置字段契约。
- []当前取舍：先保证功能打通与前后端联调，暂不引入通用校验框架，不做大规模抽象。
- []后续重点：
- []梳理 `/settings/*`、`/logs/spans` 等入口的字段白名单、类型校验、范围校验、枚举值域。
- []明确“接口层校验”和“业务层校验”的边界，避免校验全堆在 Handler 或全散在 Repository。
- []评估是否按业务域拆分小型校验模块，例如 `ConfigValidators`、`TraceRequestParsers`，而不是引入全局万能 Validator。
- []补齐前后端配置字段映射表，消除默认值和语义漂移问题（例如 provider/language/maxBatch 等）。
- []在前后端主功能稳定后，再决定是否把 prompts/channels 从“全量覆盖”接口升级为增量操作接口。

## 可能增强点（占位）
### 增强点一（待补充）
### 增强点二（待补充）

## 风险与依赖（占位）
### 依赖事项（待补充）
### 风险事项（待补充）
