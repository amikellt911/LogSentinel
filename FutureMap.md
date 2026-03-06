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
### LangGraph/Agent
### SQLite/B-Tree 迁移到 RocksDB/LSM-Tree
### 线程池任务队列升级为无锁 MPSC Ring Buffer

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
