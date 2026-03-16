# 性能阶段实验模板

这份文档只管一件事：把“每完成一个关键优化，就用同一套命令留一份可对比的基线”这件事钉死。

既然你后面要把结果写进毕业论文，那么最重要的不是“命令很多”，而是：

同一台机器
同一套测试方法
同一套指标
每次只改一个关键因素

这样前后结果才有归因意义。

## 1. 这份模板怎么用

既然项目后面会连续经历多次优化，比如：

Python proxy 并发修复
SQLite 双写缓冲区
后续其他存储/网络优化

那么每完成一个关键阶段，都固定跑下面这三组实验：

Python proxy 并发验证
端到端 benchmark 基线
火焰图

接着把结果填到文末的“阶段记录表”里。

不要在一个阶段里同时混入多个大改动。

既然一轮实验只对应一个关键变化，那么后面论文里才能说清楚：

“这个阶段的提升主要来自哪个优化”

## 2. 运行前约束

既然性能实验最怕环境噪声，那么每轮实验前先固定这些条件：

机器：同一台 `4C4G` 测试机
目标端口：`8080` 给 `LogSentinel`，`8001` 给 AI proxy
CPU 绑核：`WRK_CPUSET=0`，`SERVER_CPUSET=1-2`
wrk profile：固定使用 `end`
AI provider：如果做系统 benchmark，保持当前实验想要的 provider 形态；如果只验证 Python proxy 并发，只测 mock

如果实验前环境不一致，这一轮数据不进论文正式对比表。

## 3. 固定实验一：Python proxy 并发验证

这组实验只回答一个问题：

既然 mock provider 内部固定会 sleep 约 `0.5s`，那么它还会不会把整个 Python proxy 串成单车道。

### 3.1 启动 AI proxy

```bash
cd /home/llt/Project/llt/LogSentinel/server/ai/proxy
/home/llt/Project/llt/venv/bin/python main.py
```

### 3.2 并发验证命令

先跑 5 并发：

```bash
cd /home/llt/Project/llt/LogSentinel
/home/llt/Project/llt/venv/bin/python server/tests/verify_ai_proxy_concurrency.py --concurrency 5
```

再跑 10 并发：

```bash
/home/llt/Project/llt/venv/bin/python server/tests/verify_ai_proxy_concurrency.py --concurrency 10
```

### 3.3 记录什么

记录：

`total_elapsed`
`serial_floor`
`validation=passed/failed`

解释口径固定成这样：

既然 mock provider 最少会 sleep `0.5s`，那么如果 5 个并发请求仍然接近 `2.5s` 总耗时，就说明 route 层还是单车道阻塞。
接着如果总耗时明显低于这个串行下界，就说明 event loop 已经不再亲自执行阻塞 provider，线程池桥接是有效的。

## 4. 固定实验二：端到端 benchmark 基线

这组实验回答：

系统入口吞吐是多少
拒绝率是多少
P99 是多少
后链路到底有没有真正跑起来

### 4.1 固定命令

```bash
cd /home/llt/Project/llt/LogSentinel
DURATION=10s CONNECTION_SET="100" WORKER_THREAD_SET="3" \
SERVER_CPUSET="1-2" WRK_CPUSET="0" \
bash server/tests/wrk/run_bench.sh end
```

### 4.2 固定记录指标

从 wrk 输出里记录：

`Requests/sec`
`Non-2xx or 3xx responses`
`Latency Distribution` 里的 `99%`

从服务端日志里的 `[TraceRuntimeStats]` 记录：

`dispatch_count`
`submit_ok_count`
`submit_fail_count`
`worker_begin_count`
`worker_done_count`
`ai_calls`
`ai_total_ns`
`analysis_enqueue_calls`
`analysis_enqueue_total_ns`

从服务端日志里的 `[BufferedTraceRuntimeStats]` 记录：

`primary_flush_calls`
`primary_flush_total_ns`
`analysis_flush_calls`
`analysis_flush_total_ns`
`primary_flushed_summary_count`
`primary_flushed_span_count`
`analysis_flushed_analysis_count`

现在 `run_bench.sh` 会把这两行自动摘进结果目录里的 `run-summary.log`。
如果 benchmark 结束后服务端在限定时间内还没完全退干净，脚本会先保留 `SIGTERM` 时刻的埋点快照，再强制回收进程；所以这些数字的语义是“benchmark 停止时已完成到哪”，不是“无限等到后台彻底清空后的最终总账”。

