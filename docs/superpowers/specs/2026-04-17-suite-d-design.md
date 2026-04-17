# Suite D 扩展性实验设计

## 1. 目标

Suite D 不再比较功能开关，也不再承担生命周期鲁棒性证明。

它只回答一个问题：

本系统虽然从轻量、低依赖的小场景起步，但当单机总资源预算增加时，整体处理能力是否还能继续提升，而不是很早饱和。

这组实验的叙事分两层：

- 第一层：`4` 核本机 `AI-on` 只负责证明“轻量环境下完整链路真实可运行”。
- 第二层：同一台 `24` 核云机上的主扩展曲线负责证明“资源预算增加时，系统整体能力仍能继续上升”，也就是单机纵向扩展能力。

这里必须明确：

- `4` 核本机和 `24` 核云机上的 `4` 核档位不是一回事。
- 前者是“最低部署门槛”证明。
- 后者是“高配机器上的受控总资源预算点”。

## 2. 总体叙事

Suite D 走工程部署视角，不走纯控制变量的 microbenchmark 视角。

也就是说，主变量是“总核心预算”，而不是某一个孤立线程参数。

在每个总核心预算档位下：

- `sender` 和 `backend` 的资源分配允许因地制宜；
- 线程参数允许按同一套基线拓扑按比例映射；
- 水位参数也允许跟着部署形态一起缩放。

这组实验不声称“每个资源点都用了各自最优的完全独立参数搜索结果”，而是声称：

先在最高资源档位确定一套代表性部署拓扑，再按比例映射到其他资源预算，从而观察系统在统一部署策略下的整体扩展趋势。

## 3. 发生器口径

Suite D 主图继续保留 `wrk` 作为主发生器，不再切换成 Python sender。

原因：

- 当前项目已有成熟的 `wrk` runner、Lua 脚本、后端埋点和火焰图链路；
- 主图仍然需要请求级 `QPS` 和 `p95/p99 latency`；
- 后端已经有运行时链路埋点，可以补上 trace 级观测；
- 维持 `wrk` 作为主发生器能保持整套 benchmark 叙事连续，不引入新的维护面。

但 `wrk` 不能只停留在“请求级 QPS”。

Suite D 需要在现有 `wrk` 压流基础上，补一个最小 `offered traces` 计数口径。

这项计数的用途不是替代后端完成数，而是给 trace 级完成率提供统一分母：

- measurement window 内一共 offered 了多少条 trace；
- `t_stop` 时刻 SQLite 已完成了多少条 trace；
- 停发后还要拖多久才能追平 offered traces。

## 4. 指标定义

Suite D 主图不直接把请求级 QPS 作为唯一主轴。

主指标定义为：

`online_completed_traces_per_sec`

含义：

- 以 measurement window 结束时刻 `t_stop` 为截点；
- 统计 `t_stop` 时 SQLite `trace_summary` 中已经完成的 trace 数；
- 再除以 measurement window 时长。

辅助指标包括：

`online_completion_ratio`

- 定义：`t_stop` 时刻 SQLite 已完成 trace 数 / measurement window 内 sender offered trace 数。

`drain_tail_ms`

- 定义：从 `t_stop` 开始，到 SQLite `trace_summary` 追平 measurement window 内 offered trace 数为止的时间。

请求级 `QPS`

- 继续由 `wrk` 输出。

请求级 `p95 / p99 latency`

- 继续由 `wrk` 输出。

如果后续需要补充论文表格或解释瓶颈，还可以记录：

- 后端 CPU 占用；
- RSS；
- 背压触发次数；
- 后端运行时埋点中的 dispatch / flush / worker 统计。

但这些不作为主图的第一优先级指标。

## 5. measurement window

Suite D 主图统一采用固定时长窗口。

推荐口径：

- warmup：固定一段时间，把连接和缓存预热起来；
- measurement window：固定一段正式发送窗口；
- drain：measurement window 结束后，继续轮询 SQLite，直到 offered traces 全部追平或达到上限。

