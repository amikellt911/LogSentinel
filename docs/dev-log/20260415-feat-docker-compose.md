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
