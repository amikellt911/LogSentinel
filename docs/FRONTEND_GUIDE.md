# 前端架构与交互说明书

本文档旨在帮助开发者理解 `LogSentinel` 的前端架构、数据流向以及如何修改和扩展功能。

## 1. 架构概览

本项目采用了 **前后端分离** 的架构：

*   **前端 (Client):** Vue 3 + TypeScript + Pinia + Element Plus。负责界面展示、用户交互、数据模拟（Simulation Mode）。
*   **后端 (Server):** C++ (MiniMuduo) + SQLite。负责日志接收、持久化、AI 分析和数据统计。
*   **通信协议:** HTTP (REST API)。目前采用 **短轮询 (Short Polling)** 机制来模拟实时监控效果。

### 交互图解

```mermaid
graph TD
    User[用户/浏览器] -- 操作/查看 --> VueApp[Vue前端应用]

    subgraph Client [前端 (浏览器)]
        VueApp -- 读取状态 --> PiniaStore[System Store (State)]
        PiniaStore -- 1. 定时轮询 (setInterval) --> FetchAPI[Fetch API]
        FetchAPI -- 2. 发送 HTTP GET --> BackendAPI
    end

    subgraph Server [后端 (C++ Server)]
        BackendAPI[API Handlers] -- 查询 --> SQLite[SQLite 数据库]
        BackendAPI -- 调用 --> AI[AI 分析模块]
        SQLite -- 返回数据 --> BackendAPI
    end

    BackendAPI -- 3. 返回 JSON --> FetchAPI
    FetchAPI -- 4. 更新数据 --> PiniaStore
```

---

## 2. 关键文件目录说明

`client/src/` 下的核心文件结构及其作用：

| 路径 | 说明 | 重要性 |
| :--- | :--- | :--- |
| **`stores/system.ts`** | **最核心文件**。包含所有状态管理（日志、图表数据、设置）、后端 API 调用逻辑 (`fetchDashboardStats`) 以及模拟数据逻辑 (`updateMockData`)。 | ⭐⭐⭐⭐⭐ |
| `App.vue` | 根组件，负责整体布局框架。 | ⭐⭐⭐ |
| `main.ts` | 入口文件，初始化 Vue、Pinia 和 Element Plus。 | ⭐⭐ |
| `views/` | 页面视图文件夹（如 Dashboard, Settings 等）。 | ⭐⭐⭐ |
| `components/` | 可复用的 UI 组件（如 Charts, LogList）。 | ⭐⭐ |

---

## 3. 核心机制详解

### 3.1 数据的“实时”更新机制
目前的“实时”监控并不是真正的 WebSocket 长连接，而是通过 **轮询 (Polling)** 实现的。

*   **实现位置:** `stores/system.ts` 中的 `startPolling()` 函数。
*   **逻辑:**
    1. 前端启动后，设置一个定时器 (`setInterval`)，每隔 **1000ms (1秒)** 执行一次。
    2. 定时器触发 `fetchDashboardStats()` 和 `fetchLogs()`。
    3. 如果后端返回成功，更新 `chartData` 和 `logs`。
    4. 如果后端连接失败（或开启了 Simulation Mode），则生成随机的模拟数据。

### 3.2 模拟模式 (Simulation Mode)
为了方便演示和开发，前端内置了一套完整的模拟数据生成器。

*   **控制开关:** `isSimulationMode` (在 `stores/system.ts` 中)。
*   **工作原理:** 当 API 请求失败时，或者手动开启该模式时，`updateMockData()` 会被调用，它会利用数学随机算法生成看起来很真实的 QPS 波动、内存变化和虚假日志。

### 3.3 设置 (Settings) 的现状
目前，前端有一个 `settings` 对象（包含 AI Key, Webhook URL 等）。
*   **注意:** 当前这些设置 **仅存在于前端内存中**。
*   **问题:** 刷新浏览器后，设置会重置。
*   **解决方向:** 需要对接后端的 `/settings` 接口（需开发）来实现持久化保存。

---

## 4. 接口契约 (API Contract)

前端期望后端提供的核心接口（详见 `docs/API_REQUIREMENTS.md`）：

1.  **`GET /api/dashboard`**: 获取仪表盘统计数据（QPS, 风险计数, 内存等）。
2.  **`GET /api/history`**: 获取历史日志列表。
3.  **`POST /api/logs`**: (由外部系统调用) 提交日志。

---

## 5. 开发指南：如何添加新功能？

### 场景：我想在仪表盘上新增一个“CPU 温度”的图表

1.  **后端 (Server):**
    *   修改 `DashboardStats` 结构体，增加 `cpu_temp` 字段。
    *   在 `DashboardHandler.cpp` 中获取系统温度并填充该字段。
2.  **前端 (Store):**
    *   打开 `client/src/stores/system.ts`。
    *   在 `DashboardStatsResponse` 接口中增加 `cpu_temp: number`。
    *   在 `state` 中增加 `cpuTemp` 变量。
    *   在 `fetchDashboardStats` 函数中，将 `data.cpu_temp` 赋值给 `cpuTemp`。
3.  **前端 (UI):**
    *   在 `DashboardView.vue` 中添加一个图表组件，绑定 `useSystemStore().cpuTemp` 数据。

---

## 6. 构建与部署

*   **开发模式:** `cd client && npm run dev` (通过 Vite 代理连接后端)。
*   **生产构建:** `cd client && npm run build` (生成 `dist/` 静态文件)。
*   **集成:** 将 `dist/` 文件夹部署在 C++ Server 的静态文件目录下（需配置 Server 支持静态文件服务），或者使用 Nginx/Python 作为反向代理服务器。
