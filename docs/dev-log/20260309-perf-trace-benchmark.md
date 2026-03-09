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

---

fix(benchmark): 修复 run_bench.sh 被旧端口监听污染的问题

## Git Commit Message
fix(benchmark): 修复 run_bench 端口污染检查

## Modification
- **server/tests/wrk/run_bench.sh**:
  - 新增 `ensure_port_available()`，在每轮启动前先检查目标端口是否已有监听。
  - 如果监听者是旧的 `LogSentinel`，默认会先 `TERM` 掉并等待端口释放；如果监听者不是 benchmark 目标进程，则直接失败退出，避免误杀未知服务。
  - 新增 `port_owned_by_pid()`，在新服务启动并等待端口 ready 之后，再次确认当前监听这个端口的就是刚拉起来的那一个 PID。
  - 这样 wrk 不会再被“旧服务也在监听同一端口”这种脏环境骗过去。

## Learning Tips
- **Newbie Tips**:
  - “端口 ready” 和 “监听这个端口的是我刚拉起来的进程” 不是一回事。benchmark 只检查 ready，很容易被旧实例污染。
- **Function Explanation**:
  - `lsof -tiTCP:<port> -sTCP:LISTEN`: 用来找当前到底是谁在监听目标端口，比单纯 `ss` 看端口是否 ready 更适合做 benchmark 前置检查。
- **Pitfalls**:
  - 如果脚本只会等端口 ready，而不会核对监听者 PID，那么旧服务和新服务抢同一端口时，wrk 看起来照样能跑，但整轮实验对象其实是错的。
  - 在 bash 里把“shell 函数”放到后台执行，`$!` 拿到的未必是最终业务进程本体 PID，可能只是后台 subshell 的 PID。benchmark 如果要校验端口归属，一定要小心这一层。

---

fix(trace): 修复 timeout 压测下 TraceSessionManager 的跨线程竞态崩溃

## Git Commit Message
fix(trace): 修复 TraceSessionManager 超时扫与入站并发竞态

## Modification
- **server/core/TraceSessionManager.h**:
  - 新增 `std::mutex mutex_`，作为 TraceSessionManager 第一版最小互斥保护。
  - 新增私有 `PushLocked(...)` / `DispatchLocked(...)`，把“公开入口拿锁”和“内部已持锁逻辑”拆开，避免 `Push -> Dispatch`、`Sweep -> Dispatch` 这种路径重复加锁死锁。
- **server/core/TraceSessionManager.cpp**:
  - `size()`、`Push()`、`Dispatch()`、`SweepExpiredSessions()` 统一改为公开入口拿锁。
  - 将原 `Push()` 主体迁入 `PushLocked(...)`，内部触发分发时直接调用 `DispatchLocked(...)`，不再绕公开接口。
  - 将原 `Dispatch()` 主体迁入 `DispatchLocked(...)`，保持线程池任务体不在 manager 锁内执行，只保护 manager 自己的容器与计数状态。
  - `SweepExpiredSessions()` 收集到超时 trace 后，改为在同一把锁保护下调用 `DispatchLocked(...)`，消除主 loop 定时器线程与 HTTP 入站线程并发修改 `sessions_ / index_by_trace_ / time_wheel_` 的竞态。
- **docs/todo-list/Todo_TraceSessionManager.md**:
  - 追加并勾选“gdb 定位 timeout 竞态崩溃”“补最小互斥保护”“复跑 timeout 验证不再崩溃”三项记录。

## Learning Tips
- **Newbie Tips**:
  - “只给两个容器加锁”这句话通常是错的。真正要保护的是“一整套状态机的一致性”，包括容器、索引、计数器、时间轮、会话内部字段。
  - `std::atomic` 只能解决单个变量的原子读写，解决不了 `vector + map + set + counter` 这种复合状态的一致性。
  - 第一次止血优先选 `std::mutex`，别急着上 `recursive_mutex`、自旋锁、无锁结构。先把正确性救回来，再谈细化。
- **Function Explanation**:
  - `gdb bt` / `thread apply all bt`：抓到 core 之后，先看当前崩溃线程栈，再看所有线程栈。比继续猜“是不是旧时间轮节点又来了”有效得多。
  - `std::lock_guard<std::mutex>`：适合这种明确的作用域加锁。既然持锁范围很清楚，就别手搓 `lock()/unlock()`。
- **Pitfalls**:
  - 这次 crash 的根因不是 timer callback 没在 loop 线程，而是 `Push()` 根本跑在另一个 IO loop 线程里。给 `runEvery` 再套一层 `runInLoop()` 没意义，因为 timer 回调本来就在 loop 线程。
  - `timeout` 模型比 `end/capacity` 更容易把竞态打出来，不是因为它“更复杂”这么一句空话，而是因为它会把主 loop 的 `SweepExpiredSessions()` 路径稳定拉进来，与 HTTP 入站线程同时改同一个 manager。

