# Git Commit Message

feat(deploy): 补 Dockerfile 默认容器启动语义并落 compose 的 server 第一版

# Modification

- `.dockerignore`
- `docker/Dockerfile.ai-proxy`
- `docker/Dockerfile.server`
- `compose.yaml`
- `docs/todo-list/Todo_DockerCompose.md`

# Learning Tips

## Newbie Tips

- Dockerfile 里的 `CMD` 负责“镜像默认怎么启动”，`compose.yaml` 里的 `command` 负责“这次部署时怎么覆盖默认启动命令”，两者不是并存叠加。
- 多阶段构建里前面的 `builder` 阶段可以很脏，但最终镜像默认只会产出最后一个阶段；前面阶段的文件只能通过 `COPY --from=builder ...` 手动带到 runtime。
- `127.0.0.1` 在容器里只代表“当前容器自己”，不是宿主机，也不是另一个 compose service；跨容器访问应该用 service 名。
- SQLite 这类持久化数据不要和 `/app` 里的程序文件混放，容器化后更适合显式走 `/data/...` 再配 volume。

## Function Explanation

- `COPY --from=<stage> <src> <dst>`：从前一个构建阶段的文件系统里复制产物，不是从宿主机复制。
- `WORKDIR`：只影响后续相对路径命令的基准目录，不会改变 `apt-get install` 这类系统包安装位置。
- `--no-auto-start-proxy`：关闭后端主进程在本地开发场景里自动 fork Python sidecar 的行为，容器部署时必须显式关掉，避免 runtime 阶段找不到 `main.py`。

## Pitfalls

- 如果 server 容器继续吃默认 `trace_ai_base_url=http://127.0.0.1:8001`，compose 下它会错误地去请求自己，而不是 `ai-proxy` 容器。
- 如果 Dockerfile 的默认 `CMD` 不加 `--no-auto-start-proxy`，当前 runtime 镜像因为没有打包 `server/ai/proxy/main.py`，启动时会直接在本地 sidecar 探测阶段失败。
- `.dockerignore` 不能按“想拷哪个目录就放哪个子目录里”来理解，它跟 `docker build` 的 build context 绑定；当前从仓库根目录 build，就该把 `.dockerignore` 放在仓库根。

# 追加记录

- 在 `compose.yaml` 里补了 `ai-proxy` service 第一版，并让 `server` 通过最小 `depends_on` 先依赖它启动。
- 当前 `depends_on` 只解决启动顺序，不解决“服务已经 ready”；后续如果要更稳，需要再补 `healthcheck`。

---

# Git Commit Message

refactor(benchmark): 重构 benchmark 目录并按 suite 收口结果路径

# Modification

- `AGENTS.md`
- `CurrentTask.md`
- `GEMINI.md`
- `README.md`
- `docs/BENCHMARK_DEPLOY_PREP.md`
- `docs/BENCHMARK_SUITE_OVERVIEW.md`
- `docs/PERFORMANCE_PHASE_TEMPLATE.md`
- `docs/TRACE_WRK_BENCHMARK_GUIDE.md`
- `docs/todo-list/Todo_Benchmark.md`
- `server/tests/benchmark/README.md`
- `server/tests/benchmark/common/run_wrk_case.sh`
- `server/tests/benchmark/common/run_flamegraph_case.sh`
- `server/tests/benchmark/common/loadgen/trace_paced_sender.py`
- `server/tests/benchmark/common/utils/verify_trace_model_active_pool.py`
- `server/tests/benchmark/common/wrk/*`
- `server/tests/benchmark/suite_a/run_suite_a.sh`
- `server/tests/benchmark/suite_a/run_flamegraph.sh`
- `server/tests/benchmark/suite_b/README.md`
- `server/tests/benchmark/suite_d/run_suite_d.sh`
- `server/tests/benchmark/suite_d/run_flamegraph.sh`

# Learning Tips

## Newbie Tips

- 目录重构最怕只搬文件不改入口。只要 runner、结果目录、README 里还有旧路径，这次重构就不算真的完成。
- `common` 和 `suite_*` 的分工要先定死：`common` 负责“怎么跑”，`suite_*` 负责“这轮实验属于谁”，不然很快又会把 A/D 的脚本各拷一份。
- benchmark 结果目录如果不按 `suite` 分桶，后面哪怕脚本放对地方，产物还是会重新混在一起。

## Function Explanation

- `exec <command> "$@"`：用当前 shell 进程直接替换成目标命令，wrapper 不会再多套一层父进程，日志和退出码也更干净。
- `python3 -m py_compile <file.py>`：只做语法级校验，不运行业务逻辑，适合目录迁移后先确认 Python 入口还能被解释器正常解析。
- `bash -n <script.sh>`：只检查 shell 语法，不真正执行脚本，适合这类路径和 wrapper 重构后的最小验证。

