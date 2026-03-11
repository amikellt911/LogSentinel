# LogSentinel

LogSentinel 是一个面向中小规模服务排障场景的 C++17 日志 / Trace 实时分析项目。当前仓库已经包含可运行的后端主链路、Python AI proxy、前端页面、测试与压测脚本；本文档描述的是当前代码状态，不再把“目标架构草图”当成现状。

## 项目现在能做什么

- 接收离散 Span，请求入口为 `POST /logs/spans`
- 按 Trace 会话聚合 Span，并在多种触发条件下分发
- 在高水位下做分级背压，向入口返回 `503 + Retry-After`
- 将 `trace_summary / trace_span / trace_analysis` 异步批量写入 SQLite（WAL）
- 可选通过 Python FastAPI proxy 调用模型服务，并在 critical 风险时触发 Webhook
- 提供 Dashboard / History / Settings 页面与对应后端接口（后端接口没有写好）
- 提供 GTest、smoke 脚本、wrk 压测脚本和 GitHub Actions workflow

## 当前边界

- 仓库里同时存在两条链路：
  - 旧日志批处理链路：`/logs` -> `LogBatcher` -> `SqliteLogRepository`
  - 新 Trace 主链路：`/logs/spans` -> `TraceSessionManager` -> `BufferedTraceRepository` -> `SqliteTraceRepository`
- 当前演进重点是 Trace 主链路；读侧 API 还没有完全切到 Trace 存储，`/dashboard`、`/history`、`/results/*` 主要仍服务旧链路
- `TraceExplorer`、`Benchmark` 以及部分 Dashboard 图表仍有 mock 数据补位，前端不等于全链路真实可观测平台
- Trace AI 分析不是默认开启：如果没有显式启动 proxy 或指定 Trace AI provider，Trace 链路仍会聚合并落主数据，但不会自动做 Trace AI 分析
- `advanced smoke` 与 integration workflow 目前更适合手动触发，不要把当前 CI 理解成“所有链路全自动门禁”

## 核心链路

### 1. 接入层

后端基于 MiniMuduo 的 `EventLoop + TcpServer` 组织 HTTP 接入层。请求解析、路由、CORS 和响应发送都在这一层完成；入口尽量只做轻量工作，把重活交给后续线程池或后台 flush 线程。

### 2. Trace 聚合与分级背压

`POST /logs/spans` 的请求会进入 `TraceSessionManager`：

- 以 `trace_key / span_id / parent_span_id` 维护活跃会话
- 按 `trace_end`、Span 容量阈值、Token 阈值、idle timeout 触发分发
- 用时间轮维护 collecting / retry 会话
- 用 `buffered spans / active sessions / pending tasks` 三类指标判断水位
- 高水位优先拒绝新 Trace，critical 水位才拒绝存量 Trace 的后续 Span
- 线程池 submit 失败时把会话回滚到内存，并按 `1/2/4/8/16` tick 指数退避重试

### 3. 持久化

Trace 主链路通过 `BufferedTraceRepository` 把主数据和分析结果分两条缓冲线写入：

- `trace_summary + trace_span` 先走 primary buffer
- `trace_analysis + prompt_debug` 走 analysis buffer
- 后台 flush 线程按“容量阈值 / 时间阈值”切桶并批量刷入 SQLite
- SQLite 使用 WAL，目标是把热路径上的等待尽量变短，而不是让 Reactor 主线程直接阻塞在磁盘 I/O 上

### 4. AI proxy 与告警

- Python FastAPI proxy 位于 `server/ai/proxy`
- C++ 侧通过 `TraceProxyAi` 调用 `/analyze/trace/{provider}`
- provider 当前支持 `mock` 与 `gemini`
- critical 风险会通过 `WebhookNotifier` 发送通知；本地开发可以自动拉起 mock webhook 服务

## 仓库结构

