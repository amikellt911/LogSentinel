# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LogSentinel is a high-performance log analysis service combining C++ backend architecture with Vue.js frontend and AI-powered analysis. The system processes logs in real-time using a Reactor pattern network layer, performs root cause analysis via LLMs, and provides a web-based monitoring dashboard.

## Build and Development Commands

### Frontend (Vue.js)
```bash
cd client
npm run dev          # Development server (proxies /api to localhost:8080)
npm run build        # Production build
```

### Backend (C++)
```bash
cd server
cmake -B build -S .          # Configure build
cmake --build build          # Build the project
./build/LogSentinel          # Run server (default: port 8080, db: LogSentinel.db)
./build/LogSentinel --db <path> --port <port>  # Custom config
```

### Python AI Proxy Service
```bash
cd server/ai
pip install -r requirements.txt
cd proxy && python main.py   # Runs on 127.0.0.1:8001
```

### Testing
```bash
cd server/build
ctest                         # Run all tests
./test_http_context           # Run single test
./test_logbatcher            # Run specific module test
```

## Architecture Overview

### Module Structure (C++ Backend)

The backend follows a modular architecture with clear separation of concerns:

- **http_module**: HTTP request/response handling (HttpRequest, HttpResponse, HttpContext, HttpServer, Router)
- **core_module**: Log batching and analysis logic (LogBatcher, AnalysisTask)
- **persistence_module**: Database operations (SqliteLogRepository, SqliteConfigRepository)
- **ai_module**: AI provider abstraction (AiProvider interface, GeminiApiAi, MockAI)
- **threadpool_module**: Asynchronous task processing
- **handler_module**: Request routing and handling (LogHandler, DashboardHandler, HistoryHandler, ConfigHandler)
- **notification_module**: Webhook alerts (WebhookNotifier)
- **util_module**: Utilities (TraceIdGenerator)

### Key Design Patterns

1. **Reactor Pattern**: MiniMuduo framework provides non-blocking I/O with epoll. Single I/O thread handles network events, worker threads handle business logic.

2. **Adaptive Micro-batching**: `LogBatcher` uses a ring buffer with dual triggers:
   - Capacity-based: dispatch when batch reaches `batch_size_` (default: 100)
   - Time-based: dispatch on timeout (via EventLoop timer)
   - Flow control: stops dispatching when thread pool utilization exceeds `pool_threshold_` (90%)

3. **Map-Reduce Batch Processing**:
   - Map phase: `analyzeBatch()` processes multiple logs in parallel
   - Reduce phase: `summarize()` aggregates results into a single summary

4. **AI Provider Pattern**: Abstract `AiProvider` interface enables switching between Gemini, Mock, and future providers. Configuration (API key, model) is passed at call time.

5. **Router Pattern**: HTTP routing via `Router` class with method-based dispatch. Routes registered via `router->add("METHOD", "/path", handler)`.

### Data Flow

1. Log submission: `POST /logs` → LogHandler → LogBatcher (ring buffer)
2. Batch dispatch: LogBatcher → ThreadPool → AnalysisTask → AiProvider → Python proxy service
3. Result storage: SqliteLogRepository saves to `logs_raw` and `analysis_results` tables
4. Result retrieval: `GET /results/{trace_id}` → returns analysis or 404 if pending

### Thread Model

- **I/O threads**: 1 (configurable in main.cpp) - handles network I/O via epoll
- **Worker threads**: `CPU cores - 1` - processes business logic from thread pool
- **Thread-local**: SQLite connections (planned optimization for `SQLITE_OPEN_NOMUTEX`)

### Database Schema (SQLite3)

- `logs_raw`: Raw log entries with trace_id, received_at, message
- `analysis_results`: AI analysis outcomes with risk_level, summary, root_cause, solution
- `system_config`: Key-value configuration (api_key, model, prompt, webhook_urls)
- `prompts`: Custom AI prompt templates
- `webhook_channels`: Notification configuration

### Python AI Proxy Architecture

The Python service (`server/ai/proxy/main.py`) acts as an AI provider abstraction layer:
- **Providers**: `base.py` (AIProvider interface), `gemini.py`, `mock.py`
- **Endpoints**: `/analyze/{provider}`, `/analyze/batch/{provider}`, `/summarize/{provider}`, `/chat/{provider}`
- **Purpose**: Unified API for different AI vendors, supports runtime configuration (api_key, model)

### Frontend Architecture (Vue 3)

- **Views**: Dashboard, LiveLogs, History, BatchInsights, Benchmark, Settings
- **State Management**: Pinia store (client/src/stores/system.ts)
- **Routing**: Vue Router with MainLayout wrapper
- **i18n**: EN/ZH support via vue-i18n (client/src/i18n.ts)
- **API Proxy**: Vite dev server proxies `/api` to backend `localhost:8080`

## Important Implementation Details

### Trace ID Generation
Used to correlate log submissions with analysis results. Generated via `TraceIdGenerator` (SHA-256 hash based). Plans to optimize length for readability.

### Error Handling
- `SQLITE_BUSY`: Needs retry logic with backoff (current TODO)
- Webhook failures: Logged but don't block main flow
- AI provider failures: Circuit breaker pattern planned

### Configuration Management
Runtime configuration via SqliteConfigRepository:
- API keys and models stored in database
- Changes applied via `/settings/*` endpoints
- Server restart via `POST /restart` to reload config

### Internationalization
Client supports EN/ZH. Locale affects UI text only; backend responses are JSON.

## Optimization Roadmap (Key Points)

- **gRPC**: Replace REST for C++-Python communication
- **Thread-local SQLite**: Enable `SQLITE_OPEN_NOMUTEX` for concurrent writes
- **Length-based bucketing**: Prevent head-of-line blocking for long log analysis
- **Log template extraction**: Pre-process logs to cache known patterns before AI analysis
- **string_view adoption**: Zero-copy HTTP parsing (C++17 modernization)

## Testing Strategy

- Unit tests in `server/tests/` using Google Test
- Test executables: `test_http_context`, `test_logbatcher`, `test_sqlite`, `test_config_repo`
- CTest integration for VSCode C++ extension
- Python test scripts for API validation
- Performance testing with `wrk` (use separate machine for accurate results)

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
