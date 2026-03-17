# Trace wrk Benchmark Guide

这份文档只管一件事：把 `LogSentinel + wrk trace_model.lua` 这套 Trace 压测的测试目标、参数选择和第一版实验矩阵钉死。

## 1. 这轮到底在测什么

既然本项目这一轮重点是 `TraceSessionManager` 的分发与背压，那么压测目标不是“公网带宽能顶多高”，也不是“前端请求链路多丝滑”，而是下面这几件事：

- `trace_end / capacity / token_limit / idle timeout` 这四类分发路径能不能按预期触发
- `ThreadPool` 队列积压时，`pendingTasks` 水位和 `503` 拒绝是否按预期出现
- `total_buffered_spans / active_sessions` 上涨后，系统会不会稳定进入 overload / critical
- 压力撤掉后，入口积压和线程池队列能不能自动回落

所以这轮 benchmark 本质上不是单纯跑一个 QPS 数，而是在做“按分发路径拆开的瓶颈定位”。

## 2. 为什么要本地回环

既然测试机公网带宽只有 `3MBPS`，那么如果流量从外网打进来，先撞上的一定是带宽上限，而不是 `LogSentinel` 本身。

所以第一版统一采用本地回环：

- `wrk` 打 `127.0.0.1`
- `LogSentinel` 跑在本机
- `ai-proxy(mock)` 和 `webhook mock` 也跑在本机

这样测出来的才是“单机处理能力 + 线程池/背压行为”，不是“网卡太慢”。

## 3. 为什么不一上来只写一个大杂烩 payload

既然一条 Trace 可能因为不同原因被分发，那么如果一开始就把各种请求全混成随机大杂烩，结果一差，根本不知道是谁先拖后腿。

所以第一版必须先按分发触发路径拆开测：

- `end-driven`: 最后一个 span 带 `trace_end=true`
- `capacity-driven`: 不带 `trace_end`，靠 span 数撞 `capacity`
- `token-driven`: span 数不多，但属性字段更大，靠 `token_limit` 触发
- `timeout-driven`: 只发半截 trace，不结束，靠 `idle timeout` 被时间轮扫出
- `mixed-realistic`: 固定 `70% end / 20% timeout / 10% capacity`

既然这样拆开了，后面每条路径测出来的吞吐、P99 和背压拐点才有解释力。

## 4. 为什么模板池用“顺序回环”，不用 random

既然 benchmark 更在乎可重复，而不是“看起来很随机”，那么模板池采用 round-robin 比 random 更适合。

物理意义很直接：

- 完全一样的 payload 会把 CPU cache 和字符串分配路径测得太假
- 完全随机又会把变量撒太散，结果不稳定

所以折中方案就是：

- 每个模型维护 `4~8` 组固定模板
- 每条新 trace 开始时按顺序轮到下一组模板
- 同一条 trace 内，`trace_key` 相同，`span_id` 递增，模板风格不变

这样既有扰动，又容易复现。

## 5. 为什么先建议测 `worker-threads = 2 / 3 / 4`

既然机器只有 `4 核`，而且还要同时跑：

- `wrk`
- `LogSentinel`
- `ai-proxy(mock)`
- `webhook mock`

那么 worker 线程数不能凭感觉乱拍。

顺着代码看，`TraceSessionManager::Dispatch()` 提交到共享 `ThreadPool` 之后，worker 线程会同步做完这一整条 trace 的重活：

- 构建 trace 树
- 序列化
- 可选 AI mock 调用
- SQLite 落库
- 可选 critical 告警

也就是说，一个 worker 一旦拿到任务，就会被整条 trace 占住，不是切成很多细粒度小任务。

所以：

- `2` 线程：适合故意压低消费能力，更容易观察背压和 `pendingTasks` 上涨
- `3` 线程：第一版主力组，更接近这台 `4 核` 机器的合理服务端配置
- `4` 线程：验证“线程再多还有没有收益”，防止错把上下文切换当优化

所以不是说 `2` 一定最好，而是它适合背压观察；主力组先看 `3`，再拿 `4` 做对照。

## 6. 服务端参数为什么要按模型分开配

既然不同模型想触发的分发路径不同，那么服务端参数就不能一套打天下。

### 6.1 end-driven

目标：让 `trace_end=true` 成为最先触发的条件。

所以：

- `trace-capacity` 要大于每条 trace 的 span 数，避免先撞 capacity
- `trace-token-limit=0`，先关 token 限制
- `trace-idle-timeout-ms` 不要太小，避免 timeout 插队

推荐：