也就是说：

- 主图先看窗口内已经完成多少；
- 再看尾巴还要拖多久。

不能直接把“最终全部 drain 完后的完成数”当成主吞吐，否则会把窗口内没跟上的情况美化掉。

## 6. 轻量场景证明图

Suite D 正文开头先放一个轻量场景证明图：

- 机器：本机 `4` 核
- 模式：`AI-on`
- 负载：只放 `1` 个代表性负载点
- 发生器：继续用 `wrk`

这张图的职责只有一个：

证明“轻量环境下完整链路可以真实运行”，而不是再展开成一组小型扩展性矩阵。

因此这张图不承担：

- `4` 核多档负载扫描；
- 线程拓扑搜索；
- 水位搜索；
- 和主扩展曲线同一横轴上的直接比较。

## 7. 主扩展曲线

Suite D 主扩展曲线统一在同一台 `24` 核云机上完成：

- 模式：`AI-off`
- 视角：总核心预算驱动的整体可部署性能
- 横轴：总核心数 `4 / 8 / 12 / 16 / 20 / 24`

这里的总核心数指：

- sender 核数
- backend 核数

两者之和。

每个档位都按总预算重新分配 sender 和 backend，而不是把 sender 永远固定成同一个值。

当前冻结的资源分配表为：

| 总核数 | sender 核数 | backend 核数 |
| --- | --- | --- |
| 4 | 1 | 3 |
| 8 | 2 | 6 |
| 12 | 2 | 10 |
| 16 | 3 | 13 |
| 20 | 3 | 17 |
| 24 | 4 | 20 |

这张表的意图是：

- 低配档位先保证 sender 不至于先饿死；
- 中高配档位把更多预算让给 backend，观察真正的后端扩展能力。

## 8. 主负载模型

Suite D 主图统一使用 clean end-trace 模型。

原因：

- Suite D 的目标是测系统扩展性，而不是测脏时序容错；
- `AI-off + clean end-trace` 最适合把瓶颈收敛在入口、dispatch、flush 这些真实主链路阶段；
- 如果把 Suite B 式的脏时序 sender profile 混进来，结论会混成“扩展性 + 生命周期鲁棒性”的叠加噪声。

因此：

- Suite D 主图先只回答“能不能扩得起来”；
- 如果后面时间够，再补现实一点的 sender 负载作为补充图，而不是抢主图职责。

## 9. 线程拓扑策略

Suite D 不在每个资源档位单独做完全独立的调参。

正确策略是：

- 先在 `24` 核高配机上做一次很小的基线拓扑搜索；
- 只试 `2~3` 组候选；
- 再把胜出的基线拓扑按比例映射到其他总核数档位。

这样做的原因是：

- 如果每个档位都各自调到最优，曲线会变成“每个点都换一套系统”；
- 后面老师一问“到底是核数变了，还是参数变了”，解释会很脏；
- 先在最高资源档位固定代表性拓扑，再缩到其他档位，更符合“统一部署策略”的叙事。

### 9.1 `24` 核候选拓扑

Suite D 当前统一按 `AI-off` 语义思考线程分工。

也就是说：

- `worker_threads` 不再承担 AI provider 调用和 webhook 外发；
- 这时更可能先顶住的是 `server_io_threads` 和 `dispatch_worker_threads`；
- 因此基线搜索走 `io/dispatch-heavy`，而不是沿用旧的 `worker-heavy` 直觉。

当前冻结的 `24` 核候选拓扑只有三组：

`T1`

- `server_io_threads = 4`
- `dispatch_worker_threads = 3`
- `worker_threads = 32`

`T2`

- `server_io_threads = 6`
- `dispatch_worker_threads = 4`
- `worker_threads = 32`

`T3`

- `server_io_threads = 6`
- `dispatch_worker_threads = 5`
- `worker_threads = 48`

这三组的职责分别是：

