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
