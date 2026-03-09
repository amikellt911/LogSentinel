# 20260309-perf-trace-benchmark

perf(trace): 新增 wrk Trace 模型脚本，按分发路径拆分压测流量

## Git Commit Message
perf(trace): 新增 wrk trace 压测模型脚本

## Modification
- **server/tests/wrk/trace_model.lua**:
  - 新增统一的 wrk Lua 脚本，支持 `end`、`capacity`、`token`、`timeout`、`mixed` 五种 trace 模型。
  - 模板池采用顺序回环，而不是完全随机，保证 benchmark 结果可重复对比。
  - 在脚本内维护 `trace_key/span_id` 的跨请求状态，使多次 HTTP 请求可以共同组成一条 trace。
  - `mixed` 模型采用固定 `70% end / 20% timeout / 10% capacity` 的轮转序列，先做稳定拟真，不把变量撒得过散。
  - 使用 `setup()/init()/request()/done()` 这套 wrk 生命周期回调，把多线程下的 `trace_key` 分片和单线程下的 trace 状态推进都收进脚本本身。
- **docs/todo-list/Todo_Benchmark.md**:
  - 将压测脚本待办从占位的 `trace_standard.lua` 收口为实际落地的 `trace_model.lua`，并标记完成。

## Learning Tips
- **Newbie Tips**:
  - wrk 的 `-t` 是客户端发压线程数，`-c` 是总连接数。单线程完全可以多连接，底层靠非阻塞 socket + 事件循环同时驱动很多连接。
  - Trace 压测和普通日志压测不一样，一条 trace 往往由多次 HTTP 请求共同组成，所以脚本里必须自己维护“当前 trace 发到第几个 span 了”的状态。
- **Function Explanation**:
  - `setup(thread)`: wrk 在创建每个压测线程时自动回调，用来给不同线程注入不同的线程号或种子。
  - `init(args)`: wrk 加载脚本后、每个线程开始工作前调用，用来读取 `mode/spans_per_trace/timeout_cutoff/token_payload_size` 这些参数。
  - `request()`: 每发一个 HTTP 请求前都会调一次，这里才是真正生成 span body 的核心。
  - `done(summary, latency, requests)`: 整轮压测结束后调用，用来打印脚本侧汇总信息。
- **Pitfalls**:
  - 如果所有请求除了 `trace_key` 以外完全一样，benchmark 结果会过于理想化，容易把 CPU cache 和字符串分配路径测假。
  - 如果一上来就把五种分发路径混成随机大杂烩，最后 QPS 或 P99 一差，很难判断到底是 `trace_end`、`capacity` 还是 `timeout` 路径先拖后腿。
  - 多线程 wrk 下如果不给不同线程切不同的 `trace_key` 段，不同线程可能构造出相同的 trace_key，直接把服务端聚合语义搅乱。

## Validation
- 机器上未安装独立 `lua` 解释器，无法直接用 `lua -e` 做语法检查。
- 已执行 `wrk -t1 -c1 -d1s -s server/tests/wrk/trace_model.lua http://127.0.0.1:1 -- end 8 4 2048`，结果进入连接阶段后因目标端口拒绝连接退出，说明 wrk 已成功装载并解析脚本本身。

---

perf(main): 暴露 benchmark 所需的 Trace 与线程池启动参数

## Git Commit Message
perf(main): 暴露 trace benchmark 所需启动参数

## Modification
- **server/src/main.cpp**:
  - 新增 `--worker-threads`，允许压测时手动控制共享线程池线程数，不再只能跟随 CPU 核数自动计算。
  - 新增 `--worker-queue-size`，把 `ThreadPool` 队列容量从硬编码 `10000` 暴露成命令行参数，便于稳定复现 `pending_tasks` 水位。
  - 新增 `--trace-capacity`、`--trace-token-limit`、`--trace-max-dispatch-per-tick`，把三种常用分发/吐货旋钮从硬编码挪到 CLI。
  - 新增 `--trace-buffered-span-limit`、`--trace-active-session-limit`，把背压的两个核心 hard limit 暴露给 benchmark。
  - 为以上参数补齐最小合法性校验，并在启动日志里打印最终生效值，避免压测时“以为参数生效了，其实还在吃默认值”。