## Pitfalls

- `run_bench.sh` 这种脚本如果直接复制成 `suite_a` 和 `suite_d` 两份，后面修一个 bug 大概率只会想起改其中一份。
- `trace_paced_sender.py` 这种 Python 入口如果还被 common runner 指回旧 `server/tests/wrk/`，目录搬完后火焰图入口会第一时间炸。
- 旧 `docs/dev-log` 里的历史路径可以保留，但 README、CurrentTask、AGENTS 这类活文档必须跟着新目录一起改，不然团队口径会一直分裂。

---

# Git Commit Message

feat(benchmark): 增加 Suite B sender 第一刀

# Modification

- `docs/todo-list/Todo_Benchmark.md`
- `server/tests/benchmark/suite_b/README.md`
- `server/tests/benchmark/suite_b/profiles.py`
- `server/tests/benchmark/suite_b/sender.py`
- `server/tests/benchmark/suite_b/sender_unit_test.py`

# Learning Tips

## Newbie Tips

- 这次先做 sender，不做 evaluator。sender 负责制造请求和真值账本，evaluator 后面才负责拿 manifest + SQLite 去算指标。
- `dry-run` 只写 manifest，不请求后端，适合先检查流量模型和真值标签，不适合拿来当真实性能数据。
- 单线程 sender 是第一刀 correctness 版本，不是最终 3 核 benchmark 版本；后续要补多 worker，但 manifest 字段不会因此改掉。

## Function Explanation

- `@dataclass`：可以把 Python 类当成接近 C++ `struct` 的数据容器，自动生成初始化函数。
- `heapq`：Python 标准库小顶堆；这次只按 `planned_emit_at_ms` 排序，等价于 C++ 里给 `priority_queue` 配一个按时间比较的 comparator。
- `urllib.request`：Python 标准库 HTTP 客户端；这次先不用 `requests/aiohttp`，避免为了 benchmark sender 引入额外依赖。
- `jsonl`：一行一个 JSON 对象，适合边发送边追加 manifest，也方便后续 evaluator 按行流式读取。

## Pitfalls

- `replay_clone` 不能直接替换原始 span。正确做法是“原始 span 正常发 + 复制品晚到”，否则 evaluator 分不清该保住的 span 和本该被 tombstone 拦住的复制品。
- 只看 `planned_emit_at_ms` 不够，后续还要记录 `actual_send_start_ms / actual_send_done_ms`，否则 sender 自己卡住时会把后端生命周期实验结果污染掉。
- `late_after_dispatch / replay_after_dispatch` 的真值标签必须是 `ignore_after_cutoff`，不然 completeness 和 pollution 指标会互相打架。

---

# Git Commit Message

feat(benchmark): 为 Suite B sender 增加多 worker 发送模型

# Modification

- `docs/dev-log/20260415-feat-docker-compose.md`
- `docs/todo-list/Todo_Benchmark.md`
- `server/tests/benchmark/suite_b/README.md`
- `server/tests/benchmark/suite_b/sender.py`
- `server/tests/benchmark/suite_b/sender_unit_test.py`

# Learning Tips

## Newbie Tips

- 多 worker sender 里最容易写错的不是并发发送，而是“谁负责写 manifest”。如果让 worker 一边发 HTTP 一边自己写文件，最后拿到的账本顺序和发送观测会被线程调度搅乱，很难解释 benchmark 真值。
- `queue.Queue` 适合这种“主线程负责调度，worker 负责阻塞 I/O”的模型，因为它天然带锁和条件变量，不需要自己再补一层 wait/notify。
- CLI 参数如果想做单测，最好让 `parse_args()` 支持显式传 `argv`，不然测试只能硬改全局 `sys.argv`，容易把别的测试一起污染。

## Function Explanation

- `queue.Queue()`：Python 标准库线程安全队列。主线程把到点事件塞进 `send_queue`，worker 发完后再把结果塞回 `result_queue`。
- `threading.Thread(..., daemon=True)`：这里的 daemon 只是兜底，真正的正常退出还是靠主线程投递 `None` 哨兵并 `join()`。
- `urllib.error.URLError`：代表连 HTTP 响应头都没拿到的传输层失败，这次 sender 里把它折叠成 `http_status=0`，避免单个网络错误直接把 benchmark sender 线程打死。

## Pitfalls