- `trace-capacity=16`
- `trace-token-limit=0`
- `trace-sweep-interval-ms=200`
- `trace-idle-timeout-ms=1200`
- `trace-max-dispatch-per-tick=64`

### 6.2 capacity-driven

目标：让 `capacity` 成为第一触发条件。

所以：

- `trace-capacity` 直接设成脚本里每条 trace 的 span 数
- `trace-token-limit=0`
- `trace-idle-timeout-ms` 稍大，避免 timeout 抢跑

推荐：

- `trace-capacity=8`
- `trace-token-limit=0`
- `trace-sweep-interval-ms=200`
- `trace-idle-timeout-ms=2000`
- `trace-max-dispatch-per-tick=64`

### 6.3 timeout-driven

目标：让半截 trace 留在内存里，最终靠时间轮扫出来。

所以：

- `trace-capacity` 必须调大，不能让前半截 span 先撞 capacity
- `trace-idle-timeout-ms` 必须明显调小，否则 timeout 路径很难在短时间内观察到
- `trace-sweep-interval-ms` 也要调细一点，不然时间粒度太粗

推荐：

- `trace-capacity=16`
- `trace-token-limit=0`
- `trace-sweep-interval-ms=100`
- `trace-idle-timeout-ms=600`
- `trace-max-dispatch-per-tick=32`

### 6.4 token-driven

目标：让“大字段 + 低 token 阈值”先触发 token 分发。

所以：

- `trace-token-limit` 要明显调小
- `trace-capacity` 要调大，避免先撞 capacity
- `trace-idle-timeout-ms` 要调大，避免 timeout 先来

推荐：

- `trace-capacity=16`
- `trace-token-limit=2000`
- `trace-sweep-interval-ms=200`
- `trace-idle-timeout-ms=1500`
- `trace-max-dispatch-per-tick=64`

### 6.5 mixed-realistic

目标：看综合路径下的吞吐、延迟和背压拐点。

所以这里不用那些“故意拉偏”的极端参数，而是回到更温和的一组：

- `trace-capacity=12`
- `trace-token-limit=0`
- `trace-sweep-interval-ms=200`
- `trace-idle-timeout-ms=800`
- `trace-max-dispatch-per-tick=64`

## 7. 共享线程池和压测口径

既然 `LogBatcher` 和 `TraceSessionManager` 当前共用同一个 `ThreadPool`，那么 benchmark 口径必须先收紧。

第一版统一只打：

- `POST /logs/spans`

不同时打：

- `POST /logs`

这样做的原因很简单：

既然同池竞争会把 `LogBatcher` 那条链路的 AI 批处理、原始日志入库也混进来，那么第一版压 trace 时，结果就会脏得很难解释。

所以第一版 benchmark 的口径就是：

只测 trace 聚合链路，不测老的 `/logs` 批处理链路。

## 8. 第一版实验矩阵

既然我们想先把机制和主拐点摸清楚，那么第一版实验矩阵先别搞太大：

### 8.1 wrk 线程和连接

- `wrk -t1 -c100`
- `wrk -t1 -c200`
- `wrk -t1 -c500`

物理意义：

- 单线程多连接，先看 client 自己能不能把服务端压起来
- 如果 `-t1` 先成瓶颈，再补 `-t2`

### 8.2 worker 线程档位

- `worker-threads=2`
- `worker-threads=3`
- `worker-threads=4`

### 8.3 先跑的模型顺序

既然 benchmark 最怕一上来就把变量搞炸，那么顺序固定成：

1. `end-driven`
2. `capacity-driven`
3. `timeout-driven`
4. `token-driven`
5. `mixed-realistic`

先摸清单路径，再看综合。

## 9. 压测前环境约束

既然机器资源有限，那么压测前环境要尽量收敛：

- `openclaw-gateway` 关闭，不要占 `11%` 左右内存
- `mihomo` 保留，因为它是当前 VPN
- 当前活跃的 VSCode server 不动，但不要再同时开很多窗口和插件扫描
- 确认 `8080/8001/9000/9001` 没有旧实例残留
- 每次压测最好用独立 DB 路径，例如 `/tmp/trace-bench.db`

## 10. 接下来写什么

这份文档先把 benchmark 的“实验设计”钉住。

下一步再写：

- `server/tests/wrk/run_bench.sh`

它的职责应该是：

- 清理旧的 benchmark 进程
- 用固定 profile 启动 `LogSentinel`
- 用固定命令启动 `wrk`
- 把结果落到统一目录
- 压测结束后收尾

它不负责重新发明参数选择逻辑，只负责忠实执行这份文档里的 profile。