- **docs/todo-list/Todo_Benchmark.md**:
  - 将 benchmark 所需的主进程参数改造项标记完成，和实际落地的 CLI 名字对齐。

## Learning Tips
- **Newbie Tips**:
  - benchmark 最怕“每改一组参数就重新改代码再编译”，因为这样很容易把二进制和测试记录搞混。能走 CLI 的旋钮，优先都做成 CLI。
  - `worker_queue_size` 决定的是线程池任务队列容量，不是线程数。线程数影响消化速度，队列大小影响能积多少待处理任务。
- **Function Explanation**:
  - `ThreadPool(num_worker_threads, max_queue_size)`: 第二个参数就是队列上限，直接决定 `pendingTasks()` 的理论 100% 基数。
  - `TraceSessionManager(..., capacity, token_limit, ..., buffered_span_hard_limit, active_session_hard_limit)`: 后面这两个 hard limit 是背压状态机的入口主闸门。
- **Pitfalls**:
  - `trace_token_limit` 允许传 `0`，因为这在当前实现里表示“关闭 token 限制”；如果误写成必须大于 0，反而会把默认行为搞坏。
  - benchmark 参数如果只改了一半，比如只调 `trace_capacity` 不调 `trace_max_dispatch_per_tick`，很容易测出来“看着像 timeout 路径慢”，其实是每轮吐货上限太小。

---

docs(benchmark): 补齐 Trace wrk 压测设计文档

## Git Commit Message
docs(benchmark): 补齐 trace wrk 压测设计文档

## Modification
- **docs/TRACE_WRK_BENCHMARK_GUIDE.md**:
  - 新增 Trace wrk benchmark 设计文档，明确这轮压测只测 `/logs/spans` 链路，不混 `/logs` 批处理链路。
  - 按 `end/capacity/token/timeout/mixed` 五种模型解释为什么 payload 要按分发路径拆开设计。
  - 收口 `worker-threads=2/3/4`、`trace-capacity`、`trace-token-limit`、`trace-idle-timeout-ms` 等关键参数的推荐值和选择原因。
  - 给出第一版实验矩阵、环境清理约束和后续 `run_bench.sh` 的职责边界。
- **docs/todo-list/Todo_Benchmark.md**:
  - 追加并勾选 benchmark 设计文档任务，避免后面脚本和参数脱节。

## Learning Tips
- **Newbie Tips**:
  - benchmark 不是一上来就写壳脚本，先把实验设计钉住，不然后面脚本只会把错误参数固化进去。
  - `timeout-driven` 模型如果还沿用过小的 `trace-capacity`，会先撞 capacity，根本轮不到 timeout，这类实验最容易“看着跑了，实际测偏了”。
- **Function Explanation**:
  - `worker-threads=2/3/4` 的三档不是拍脑袋：`2` 偏向观察背压，`3` 作为主力组，`4` 用来验证再加线程还有没有收益。
  - `mixed-realistic` 当前先固定 `70% end / 20% timeout / 10% capacity`，目的是先要稳定拟真，不是追求复杂随机性。
- **Pitfalls**:
  - 如果同时压 `/logs` 和 `/logs/spans`，因为它们当前共用同一个 `ThreadPool`，结果会混进 LogBatcher 链路的干扰，第一版很难解释。
  - benchmark 结果如果不带环境约束说明，比如 VPN 保留、openclaw 关闭、VSCode 保持最小活动，后面复现实验时很容易前后对不上。

---

perf(benchmark): 新增最小版 run_bench.sh 实验编排脚本

## Git Commit Message
perf(benchmark): 新增最小版 trace wrk 编排脚本

