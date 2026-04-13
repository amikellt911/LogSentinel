# Single Entry Deployment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让后端直接托管 `client/dist`，把前端页面、静态资源和现有 `/api/*` 请求都收口到同一个 `http_port`

**Architecture:** 在后端新增一个最小 `FrontendAssetHandler`，负责真实静态文件直出、前端页面白名单 fallback 到 `index.html`、以及路径安全校验。`main.cpp` 入口层先兼容 `/api/*` 前缀和现有裸 API，再把非 API 请求交给 `FrontendAssetHandler`，其余统一 404。

**Tech Stack:** C++17、`std::filesystem`、`std::ifstream`、现有 `MiniMuduo` HTTP 封装、GoogleTest、Python 黑盒脚本

---

### Task 1: Lock Frontend Asset Handler Semantics

**Files:**
- Create: `server/handlers/FrontendAssetHandler.h`
- Create: `server/handlers/FrontendAssetHandler.cpp`
- Create: `server/tests/FrontendAssetHandler_test.cpp`
- Modify: `server/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

补 4 条最小语义：
- 命中真实文件时返回文件内容和正确 `Content-Type`
- 命中前端页面白名单时回 `index.html`
- 未知路径不命中白名单时返回“不处理”
- `..` 路径穿越必须被挡住

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build server/build --target test_frontend_asset_handler && ./server/build/test_frontend_asset_handler`

Expected: FAIL，因为 `FrontendAssetHandler` 还不存在

- [ ] **Step 3: Write minimal implementation**

实现 `FrontendAssetHandler`：
- 保存 `dist_root`
- 保存前端页面白名单
- 后缀到 MIME 的最小映射
- `Handle()` 只负责：
  - 静态文件命中 -> 读文件、写 body、设 `Content-Type`
  - 页面白名单 -> 回 `index.html`
  - 其他 -> 返回 `false`

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build server/build --target test_frontend_asset_handler && ./server/build/test_frontend_asset_handler`

Expected: PASS

### Task 2: Wire Main Request Dispatch

**Files:**
- Modify: `server/src/main.cpp`
- Modify: `server/CMakeLists.txt`

- [ ] **Step 1: Write the failing integration test or black-box expectation**

先不新建复杂 C++ 集成测试，直接把后面黑盒需求写死：
- `/api/settings/all` 继续返回 JSON
- `/settings` 走前端页面 fallback
- `/fdasxz` 返回 404

- [ ] **Step 2: Implement minimal request routing**

在 `main.cpp` 入口层按顺序分流：
- `/api/*`：复制一份 `HttpRequest`，去掉 `/api` 前缀，再交给现有 `Router`
- 裸 API：继续交给现有 `Router`
- 非 API：交给 `FrontendAssetHandler`
- 仍未命中：404

同时补一个可选 CLI 参数：
- `--frontend-dist <path>`

- [ ] **Step 3: Run minimal build**

Run: `cmake --build server/build --target LogSentinel`

Expected: PASS

### Task 3: Add Single Entry Black-box Coverage

**Files:**
- Modify: `server/tests/smoke_settings_blackbox.py`

- [ ] **Step 1: Write the failing black-box assertions**

补 5 条：
- `GET /` 返回 HTML
- `GET /settings` 返回 HTML
- `GET /fdasxz` 返回 404
- `GET /assets/*.js` 返回 JS 且 `Content-Type` 正确
- `GET /api/settings/all` 返回 JSON

- [ ] **Step 2: Run black-box to verify it fails**

Run: `python3 server/tests/smoke_settings_blackbox.py`

Expected: FAIL，在静态文件托管或前端页面 fallback 上红灯

- [ ] **Step 3: Finish minimal implementation and asset setup**

黑盒里给后端传一个临时 `--frontend-dist` 目录，里面放：
- `index.html`
- `assets/app.js`
- `assets/app.css`

这样黑盒不依赖本地是否提前跑过 `npm run build`

- [ ] **Step 4: Run black-box to verify it passes**

Run: `python3 server/tests/smoke_settings_blackbox.py`

Expected: PASS

### Task 4: Final Verification And Docs

**Files:**
- Modify: `CurrentTask.md`
- Modify: `docs/todo-list/Todo_Settings_MVP5.md`
- Modify: `docs/dev-log/20260413-feat-ai-retry.md` or same-day deployment dev-log append file

- [ ] **Step 1: Run verification**

Run:
- `cmake --build server/build --target LogSentinel test_frontend_asset_handler`
- `./server/build/test_frontend_asset_handler`
- `python3 server/tests/smoke_settings_blackbox.py`
- `git diff --check`

Expected: PASS

- [ ] **Step 2: Update task/docs**

把单入口部署进度写回：
- `CurrentTask.md`
- `Todo_Settings_MVP5.md`
- 同日 dev-log