- `T1`：保守基线；
- `T2`：当前最看好的平衡候选；
- `T3`：更激进的 `dispatch-heavy` 候选。

### 9.2 比例映射规则

如果 `T2` 在 `24` 核上胜出，则其他档位按如下比例缩放：

| 总核数 | sender | backend | server_io_threads | dispatch_worker_threads | worker_threads |
| --- | --- | --- | --- | --- | --- |
| 4 | 1 | 3 | 1 | 1 | 8 |
| 8 | 2 | 6 | 2 | 1 | 12 |
| 12 | 2 | 10 | 3 | 2 | 16 |
| 16 | 3 | 13 | 4 | 3 | 24 |
| 20 | 3 | 17 | 5 | 3 | 28 |
| 24 | 4 | 20 | 6 | 4 | 32 |

这里有几个必须钉死的解释：

- `worker_threads` 不是 CPU 并行度，而是阻塞并发槽位；
- `server_io_threads` 和 `dispatch_worker_threads` 才是这轮 `AI-off` 主图里更值得优先观察的拓扑变量；
- `flush_thread` 固定单线程，不纳入 Suite D 主变量。

## 10. 水位策略

Suite D 的水位不再拍死成全档位统一常数。

它要跟着线程拓扑走，避免出现两种脏情况：

- 低配档位拿了过宽的水位，导致积压被拖很久才显形；
- 高配档位拿了过窄的水位，导致还没测到架构极限就先被假背压截断。

当前冻结的派生规则为：

`worker_queue_size`

- `worker_queue_size = max(4096, worker_threads * 256)`

`trace_active_session_limit`

- backend `<= 6`：`512`
- backend `<= 10`：`1024`
- backend `<= 17`：`1536`
- backend `>= 20`：`2048`

`trace_buffered_span_limit`

- 统一按 `trace_active_session_limit * 8`

也就是：

- `512 -> 4096`
- `1024 -> 8192`
- `1536 -> 12288`
- `2048 -> 16384`

## 11. 基线拓扑判定逻辑

Suite D 这轮不是追单点理论极限，而是选一套“可代表真实部署形态”的基线拓扑。

因此胜出逻辑要服务主叙事，而不是只盯单一峰值。

推荐判定顺序为：

第一层：

- `online_completed_traces_per_sec`

第二层：

- `online_completion_ratio`
- `drain_tail_ms`

第三层：

- 请求级 `QPS`
- `p95 / p99 latency`

也就是说：

- 优先看窗口内到底完成了多少 trace；
- 再看是不是靠长尾拖补或者明显积压撑出来的；
- 最后再看请求级吞吐和延迟细节。

## 12. 不做的事

这轮 Suite D 明确不做：

- 每个核数档位都重新开一轮独立大搜索；
- 把 `flush_thread` 也拉进主变量；
- 在主图里混入 `AI-on`；
- 在主图里混入 Suite B 式脏时序 sender；
- 把 `24` 核云机上的 `4` 核点和本机 `4` 核轻量证明图混成同一条曲线；
- 把 sender 实现写死成“未来永远只能是 `wrk`”，当前只是这轮主图继续用 `wrk`。

## 13. 结果解释边界

Suite D 要证明的是：

- 本系统不是只能在低配环境里“勉强能跑”；
- 当单机资源预算增加时，整体处理能力还能继续提升；
- 因此它具备单机纵向扩展能力。

Suite D 不证明：

- 分布式横向扩展能力；
- 外部 AI provider 配额条件下的完整链路上限；
- 任意 sender 或任意线程参数下的绝对理论极限。

如果后续需要解释为什么主图统一 `AI-off`，正文里应明确写：

- `AI-on` 更容易被外部 provider 配额、网络抖动和 proxy limiter 影响；
- 它适合证明“完整链路可运行”，不适合充当主扩展曲线；
- AI 相关扩展性可以留到未来工作，例如多 key、负载均衡代理、API 网关或更强的 provider 并发治理。
