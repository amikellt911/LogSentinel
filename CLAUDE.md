# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LogSentinel is a high-performance log analysis service combining C++ backend architecture with Vue.js frontend and AI-powered analysis. The system processes logs in real-time using a Reactor pattern network layer, performs root cause analysis via LLMs, and provides a web-based monitoring dashboard.

当前已跑通的 Trace 主链路为：`POST /logs/spans` -> `TraceSessionManager` -> `BufferedTraceRepository` -> `SqliteTraceRepository`。旧 `/logs` 批处理分析链已移出主程序主路由，不再作为"功能是否真实生效"的判断依据。

## Build and Development Commands

### Frontend (Vue.js)
```bash
cd client
npm run dev          # Development server (proxies /api to localhost:8080)
npm run build        # Production build (vue-tsc type check + vite build)
npm run preview      # Preview production build
```

### Backend (C++)
```bash
cd server
cmake -B build -S .          # Configure build
cmake --build build          # Build the project
./build/LogSentinel          # Run server (default: port 8080, db: LogSentinel.db)
./build/LogSentinel --db <path> --port <port>  # Custom config
./build/LogSentinel --db /tmp/logsentinel.db --port 8080 --auto-start-deps  # Auto-start AI proxy + webhook mock
```

常用 CLI 参数：
- `--worker-threads`, `--dispatch-worker-threads`, `--worker-queue-size`
- `--trace-sweep-interval-ms`, `--trace-idle-timeout-ms`, `--trace-capacity`, `--trace-token-limit`
- `--trace-ai-provider mock|gemini|glm`, `--trace-ai-base-url http://127.0.0.1:8001`
- `--disable-ai`, `--disable-webhook`, `--disable-buffered-trace-repo`
- `--trace-lifecycle-profile protected|minimal`

### Python AI Proxy Service
```bash
cd server/ai
pip install -r requirements.txt
cd proxy && python main.py   # Runs on 127.0.0.1:8001
```

### Docker Compose
```bash
docker compose up            # Start both server + ai-proxy (server on 8080, proxy on 8001)
docker compose up -d         # Detached mode
```
compose 场景下后端默认走 mock provider，不需要真实 API key 即可跑通双容器主链。

### Testing
```bash
cd server/build
ctest                                                    # Run all C++ tests
./test_http_context                                      # Run single test
./test_logbatcher                                        # Run specific module test
./test_trace_session_manager_unit                        # Trace 主链路单元测试
./test_trace_session_manager_integration                 # Trace 主链路集成测试

python server/tests/smoke_trace_spans.py --mode basic    # 基础 Trace 冒烟
python server/tests/smoke_trace_spans.py --mode advanced # 增强 Trace 冒烟（更适合手动触发）

server/tests/benchmark/suite_a/run_suite_a.sh end        # wrk 压测
```

## Architecture Overview

### Module Structure (C++ Backend)

The backend follows a modular architecture with clear separation of concerns:

- **http_module**: HTTP request/response handling (HttpRequest, HttpResponse, HttpContext, HttpServer, Router)
- **core_module**: Trace 聚合与调度 (TraceSessionManager, LogBatcher, AnalysisTask)
- **persistence_module**: Database operations (SqliteTraceRepository, BufferedTraceRepository, SqliteConfigRepository)
- **ai_module**: AI provider abstraction (AiProvider interface, TraceProxyAi, GeminiApiAi, MockAI)
- **threadpool_module**: Asynchronous task processing
- **handler_module**: Request routing and handling (LogHandler, DashboardHandler, HistoryHandler, ConfigHandler, TraceHandler)
- **notification_module**: Webhook alerts (WebhookNotifier)
- **util_module**: Utilities (TraceIdGenerator)

### Key Design Patterns

1. **Reactor Pattern**: MiniMuduo framework provides non-blocking I/O with epoll. Single I/O thread handles network events, worker threads handle business logic.