- 多 worker 下 `manifest.jsonl` 默认按完成顺序写，不再保证和 `planned_emit_at_ms` 一致；后续 evaluator 如果要稳定重放，必须自己按计划时间离线排序。
- worker 退出哨兵的数量必须和 worker 数一致。只塞一个 `None`，只会放走一个线程，剩下的线程会永远卡在 `get()`。
- `run_sender()` 不能把“发了多少条事件”直接当进程退出码返回，否则 shell 会把非零成功运行误判成失败。

---

# Git Commit Message

feat(benchmark): 增加 Suite B evaluator 第一刀

# Modification

- `docs/dev-log/20260415-feat-docker-compose.md`
- `docs/todo-list/Todo_Benchmark.md`
- `server/tests/benchmark/suite_b/README.md`
- `server/tests/benchmark/suite_b/evaluator.py`
- `server/tests/benchmark/suite_b/evaluator_unit_test.py`

# Learning Tips

## Newbie Tips

- `manifest` 记录的是“发送事件”，不是“最终 trace 结果”。所以 evaluator 第一件事不是直接查 SQLite，而是先把事件层折叠成 `ExpectedMergeSet / ExpectedIgnoreEvents / ReplayEvents` 这几种视图。
- `trace_completeness_rate` 和 `trace_pollution_rate` 不能混着算。前者只回答“该有的 span 有没有丢”，后者单独回答“有没有多出不该有的 span”，不然一个错误会被重复处罚。
- drain 等待最好看“SQLite 计数稳定”而不是拍脑袋 `sleep 3s`。样本规模、flush 节奏和机器负载一变，固定 sleep 很容易要么太短，要么浪费时间。

## Function Explanation

- `sqlite3.connect(...)`：Python 标准库 SQLite 连接；这次 evaluator 第一刀直接用它读最终快照，不先走 HTTP 详情接口。
- `time.monotonic()`：单调时钟，不受系统时间回拨影响，适合做“最多等多久”的超时判断。
- `set.issubset(...)`：集合包含判断；这里刚好拿来表达“ExpectedMergeSet 里的 span 是否都被最终落库保住了”。

## Pitfalls

- `duplicate_persistence_rate` 第一刀先按 trace 级副作用归因，只要 replay 所在 trace 因额外 span 或 `summary.span_count` 膨胀而偏离期望，就把该 replay 事件记成 bad；它还不是最细的单事件因果定位，后续如需更细要再接 runtime log。
- 如果 sender 存在大量非 2xx 请求，而 evaluator 仍然直接拿 manifest 全量事件算指标，会把“发送端没送达”和“后端处理错误”混在一起；当前第一刀默认实验运行在本地健康链路上，后续如果要上更脏的网络条件，需要再显式过滤这类事件。
- evaluator 先读 `trace_summary` 再读 `trace_span` 时，如果不先等 SQLite 稳定，就可能读到半成品快照，导致 completeness / pollution 的结果比真实值更差。

---

# Git Commit Message

feat(benchmark): 为 Suite B 增加单次 case runner

# Modification

- `docs/dev-log/20260415-feat-docker-compose.md`
- `docs/todo-list/Todo_Benchmark.md`
- `server/tests/benchmark/suite_b/README.md`
- `server/tests/benchmark/suite_b/run_suite_b.py`
- `server/tests/benchmark/suite_b/run_suite_b_unit_test.py`

# Learning Tips

## Newbie Tips

- `run_suite_b.py` 第一刀只是单次 case runner，不是正式矩阵实验器。先把“sender -> evaluator -> 统一 JSON”这条最小闭环跑通，后面再在外面包 3 x 2 批跑脚本，职责才不会打架。
- `trace_lifecycle_profile` 现在被收进结果 JSON，是为了让实验元数据自描述；但第一刀 runner 并不会替你起后端，也不会替你真的切换后端 profile。
- 编排层最容易失控的点就是顺手做太多。只要还没需要多 case、多进程起停，就别急着把 orchestration 写成大而全框架。

## Function Explanation

- `argparse.Namespace(...)`：这里用它临时重组 sender 参数，比手写一个新 dataclass 更轻，适合这种“把一份统一 CLI 映射给下游模块”的编排层。
- `Path(...).parent.mkdir(parents=True, exist_ok=True)`：确保 manifest / output_json 的父目录存在，避免 runner 只是因为目录没建好就提前失败。
- `result.update(evaluation)`：把 evaluator 输出并进统一结果对象，后续做论文表格或批跑汇总时，只需要读一份 JSON。

## Pitfalls