## Validation
- `cmake --build server/build --target LogSentinel test_trace_session_manager_unit test_trace_session_manager_integration`
- `cd server/build && ctest -R '^(TraceSessionManagerUnitTest|TraceSessionManagerIntegrationTest)\\.' --output-on-failure`
- 手动 timeout 最小复现：
  - 前台启动 `LogSentinel`，参数与 `run_bench.sh timeout` 一致
  - 单独执行 `wrk -t1 -c100 -d10s -s server/tests/wrk/trace_model.lua http://127.0.0.1:8080 -- timeout 8 4 2048`
  - 结果：服务端在 wrk 结束后仍存活（`server_alive_after_wrk`），不再出现先前的 `Segmentation fault (core dumped)`

---

fix(benchmark): 修正 wrk done 阶段的 mode/thread 展示字段

## Git Commit Message
fix(benchmark): 修正 trace_model done 展示字段

## Modification
- **server/tests/wrk/trace_model.lua**:
  - `done()` 不再直接相信当前 Lua 状态里的 `mode` 和 `WRK_THREADS`，而是优先从环境变量 `TRACE_WRK_MODE`、`TRACE_WRK_THREADS` 读取展示值。
  - 这样可以避免 wrk 收尾阶段使用另一份 Lua 状态时，又退回脚本默认值 `mode=end`，把 benchmark 日志看歪。
- **server/tests/wrk/run_bench.sh**:
  - 在启动 wrk 前显式导出 `TRACE_WRK_MODE` 和 `TRACE_WRK_THREADS`，让 `trace_model.lua` 的 `done()` 可以拿到这轮实验的真实 profile。

## Learning Tips
- **Newbie Tips**:
  - wrk 的 `done()` 只是“收尾回调”，不要默认它和 `request()` 共享同一份你以为的运行态。展示字段如果特别重要，最好显式传进去。
- **Function Explanation**:
  - `os.getenv(...)`: 读取 shell 注入的环境变量，适合这种“收尾日志需要知道本轮 profile，但不想重新发明参数传递协议”的场景。
- **Pitfalls**:
  - 如果 benchmark 的收尾日志一直打印错 profile，后面你再回头整理结果，很容易把 `timeout/token/mixed` 的结论记错，即使真实压测本身是对的。

## Validation
- `bash -n server/tests/wrk/run_bench.sh`
- 使用临时 `python3 -m http.server 18080` 做 1 秒最小 wrk 验证：
  - `TRACE_WRK_MODE=token TRACE_WRK_THREADS=1 wrk -t1 -c1 -d1s --latency -s server/tests/wrk/trace_model.lua http://127.0.0.1:18080 -- token 8 4 2048`
  - `done()` 输出已变为 `trace_model done: mode=token, threads=1, ...`

---

perf(flamegraph): 新增最小版 CPU 火焰图编排脚本

## Git Commit Message
perf(flamegraph): 新增 trace flamegraph 编排脚本

## Modification
- **server/tests/wrk/run_flamegraph.sh**:
  - 新增独立 flamegraph harness，职责只做一件事：按固定 profile 启动 `LogSentinel`，先 warmup，再用 `perf` 附着采样，并在正式 wrk 压力窗口内生成 CPU 火焰图。
  - 复用 benchmark 里已经验证过的 `end/capacity/token/timeout/mixed` profile 参数，避免 flamegraph 和 benchmark 用两套口径。
  - 自动处理端口占用检查、结果目录创建、服务端日志/预热日志/wrk 日志/perf 原始数据/svg 输出，以及最后的 cleanup。
  - 通过环境变量支持 `SERVER_CPUSET/WRK_CPUSET/WARMUP_DURATION/DURATION/CONNECTIONS/PERF_FREQ/PERF_CALL_GRAPH/FLAMEGRAPH_DIR` 等最小覆盖点，默认值尽量贴合当前 4 核机器的实验习惯。
- **docs/todo-list/Todo_Benchmark.md**:
  - 追加并勾选 flamegraph 编排脚本任务，和 benchmark harness 区分开。

## Learning Tips
- **Newbie Tips**:
  - 火焰图不是“程序里额外开个线程记录函数”，而是 `perf` 在采样窗口里不断抓当前调用栈快照，最后把样本堆成一张图。
  - 第一张火焰图优先选 `end` 这种纯路径，不要上来就拿 `mixed`。因为火焰图本来就是做热点归因，变量越少越容易解释。
  - warmup 不是装饰。既然 SQLite、Python proxy、连接和分配器都有冷启动成本，那正式采样前先热几秒，图更接近稳态。