### 4.3 论文里怎么解释

既然 wrk 的 `req/s` 只是入口请求速率，那么不能单独拿它代表系统真正完成了多少 Trace。
接着必须结合 `Non-2xx/3xx`、`TraceRuntimeStats` 和 `BufferedTraceRuntimeStats` 一起看。

说人话就是：

入口发得再猛，如果后链路没真正处理，那个 QPS 也只是“入口拒绝得很快”。

## 5. 固定实验三：火焰图

这组实验只回答：

CPU 时间主要烧在哪
热点是入口 JSON / Push / AI / SQLite 哪一段

### 5.1 固定命令

```bash
cd /home/llt/Project/llt/LogSentinel
SERVER_CPUSET="1-2" WRK_CPUSET="0" \
WORKER_THREADS=3 CONNECTIONS=1 DURATION=6s \
WARMUP_DURATION=1s WARMUP_CONNECTIONS=1 WARMUP_SETTLE_SEC=1 \
PERF_FREQ=49 \
bash server/tests/wrk/run_flamegraph.sh end
```

### 5.2 为什么固定成这组

既然火焰图第一目标是看“热点迁移”，那么 profile 先固定成最纯的 `end`。
接着本地回环下，哪怕 `1` 个连接也可能因为快速拒绝而打出很高 `req/s`，所以这张图更适合看相对热点，而不是拿来证明某个函数一定会很宽。

### 5.3 固定记录什么

记录：

结果目录
`trace.svg` 路径
最宽的 3 到 5 个热点函数

解释口径固定成这样：

既然火焰图看的是 CPU 样本占比，不是调用次数，那么一个函数“看不宽”不代表它没执行；它只代表 CPU 时间没主要烧在它身上。

## 6. 每个阶段的执行顺序

每次只按这个顺序来：

先记当前分支和 commit
再跑 Python proxy 并发验证
再跑端到端 benchmark
最后跑火焰图

推荐直接复制：

```bash
cd /home/llt/Project/llt/LogSentinel
git branch --show-current
git rev-parse --short HEAD
```

如果某一阶段漏跑了其中一项，这个阶段的论文对比就算不完整。

## 7. 什么时候需要 checkout 老 commit

主流程不要靠频繁 checkout 旧 commit 来做实验。

既然从现在开始已经有固定模板了，那么后续阶段直接沿主线留下基线就行。

只有一种情况再回头 checkout：

以前某个关键阶段没有留下干净结果
论文里又必须补一条“优化前原始版本”对照

这时候再切到明确的 commit，用同一套命令补测。

## 8. 阶段记录表

下面这张表每个阶段复制一份，直接填数字。

```text
阶段名称：
分支名：
Commit：
日期：

这轮只改了什么：

一、Python proxy 并发验证
5 并发 total_elapsed：
5 并发 serial_floor：
5 并发 validation：
10 并发 total_elapsed：
10 并发 serial_floor：
10 并发 validation：

二、端到端 benchmark
Requests/sec：
Non-2xx or 3xx responses：
P99：

TraceRuntimeStats:
dispatch_count：
submit_ok_count：
submit_fail_count：
worker_begin_count：
worker_done_count：
ai_calls：
ai_total_ns：
analysis_enqueue_calls：
analysis_enqueue_total_ns：

可选计算：
avg_ai_ms = ai_total_ns / ai_calls / 1e6
avg_analysis_enqueue_ms = analysis_enqueue_total_ns / analysis_enqueue_calls / 1e6

BufferedTraceRuntimeStats:
primary_flush_calls：
primary_flush_total_ns：
analysis_flush_calls：
analysis_flush_total_ns：
primary_flushed_summary_count：
primary_flushed_span_count：
analysis_flushed_analysis_count：

可选计算：
avg_primary_flush_ms = primary_flush_total_ns / primary_flush_calls / 1e6
avg_analysis_flush_ms = analysis_flush_total_ns / analysis_flush_calls / 1e6

三、火焰图
结果目录：
trace.svg：
热点 1：
热点 2：
热点 3：

结论：
这轮优化主要改善了什么：
这轮优化没有改善什么：
下一轮最可能的瓶颈：
```

## 9. 推荐的阶段命名方式

为了后面论文和图表不乱，阶段名称也固定一下：

`Stage-A: Python proxy 并发修复后`
`Stage-B: SQLite 双写缓冲区后`
`Stage-C: 其他优化名后`

既然名字固定了，后面图表标题、实验小节、结论段落都会顺很多。