- 单次 runner 和矩阵 runner 不能混写在一起。前者负责一次 case 的输入输出闭环，后者才负责 profile 笛卡尔积、后端起停和结果汇总；一开始就混在一份脚本里，后面只会越改越乱。
- `run_suite_b.py` 现在会打印 sender 的 stdout，所以正式批跑时如果想拿纯 JSON 输出，后面需要再决定是重定向 sender 日志，还是给 sender 增加 quiet 模式。
- dry-run + 空 SQLite 的最小验证只证明编排逻辑活着，不代表后端生命周期实验已经真实跑通；正式 benchmark 仍然要用真实后端和真实 DB。

---

# Git Commit Message

feat(benchmark): 增加 Suite B 矩阵 runner 第一刀

# Modification

- `docs/dev-log/20260415-feat-docker-compose.md`
- `docs/todo-list/Todo_Benchmark.md`
- `server/tests/benchmark/suite_b/README.md`
- `server/tests/benchmark/suite_b/run_suite_b_matrix.py`
- `server/tests/benchmark/suite_b/run_suite_b_matrix_unit_test.py`

# Learning Tips

## Newbie Tips

- `run_suite_b.py` 和 `run_suite_b_matrix.py` 不是一回事。前者解决“一次 case 怎么跑”，后者解决“6 个 case 怎么排队跑并汇总结果”。把这两层拆开，后面改 sender/evaluator 或改矩阵维度时都更稳。
- Suite B 的矩阵 case 不能共用一个 SQLite。因为 evaluator 看的就是最终持久化结果，如果前一组的 trace 留在同一个 DB 里，后一组的 completeness / pollution 会立刻串味。
- 只起 shell 再 terminate shell，不等于真的把后端停干净。只要 `server-command` 允许 shell 模板，停机时就要考虑“真正的 LogSentinel 其实是 shell 的子进程”。

## Function Explanation

- `str.format(...)`：这里拿来给 `server-command` 注入 `{sqlite_db} / {trace_lifecycle_profile} / {port} / {log_path}`，比手写字符串拼接更稳，也更容易扩新占位符。
- `socket.connect_ex(...)`：用来做最小端口探测；返回 `0` 代表端口已经可连，不需要再上 curl。
- `os.killpg(...)`：按进程组发信号；这次专门用它收 shell + 真正后端子进程，避免矩阵 runner 留下孤儿进程。

## Pitfalls

- `shell=True` 虽然让 `server-command` 模板更灵活，但也把“停机要不要管子进程”这个问题带进来了；如果后续有人把这层改回只杀 `process.pid`，很容易重新留下端口残留。
- 矩阵 runner 现在默认按 case 顺序串行执行，没有并发；这是故意的，因为当前重点是先保证 case 之间完全隔离，而不是抢跑实验总时长。
- 用 dummy server + `--dry-run` 的最小验证，只能证明矩阵 runner 的编排活着；真正要证明 `protected` 和 `minimal` 差异，还得用真实后端和真实 sender 请求。

---

# Git Commit Message

feat(benchmark): 为 Suite B 矩阵 runner 增加资源控制 CLI

# Modification

- `CurrentTask.md`
- `docs/dev-log/20260415-feat-docker-compose.md`
- `docs/todo-list/Todo_Benchmark.md`
- `server/tests/benchmark/suite_b/README.md`
- `server/tests/benchmark/suite_b/run_suite_b_matrix.py`
- `server/tests/benchmark/suite_b/run_suite_b_matrix_unit_test.py`

# Learning Tips

## Newbie Tips

- benchmark runner 如果强依赖一长串 `--server-command` 模板，后面机器核数一变，实验命令很快就会变成人脑难以检查的字符串。把资源参数拉成顶层 CLI，复现实验时才看得清。
- `--send-workers` 只是 sender 的并发，不是后端线程数，更不是绑核。Suite B 里 sender 现在还跑在 matrix runner 自己的 Python 进程里，所以后端绑核和 sender 并发是两条线。
- 4 核本机和 16 核云机最该改的是“资源参数”，不是 lifecycle 参数。`trace_idle_timeout_ms / grace / tombstone` 这些决定实验语义，不该为了迁就机器核数乱改。

## Function Explanation

- `shlex.split(...)`：把 `--server-bin` 这种命令字符串按 shell 语义切成 token，避免把 `python3 fake_server.py` 错当成单个可执行文件路径。
- `shlex.quote(...)`：把每个 token 重新转回安全的 shell 字符串，避免路径里带空格时启动命令被 shell 拆坏。
- `taskset -c <cpuset>`：把整个后端进程绑到指定 CPU 集合上；当前这一刀只绑后端，不会自动把 sender 也拆出去单独绑核。

## Pitfalls