2. **Trace 聚合与分级背压**：
   - `TraceSessionManager` 以 `trace_key / span_id / parent_span_id` 维护活跃会话
   - 按 `trace_end`、Span 容量阈值、Token 阈值、idle timeout 触发分发
   - 用时间轮维护 collecting / sealed / retry 会话
   - `trace_end`、容量阈值、Token 阈值命中后不会立刻 dispatch，而是先进入短暂 `sealed grace window`
   - dispatch 成功后把已完成 `trace_key` 放入短暂 `TIME_WAIT tombstone`，拦截晚到 Span
   - 高水位优先拒绝新 Trace，critical 水位才拒绝存量 Trace 的后续 Span
   - 线程池 submit 失败时把会话回滚到内存，并按 `1/2/4/8/16` tick 指数退避重试

3. **AI Provider Pattern**: Abstract `AiProvider` interface enables switching between Gemini, GLM, Mock, and future providers. C++ 侧支持主 provider + fallback provider 两路冷启动构造，主路失败后自动尝试 fallback。熔断采用最小状态机：连续失败达到阈值后进入冷却时间。

4. **Router Pattern**: HTTP routing via `Router` class with method-based dispatch. Routes registered via `router->add("METHOD", "/path", handler)`.

### Data Flow

1. 日志上报：`POST /logs/spans`（必须包含 `trace_key`, `span_id`, `start_time_ms`, `name`, `service_name`）→ TraceHandler → TraceSessionManager
2. 聚合与调度：TraceSessionManager 按 trace_key 维度聚合 span，满足条件后进入 sealed grace window，再 dispatch
3. 持久化：BufferedTraceRepository 通过双缓冲线（primary buffer 写 trace_summary + trace_span，analysis buffer 写 trace_analysis）批量刷入 SQLite（WAL）
4. AI 分析：dispatch 时通过 TraceProxyAi 调用 Python proxy（`/analyze/trace/{provider}`），支持主路 + fallback 自动降级
5. 结果查询：`POST /traces/search` / `GET /traces/{trace_id}`

### Thread Model

- **I/O threads**: 1 (configurable in main.cpp) - handles network I/O via epoll
- **Worker threads**: `CPU cores - 1` - processes business logic from thread pool
- **Dispatch worker threads**: dedicated to trace dispatch tasks
- **Flush threads**: background threads for buffered trace repository batch writes

### Database Schema (SQLite3)

- `trace_summary`: Trace 级别汇总（trace_key, status, duration 等）
- `trace_span`: 单条 Span 明细
- `trace_analysis`: AI 分析结果（risk_level, summary, root_cause, solution）
- `app_config`: Key-value configuration（模型、密钥、`active_prompt_id` 等运行参数）
- `prompts`: 单表存储提示词模板，字段为 `id/name/content/is_active`
- `alert_channels`: Notification configuration（钉钉/Slack 等渠道）

### Python AI Proxy Architecture

The Python service (`server/ai/proxy/main.py`) acts as an AI provider abstraction layer:
- **Providers**: `base.py` (AIProvider interface), `gemini.py`, `glm.py`, `mock.py`
- **Endpoints**: `/analyze/trace/{provider}`, `/analyze/{provider}`, `/analyze/batch/{provider}`, `/summarize/{provider}`, `/chat/{provider}`
- **Purpose**: Unified API for different AI vendors, supports runtime configuration (api_key, model)

### Frontend Architecture (Vue 3)

- **Views**: TraceExplorer, Dashboard, ServiceMonitor, SettingsPrototype, LiveLogs, History, BatchInsights, Benchmark
- **State Management**: Pinia store (client/src/stores/system.ts)
- **Routing**: Vue Router with MainLayout wrapper
- **i18n**: EN/ZH support via vue-i18n (client/src/i18n.ts)
- **API Proxy**: Vite dev server proxies `/api` to backend `localhost:8080`

## Important Implementation Details