- **Function Explanation**:
  - `perf record -p <pid> -- sleep <duration>`：把采样窗口绑定在目标服务进程上，`sleep` 只是用来控制采样持续多久。
  - `perf script`：把二进制 perf 数据还原成文本栈。
  - `stackcollapse-perf.pl`：把大量文本栈折叠成 FlameGraph 需要的 folded stack 格式。
  - `flamegraph.pl`：把折叠栈画成 svg。
- **Pitfalls**:
  - `perf` 必须包住正式 wrk 的压力阶段，不是 wrk 跑完以后再单独采。后采样只会看到空闲或收尾，不是热点。
  - 如果 flamegraph 脚本和 benchmark 脚本各自偷偷用不同的 trace 参数，最后 benchmark 数字和火焰图热点会对不上，解释会很乱。

## Validation
- `bash -n server/tests/wrk/run_flamegraph.sh`
- `bash server/tests/wrk/run_flamegraph.sh --help`

---

fix(flamegraph): 修复首次 flamegraph 采样 0 样本的问题

## Git Commit Message
fix(flamegraph): 修正 perf 目标线程与回栈方式

## Modification
- **server/tests/wrk/run_flamegraph.sh**:
  - 将默认 `PERF_CALL_GRAPH` 从 `fp` 改为 `dwarf`。既然当前二进制没有专门用 `-fno-omit-frame-pointer` 重编，直接用 `fp` 很容易拿不到可靠栈。
  - 新增 `server_thread_ids()`，在 `perf record` 前显式收集 `LogSentinel` 的所有线程 TID，并改为 `-t tid1,tid2,...` 采样，而不是只盯主 PID。
  - 增加 `WARMUP_SETTLE_SEC`，在 warmup 结束后短暂等待，让正式 flamegraph 阶段别一上来就吃满前一波残留积压。
  - `--help` 同步补上新的默认值和可覆盖变量说明。

## Learning Tips
- **Newbie Tips**:
  - `perf.data` 文件存在，不代表真的采到样本。真正要看的是 `perf report --header-only` 里有没有 `time of first sample` 和非 0 的 `sample duration`。
  - 如果只盯进程主 PID，而真正干活的是其它 worker 线程/IO 线程，你可能会得到“perf 成功跑完，但样本数是 0”这种特别恶心的假成功。
- **Function Explanation**:
  - `ps -T -p <pid> -o spid=`：列出某个进程当前所有线程的 TID，适合喂给 `perf record -t ...` 做线程级采样。
  - `--call-graph dwarf`：通过 DWARF 展开调用栈，通常比 `fp` 更不依赖你是否重编了 frame pointer，但开销也更高一点。
- **Pitfalls**:
  - flamegraph 第一轮失败并不一定是 `perf` 权限不够，也可能是“采样目标不对”或者“回栈方式和当前二进制不匹配”。
  - warmup 如果紧接着正式 wrk，不留一点点缓冲，正式阶段更容易被 warmup 残留积压带偏，尤其当前系统本来就容易快速进入背压。

## Validation
- 检查失败现场：
  - `perf.data` 只有 16K
  - `perf report --header-only` 中 `time of first sample : 0.000000`
  - `sample duration : 0.000 ms`
  - 说明首次 flamegraph 失败的根因不是 SVG 生成脚本坏了，而是 `perf` 根本没采到有效样本
- 继续追查后确认 flamegraph harness 还存在第二个脚本层 bug：
  - 启动时直接把 `$!` 当成 `SERVER_PID`
  - 但这拿到的是外层启动壳 PID，不是真正监听 `8080` 的 `LogSentinel`
  - 导致日志里出现 `pid=542161 tids=542161` 这种只采到单个线程的假目标
- 已补上和 `run_bench.sh` 同级别的 listener PID 反查逻辑：
  - 端口 ready 后通过 `lsof` 找真正监听 `8080` 的业务进程
  - `cleanup` 同时区分 `SERVER_PID` 和 `SERVER_LAUNCH_PID`
  - 避免 perf 再次附着到外层壳进程而不是服务本体
- 继续最小对照实验后确认还有第三个根因：
  - 当前环境下 `perf` 默认事件 `cycles` 会“命令成功、生成 perf.data、但 0 样本”
  - 复现实验：对同一个 `yes > /dev/null` 进程
    - `perf record -g --call-graph dwarf -p <pid>` 得到 0 样本
    - `perf record -e cpu-clock -g --call-graph dwarf -p <pid>` 1 秒即可采到 98 个样本
  - 说明之前 flamegraph 失败不是 `FlameGraph` 脚本坏了，而是默认 PMU 事件在这台机器/当前权限下不可用
- 已将 flamegraph harness 默认事件改为 `PERF_EVENT=cpu-clock`，并保留环境变量覆盖口子，避免每次都踩“成功但 0 样本”的坑
- 修正后重新通过：
  - `bash -n server/tests/wrk/run_flamegraph.sh`
  - `bash server/tests/wrk/run_flamegraph.sh --help`
