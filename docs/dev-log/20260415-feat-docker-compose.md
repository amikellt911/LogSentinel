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