- `client/`：Vue 3 + Vite 前端
- `server/`：C++ 后端
- `server/core/`：聚合、调度、批处理核心逻辑
- `server/http/`：HTTP 解析与路由
- `server/persistence/`：SQLite 仓储与 Trace 双缓冲写入器
- `server/ai/`：C++ AI 接口与 Python proxy
- `server/handlers/`：日志、配置、历史、Dashboard 相关 handler
- `server/tests/`：单元测试、集成测试、smoke、wrk 脚本
- `docs/`：外部资料、todo、开发记录

## 构建与运行

### 1. 构建后端

```bash
cd server
cmake -B build -S .
cmake --build build
```

### 2. 运行后端

最小启动：

```bash
./server/build/LogSentinel --db /tmp/logsentinel.db --port 8080
```

带本地开发依赖一起启动（AI proxy + webhook mock）：

```bash
./server/build/LogSentinel --db /tmp/logsentinel.db --port 8080 --auto-start-deps
```

常用 Trace 相关参数：

- `--worker-threads`
- `--worker-queue-size`
- `--trace-sweep-interval-ms`
- `--trace-idle-timeout-ms`
- `--trace-capacity`
- `--trace-token-limit`
- `--trace-max-dispatch-per-tick`
- `--trace-buffered-span-limit`
- `--trace-active-session-limit`
- `--trace-ai-provider mock|gemini`
- `--trace-ai-base-url http://127.0.0.1:8001`

### 3. 单独启动 AI proxy

```bash
cd server/ai
pip install -r requirements.txt
cd proxy
python main.py
```

### 4. 启动前端

```bash
cd client
npm install
npm run dev
```

Vite 默认通过 `/api` 代理到 `localhost:8080`。

## 一个最小 Trace 请求示例

```bash
curl -X POST http://127.0.0.1:8080/logs/spans \
  -H 'Content-Type: application/json' \
  -d '{
    "trace_key": 1001,
    "span_id": 1,
    "start_time_ms": 1741680000000,
    "name": "GET /api/order",
    "service_name": "order-service",
    "trace_end": true
  }'
```

典型返回：

- `202`：已接收；如果下游暂时拥堵，响应体里可能带 `deferred=true`
- `503 + Retry-After`：入口触发分级背压，建议稍后重试

## 当前接口概览

### Trace 主链路

- `POST /logs/spans`：接收 Span 并进入 Trace 聚合链路

### 旧日志批处理链路

- `POST /logs`：旧日志批处理入口
- `GET /results/{trace_id}`：旧链路分析结果查询

### 页面与配置接口

- `GET /dashboard`
- `GET /history?page=<n>&pageSize=<n>&level=<level>&search=<keyword>`
- `GET /settings/all`
- `POST /settings/config`
- `POST /settings/prompts`
- `POST /settings/channels`

## 测试与验证

### C++ 单元测试

```bash
cd server/build
ctest
```

### 基础 Trace 冒烟

```bash
python server/tests/smoke_trace_spans.py --mode basic
```

### 增强 Trace 冒烟

```bash
python server/tests/smoke_trace_spans.py --mode advanced
```

### wrk 压测

```bash
server/tests/wrk/run_bench.sh end
```

也可以直接查看：

- `server/tests/wrk/trace_model.lua`
- `server/tests/wrk/run_flamegraph.sh`

## CI 现状

- `CI Unit`：核心单元测试
- `CI Smoke`：basic smoke 常规运行；advanced smoke 更适合手动触发
- `CI Integration`：TraceSessionManager integration workflow，当前偏手动触发

## 进一步阅读

- `FutureMap.md`：后续演进方向
- `docs/KnownIssues.md`：当前已知问题
- `docs/README.md`：外部资料索引

如果你是第一次看这个仓库，建议先从 `server/src/main.cpp`、`server/core/TraceSessionManager.*`、`server/persistence/BufferedTraceRepository.*` 这三处开始看，能最快看清当前主链路。
