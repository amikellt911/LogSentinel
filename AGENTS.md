# 仓库指南

## 项目最小上下文

本项目是一个“C++ 高并发日志接入 + Trace 聚合 + 可选 AI 推理（Python proxy）+ 存储/查询/告警”的系统。

## 禁止项与边界

- 未在 `CurrentTask.md` 明确要求时，禁止引入重量级依赖或大规模架构改动。
- 禁止擅自修改接口契约、数据库 schema、线程模型、聚合策略等核心约束。
- 需求不明确时，优先查阅 `CurrentTask.md`、`FutureMap.md`，不要自行扩展需求。

## 项目结构与模块组织

- `client/`: Vue 3 + Vite 前端（视图在 `client/src/views`，组件在 `client/src/components`，路由在 `client/src/router`）。
- `server/`: C++ 后端（模块位于 `server/http`, `server/core`, `server/persistence`, `server/ai`, `server/threadpool`, `server/handlers`, `server/notification`, `server/util`）。
- `server/ai/proxy/`: 用于 LLM 调用的 Python AI 代理服务。
- `server/tests/`: C++ GoogleTest 目标与 Python 集成测试；`server/tests/wrk/` 存放压测脚本。
- `docs/`: 架构说明、计划与开发参考资料。

## 构建、测试与开发命令

- 前端开发服务：`cd client && npm run dev`（Vite，`/api` 代理到 `localhost:8080`）。
- 前端构建：`cd client && npm run build`（先 `vue-tsc` 类型检查，再打包）。
- 后端构建：`cd server && cmake -B build -S . && cmake --build build`。
- 运行后端：`./server/build/LogSentinel` 或 `./server/build/LogSentinel --db <path> --port <port>`。
- AI 代理：`cd server/ai && pip install -r requirements.txt && cd proxy && python main.py`（监听 `127.0.0.1:8001`）。

## 编码风格与命名规范

- 默认使用 C++17，优先 RAII 与智能指针管理所有权。
- 遵循现有模块命名（如 `LogBatcher`、`SqliteLogRepository`、`HttpServer`）与文件结构。
- Vue/TS 使用 SFC，视图放在 `client/src/views`，通用组件放在 `client/src/components`。
- 仓库未统一配置格式化工具，请保持缩进与大括号风格与周边代码一致。

## 测试规范

- C++ 单元测试使用 GoogleTest，并通过 CMake/CTest 接入。
- 运行全部 C++ 测试：`cd server/build && ctest`。
- 运行单个测试二进制：`./test_http_context`（或其他 `test_*` 可执行文件）。
- Python 集成测试位于 `server/tests/*.py`，`python server/tests/run_tests.py` 会启动服务并执行 API 校验。

## 提交与 PR 规范

- 提交历史遵循 Conventional Commits（例如 `feat(frontend): 说明`、`fix(core): 说明`），描述简洁且中文。
- PR 通常需要简短摘要、测试说明，并在适用时附上相关问题或文档链接。
- 若涉及 UI 改动，请在 PR 描述中附截图或简短 GIF。

## 配置与安全提示

- 后端运行时配置存储在 SQLite，测试时可以使用memory作为数据库。
- AI Provider 密钥通过 Python 代理配置或 API 负载传入，避免提交敏感信息。

## 交互准则

### 1. 语言与注释

- **对话回复**：所有对话回复必须使用 **中文**。
- **代码注释**：代码注释必须使用 **中文**，且要详细说明“为什么这么写”。

### 2. 任务工作流

- **TodoList**：开始任何复杂编码任务前，必须先在 `docs/todo-list` 目录创建 `Todo_xx.md`，列出详细步骤，并在完成后打勾确认。
- **TodoList 合并**：同一主题只维护一个 `Todo_xx.md`，后续只追加条目与勾选，不重复新建文件。
- **Pre-check**：修改多个文件前，提醒用户检查 `git status`，确保工作区干净。
- **工作计划**：当前工作请查看根目录 `CurrentTask.md`，未来计划请查看根目录 `FutureMap.md`。
- **问题记录**：已知问题与后续迭代事项请查看 `docs/KnownIssues.md`。

### 3. 任务后复盘

任务完成后，必须在 `docs/dev-log` 目录生成一个 `YYYYMMDD-type-scope.md` 文件（例如 `20251225-feat-logger.md`），内容包括（**非代码变更可跳过 dev-log**）：
- **Dev-log 合并**：新增 dev-log 前先检查 `git status`；若已存在 dev-log，必须追加到同一文件，不得新建重复文件。

- **Git Commit Message**：
  - 必须遵循 **Conventional Commits** 格式（`type(scope): description`）。
  - 描述必须为 **中文**。
  - 示例：`feat(core): 优化日志批处理器的内存占用` 或 `fix(network): 修复 TCP 连接超时未重试的问题`。

- **Modification**：列出修改的文件。

- **Learning Tips**：
  - **Newbie Tips**：C++ 新手容易忽略的知识点。
  - **Function Explanation**：使用到的可能不熟悉的函数或库。
  - **Pitfalls**：常见坑点与规避原因。