### Trace ID Generation
Used to correlate log submissions with analysis results. Generated via `TraceIdGenerator` (SHA-256 hash based).
Each log record should include `trace_key` and `span_id`; the backend relies on trace_key for grouping and span_id for ordering when aggregating spans.

### Error Handling
- `SQLITE_BUSY`: Needs retry logic with backoff (current TODO)
- Webhook failures: Logged but don't block main flow
- AI provider failures: Circuit breaker pattern (consecutive failures -> cooldown period -> `skipped_circuit` status)

### Configuration Management
Runtime configuration via SqliteConfigRepository:
- API keys and models stored in database
- Single prompt table (`prompts`) + `active_prompt_id`，前后端设置页面只维护一份提示词列表
- Changes applied via `/settings/*` endpoints
- 模型/api_key 支持运行时热更新；provider 仍保持冷启动语义

### Internationalization
Client supports EN/ZH. Locale affects UI text only; backend responses are JSON.

## Testing Strategy

- C++ 单元测试使用 GoogleTest，通过 CMake/CTest 接入
- Trace 主链路关键回归入口：`test_trace_session_manager_unit`、`test_trace_session_manager_integration`
- Python 冒烟测试：`smoke_trace_spans.py --mode basic|advanced`
- 注意：当前 trace_end / capacity / token_limit 不再代表"立即 dispatch"，而是先进入 sealed 状态；如果测试不跑 main.cpp 的定时 sweep，就需要手动推进 `SweepExpiredSessions(...)`
- benchmark CLI 开关：`--disable-ai`、`--disable-webhook`、`--disable-buffered-trace-repo`、`--trace-lifecycle-profile`
- benchmark 脚本位于 `server/tests/benchmark/`

## Interaction Guidelines (交互准则)

### 1. Language & Comments (语言与注释)

- **对话回复**: 所有的对话回复必须使用 **中文**。
- **代码注释**: 代码中的注释必须使用 **中文**，且注释要详细，解释清楚"为什么这么写"。

### 2. Task Workflow (任务工作流)

- **TodoList**: 在开始任何复杂编码任务前，必须先在docs/todo-list文件夹（todo-list文件夹已经创建好了）创建一个 `Todo_xx.md`，列出详细步骤，每完成一步打钩确认。
- **Pre-check**: 在修改多个文件前，提醒用户检查 `git status` 确保工作区干净。

### 3. Post-Task Review (任务后复盘)

任务执行完毕后，必须在/docs/dev-log文件夹（dev-log文件夹已经创建好了）中生成一个如下格式：YYYYMMDD-type-scope.md的文件,例如：20251225-feat-logger.md，内容包括：

- **Git Commit Message**:
  - 必须遵循 **Conventional Commits** 标准（`type(scope): description`）。
  - 描述部分必须用 **中文**。
  - 示例: `feat(core): 优化日志批处理器的内存占用` 或 `fix(network): 修复 TCP 连接超时未重试的问题`

- **Modification**: 列出修改了哪些文件。

- **Learning Tips**:
  - **Newbie Tips**: 有什么知识点是 C++ 新手容易忽略的？
  - **Function Explanation**: 用到了哪些可能没见过的函数或库？
  - **Pitfalls**: 刚才的过程中，有哪些坑是开发者容易踩的？为什么这样写可以避免？

### 4. Safety & Standards (安全与规范)

- **C++ 标准**: 默认为 **C++17**。
- **智能指针**: 优先使用智能指针 (Smart Pointers) 和 RAII 管理资源。
- **禁止裸指针**: 严禁使用裸指针 (Raw Pointers) 进行所有权管理。

## Key Entry Points for Newcomers

如果你是第一次看这个仓库，建议从以下文件开始：
- `server/src/main.cpp` — 后端入口，看启动流程和 CLI 参数
- `server/core/TraceSessionManager.*` — Trace 聚合核心逻辑
- `server/persistence/BufferedTraceRepository.*` — 双缓冲持久化
- `client/src/views/TraceExplorer.vue` — 前端主演示页面
