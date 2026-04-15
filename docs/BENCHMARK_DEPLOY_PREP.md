# Benchmark / Docker 前置准备清单

这份文档只做一件事：把后面换到新服务器时，跑 benchmark、做 docker 验收、做答辩演示之前必须准备好的环境一次写清楚。

不要等代码传上云机之后才临时补包。那样最容易把“代码问题”和“环境问题”搅在一起。

## 1. 机器建议

- Linux：优先 Ubuntu 22.04/24.04，x86_64
- CPU：benchmark 机器建议至少 8 核；如果要扫 `Suite D`，优先 16 核或 32 核
- 内存：至少 8 GB；如果要开更大的队列和并发，优先 16 GB+
- 磁盘：SSD，避免 SQLite/WAL 被机械盘拖慢

## 2. 后端构建依赖

最小需要：

- `build-essential`
- `cmake`
- `ninja-build`（可选，但建议装）
- `pkg-config`
- `git`
- `curl`
- `sqlite3`
- `libsqlite3-dev`
- `python3`
- `python3-pip`
- `python3-venv`

推荐一次装齐：

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake ninja-build pkg-config git curl \
  sqlite3 libsqlite3-dev \
  python3 python3-pip python3-venv \
  nodejs npm \
  wrk linux-perf util-linux iproute2 lsof
```

说明：

- `util-linux` 提供 `taskset`
- `iproute2` 提供 `ss`
- `lsof` 用于 benchmark 脚本探测端口占用
- `linux-perf` 只在 `server/tests/benchmark/common/run_flamegraph_case.sh` 或火焰图实验里需要
- `nodejs/npm` 只在本机需要重新构建前端时需要；如果直接用仓库里已经产出的 `client/dist`，可以不装

## 3. Python AI proxy 依赖

进入 `server/ai/` 后安装：

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

建议：

- benchmark 机器优先固定一份虚拟环境，不要直接污染系统 Python
- 如果只跑 `mock` provider，外网不是硬依赖
- 如果要跑 `gemini / glm` 真链路，必须确认云机能访问外部模型 API

## 4. 前端构建依赖

如果要在云机上重新构建前端：

```bash
cd client
npm install
npm run build
```

最少需要：

- `nodejs`
- `npm`

如果只是验证单入口部署，而仓库里已经有可用的 `client/dist`，可以先跳过这一步。

## 5. benchmark 额外工具

要跑 `server/tests/benchmark/suite_a/run_suite_a.sh`、`server/tests/benchmark/suite_d/run_suite_d.sh` 或 `server/tests/benchmark/common/run_flamegraph_case.sh`，至少确认以下命令存在：

- `wrk`
- `taskset`
- `ss`
- `lsof`
- `sqlite3`

如果要跑火焰图，再额外确认：

- `perf`
- FlameGraph 工具目录，例如 `~/tools/FlameGraph`

检查命令可以直接用：

```bash
command -v wrk taskset ss lsof sqlite3
command -v perf
```

## 6. benchmark 机器执行前检查

正式压测前至少做这几件事：

1. 确认没有旧的 `LogSentinel`、AI proxy、mock webhook 残留进程
2. 确认端口 `8080 / 8001 / 9999` 没被其他服务占用
3. 确认 `ulimit -n` 足够，建议至少 `65535`
4. 确认当前用户能跑 `taskset`
5. 如果要跑 `perf`，确认当前内核权限允许采样
6. 不要复用旧的 benchmark SQLite 文件，避免 `trace_id` 冲突把结果测脏

建议先跑：

```bash
ulimit -n
ss -ltn '( sport = :8080 or sport = :8001 or sport = :9999 )'
ps -ef | grep LogSentinel | grep -v grep
ps -ef | grep 'server/ai/proxy/main.py' | grep -v grep
```

## 7. Docker / Compose 前置条件

如果后面要补 `docker-compose` 验收，云机至少要有：

- Docker Engine
- Docker Compose Plugin（`docker compose`）

Ubuntu 上建议：

```bash
sudo apt-get update
sudo apt-get install -y docker.io docker-compose-v2
sudo systemctl enable --now docker
```

如果当前发行版仓库没有 `docker-compose-v2`，就按官方 Docker 安装方式处理，但目标不变：

- `docker version` 可用
- `docker compose version` 可用

另外要确认当前用户能直接跑 Docker：

```bash
docker version
docker compose version
```

如果需要免 `sudo`：

```bash
sudo usermod -aG docker "$USER"
```

然后重新登录 shell。

## 8. 这轮最小验收目标

后面无论是 benchmark 还是 docker，都先不要扩需求，先满足这条最小线：

- 后端能构建
- Python AI proxy 能启动
- 前端能构建或直接由后端托管已有 `client/dist`
- `suite_a/run_suite_a.sh` 或 `suite_d/run_suite_d.sh` 能在新机器上直接落结果到 `server/tests/benchmark/results/<suite>/`
- 后续补上 `docker compose up` 后，最小演示环境能一条命令拉起

做到这里，环境就算准备合格了。