- 如果同时传了 `--server-command` 和新的资源 CLI，当前 runner 会优先信 `--server-command`。因为模板模式本来就是“你自己全权接管启动命令”，不能再偷偷混进默认拼装参数。
- `server_io_threads` 目前还没有进 `run_suite_b_matrix.py`，不是忘了，而是当前后端 CLI 侧并没有这个稳定入口，不能在 runner 里假装支持。
- 默认命令现在会把 benchmark 关心的后端参数显式带上；如果后面有人把这些参数删回“依赖 main.cpp 默认值”，历史 benchmark 结果的可比性会立刻变差。

---

# Git Commit Message

fix(benchmark): 修复 Suite B 端口假阳性并补 server_io_threads CLI

# Modification

- `CurrentTask.md`
- `docs/dev-log/20260415-feat-docker-compose.md`
- `docs/todo-list/Todo_Benchmark.md`
- `server/src/main.cpp`
- `server/tests/benchmark/suite_b/README.md`
- `server/tests/benchmark/suite_b/run_suite_b_matrix.py`
- `server/tests/benchmark/suite_b/run_suite_b_matrix_unit_test.py`

# Learning Tips

## Newbie Tips

- “端口能连通”不等于“这次新起的后端已经启动成功”。如果旧进程本来就占着这个端口，`connect_ex()` 一样会成功，所以 benchmark runner 必须先判空端口，再看 child 进程有没有提前退出。
- `server_io_threads` 和 `worker_threads` 不是一回事。前者对应 MiniMuduo 的 Reactor/I/O 线程，后者才是承接 Trace 聚合、AI、落库协作的主工作线程池。
- benchmark CLI 可以用更贴近论文语义的名字，但内部实现不一定要跟数据库字段同名。这次外部叫 `--server-io-threads`，内部仍然压到现有 `kernel_io_threads` 冷启动语义上。

## Function Explanation

- `socket.connect_ex(...)`：返回 `0` 代表目标地址当前可连。它适合做“端口是否已被占用”的探针，但单独使用并不能证明新进程就是端口拥有者。
- `process.poll()`：返回 `None` 代表子进程还活着；一旦拿到退出码，就说明 child 已经提前退出，通常应该连同最近的启动日志一起报错。
- `setThreadNum(num)`：这里作用在 MiniMuduo 的 I/O 线程模型上，不会自动扩到业务 worker 线程池。

## Pitfalls

- 如果只在 launch 之后检查端口，而不在 launch 之前先判空，那么旧进程占端口的场景还是会被误认为“ready 已达成”。
- `server_io_threads` 这种入口如果只补到 matrix runner，不补到后端 `main.cpp`，最后就会变成“命令行看起来支持，实际上后端根本没消费”的假配置。
- 这次 matrix runner 的启动校验只解决“旧进程占端口”的假阳性，不等于已经把所有 ready 语义都做成强一致；后续如果还要更严，可以继续把启动日志关键字也纳入 readiness 条件。

---

# Git Commit Message

chore(benchmark): 下调 Suite B 默认 sweep tick 到 100ms

# Modification

- `docs/dev-log/20260415-feat-docker-compose.md`
- `server/tests/benchmark/suite_b/README.md`
- `server/tests/benchmark/suite_b/run_suite_b_matrix.py`

# Learning Tips

## Newbie Tips

- `trace_sweep_interval_ms` 变小，不等于后端语义变了。它首先改变的是“时间轮多久推进一次 tick”，也就是 timeout / sealed deadline 的量化精度。
- 如果 tick 太粗，配置写的是 `800ms idle timeout`，真实效果就可能提前到大约 `600~800ms` 之间触发；因为会话 deadline 是挂在 tick 上，而不是直接挂在精确 wall-clock 上。
- benchmark 里把默认 tick 从 `200ms` 调到 `100ms`，是在减少边界样本被量化误差误伤的概率，不是在偷偷放宽 protected 的能力边界。

## Function Explanation

- `parser.add_argument("--trace-sweep-interval-ms", ..., default=100)`：这里改的是矩阵 runner 的默认实验参数，不会强行覆盖你手工传入的 CLI 值；只是在你没显式写这个参数时，默认给 Suite B 更细的 tick。

## Pitfalls

- 这次改的是 Suite B benchmark 默认值，不是 `main.cpp` 里的全局后端默认值；如果你手工命令里仍然写 `--trace-sweep-interval-ms 200`，那就还是 200。
- 只调 sweep tick 并不能彻底消除边界误差，它只是把误差区间缩小一半；如果后面还要做“绝不早于配置值”的语义保证，就得继续改 TraceSessionManager 的 deadline 计算方式。