## Modification
- **server/tests/wrk/run_bench.sh**:
  - 新增最小版 benchmark harness，支持按 `profile=end|capacity|token|timeout|mixed` 选择一套固定的服务端参数和 wrk 参数。
  - 脚本按时间线完成：启动 `LogSentinel`、等待 `8080` ready、执行多组 wrk、双写输出到终端和文件、最后自动 cleanup。
  - 默认循环 `worker_threads=2/3/4` 与 `connections=100/200/500`，并支持通过环境变量覆盖。
  - 增加可选 `SERVER_CPUSET/WRK_CPUSET`，后续做 `taskset` 核心隔离时不用重写脚本主体。
- **docs/todo-list/Todo_Benchmark.md**:
  - 将一键 benchmark 脚本条目标记完成，并明确这是“最小编排版”，不是结果分析器。

## Learning Tips
- **Newbie Tips**:
  - shell 脚本最适合做“实验流水线控制”，不适合抢 Lua/C++ 的业务逻辑工作，所以它只负责起服务、跑命令、收输出、收尾。
  - `trap cleanup EXIT` 的味道就跟 RAII 很像：既然脚本可能正常结束、失败退出或 Ctrl+C，中间路径不统一，那就把收尾收口到一个出口。
- **Function Explanation**:
  - `wait_for_port()`: 负责轮询监听端口，避免服务还没 bind 就让 wrk 先撞上去。
  - `taskset_wrap()`: 给后续核心隔离留接口；如果没设置 cpuset，就原样执行命令。
  - `set_profile_args()`: 把实验设计文档里的 profile 参数固化进脚本，避免手工敲错。
- **Pitfalls**:
  - 这份脚本现在保留 wrk 原始输出，不做 CSV 解析，是故意的。第一版 benchmark 最重要的是把原始证据留住，而不是先追求花哨汇总。
  - 脚本默认会给每组 worker 线程使用独立 DB 文件；如果后面为了省时间改成复用同一份 DB，结果就会更容易受前一轮残留影响。

---

fix(benchmark): 修正 wrk done 回调报错并优先使用项目 venv 启动 Python 依赖

## Git Commit Message
fix(benchmark): 修正 wrk done 报错并优先使用项目 venv

## Modification
- **server/tests/wrk/trace_model.lua**:
  - 修正 `done(summary, latency, requests)` 中对 `summary.threads` 的错误假设。当前 wrk 回调里这个字段并不稳定存在，直接拿来 `%d` 会触发 Lua `string.format` 报错。
  - 改为使用脚本已知的 `WRK_THREADS` 作为线程数展示，并对请求总数做空值兜底，避免整轮压测结束时才在收尾阶段 panic。
- **server/util/DevSubprocessManager.cpp**:
  - 新增 Python 解释器优先级选择逻辑：`DEV_PYTHON` > 当前激活 `VIRTUAL_ENV` > 仓库外层 `../venv/bin/python` > 系统 `python/python3`。
  - 这样 benchmark/本地联调时，如果 AI proxy 依赖装在项目自己的虚拟环境里，就不会再傻乎乎走系统 Python。

## Learning Tips
- **Newbie Tips**:
  - wrk 的 Lua 回调不是所有字段都稳稳存在，尤其 `done()` 里的 `summary` 别想当然照着名字就拿。
  - 本地开发里“我明明装了依赖却还是起不来”很多时候不是脚本错了，而是子进程压根没走你以为的那个 Python 解释器。
- **Function Explanation**:
  - `ResolvePreferredPython()`: 负责把“优先用哪个 Python”这件事集中收口，避免在 fork/exec 路径里散落一堆路径判断。
- **Pitfalls**:
  - `wrk` 主压测阶段即使成功，`done()` 回调最后崩一下也会把实验体验弄得很脏，因为你会误以为整轮 benchmark 都失败了。
  - 只依赖 `python`/`python3` 的 PATH 查找，在多虚拟环境机器上很容易起错解释器，尤其 benchmark 又经常从不同 shell 环境发起。
