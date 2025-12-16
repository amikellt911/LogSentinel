# 设置功能设计与交互逻辑 (Settings Design)

本文档详细描述了系统配置管理的数据库设计、前后端交互流程以及状态同步策略。

## 1. 核心设计理念

采用 **"全量加载，增量更新 (Load All, Update Incremental)"** 的策略。

*   **读取:** 只有在 **系统启动 (Startup)** 或 **刷新页面** 时，前端才会发起一次 `GET` 请求，拉取所有设置（基础配置、Prompt、通知渠道）。
*   **写入:** 当用户修改某一项配置时，前端会先更新本地状态 (Optimistic Update)，然后立即调用对应的特定 `POST` 接口持久化到数据库。

---

## 2. 数据库设计 (Schema)

基于 SQLite，将配置分为三类数据表：

```sql
-- 1. 通用配置表 (Key-Value 存储)
-- 用于存储 AI Key, 语言, 线程数等单值配置
CREATE TABLE IF NOT EXISTS app_config (
    config_key TEXT PRIMARY KEY,
    config_value TEXT,
    description TEXT,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 2. AI 提示词表 (多行记录)
-- 用于存储不同的 Prompt 模板
CREATE TABLE IF NOT EXISTS ai_prompts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    content TEXT NOT NULL,
    is_active INTEGER DEFAULT 1,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 3. 报警通知渠道表 (多行记录)
-- 用于存储 Webhook URL 及其配置
CREATE TABLE IF NOT EXISTS alert_channels (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    provider TEXT NOT NULL,         -- e.g., 'DingTalk', 'Slack'
    webhook_url TEXT NOT NULL,
    alert_threshold TEXT NOT NULL,  -- e.g., 'high', 'medium'
    msg_template TEXT,
    is_active INTEGER DEFAULT 0,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 初始化默认配置数据
INSERT OR IGNORE INTO app_config (config_key, config_value, description) VALUES
('ai_provider', 'openai', 'AI服务商类型'),
('ai_model', 'gpt-4-turbo', '模型名称'),
('ai_api_key', '', 'API密钥'),
('ai_language', 'English', '解析语言'),
('kernel_adaptive_mode', '1', '自适应微批模式开关 1/0'),
('kernel_max_batch', '50', '自适应最大阈值'),
('kernel_refresh_interval', '200', '刷新间隔ms'),
('kernel_worker_threads', '4', '工作线程数'),
('kernel_io_buffer', '256MB', 'IO缓冲区大小');
```

---

## 3. 接口设计 (API Contract)

### 3.1 获取所有配置 (Initialization)
*   **Endpoint:** `GET /api/settings/all`
*   **触发时机:** 浏览器页面加载 (`App.vue` onMounted)。
*   **响应结构:**
    ```json
    {
      "config": {
        "ai_provider": "openai",
        "ai_api_key": "sk-...",
        "kernel_worker_threads": "4"
        // ... 其他 KV 对
      },
      "prompts": [
        { "id": 1, "name": "Security Audit", "content": "...", "is_active": 1 }
      ],
      "channels": [
        { "id": 1, "name": "DingTalk Group", "url": "http://...", "is_active": 0 }
      ]
    }
    ```

### 3.2 更新配置 (Modification)
前端根据修改对象的不同，调用不同的接口。

#### A. 更新通用配置 (Base Config)
*   **Endpoint:** `POST /api/settings/config`
*   **Payload:**
    ```json
    {
      "items": [
        { "key": "ai_api_key", "value": "new-key-123" },
        { "key": "ai_model", "value": "gpt-3.5-turbo" }
      ]
    }
    ```
*   **后端逻辑:** 遍历 items，执行 `UPDATE OR INSERT` 到 `app_config` 表。

#### B. 更新/新增 Prompt
*   **Endpoint:** `POST /api/settings/prompt`
*   **Payload:**
    ```json
    {
      "id": 1,           // 有 ID 为修改，无 ID 为新增
      "name": "New Prompt",
      "content": "Analyze this...",
      "is_active": 1
    }
    ```

#### C. 更新/新增 通知渠道
*   **Endpoint:** `POST /api/settings/channel`
*   **Payload:**
    ```json
    {
      "id": 2,
      "name": "Slack Alert",
      "webhook_url": "...",
      "is_active": 1
    }
    ```

---

## 4. 前端交互策略 (Frontend Logic)

### 4.1 状态管理 (Pinia Store)
在 `client/src/stores/system.ts` 中维护与后端完全一致的数据结构：

```typescript
state: () => ({
    // 映射 app_config 表
    config: {
        ai_provider: '',
        ai_api_key: '',
        // ...
    },
    // 映射 ai_prompts 表
    prompts: [] as Prompt[],
    // 映射 alert_channels 表
    channels: [] as Channel[]
})
```

### 4.2 乐观更新 (Optimistic UI)
为了让界面感觉“极快”，我们不在 UI 上显示 Loading 转圈等待保存结果。

**操作流程:**
1.  **用户操作:** 用户在输入框修改 API Key，失去焦点 (Blur)。
2.  **更新 Store:** 前端立即修改 Pinia 中的 `config.ai_api_key`。
3.  **发送请求:** 后台异步发送 `POST /api/settings/config`。
4.  **错误处理:**
    *   如果请求成功 (200 OK): 静默成功，可能弹出轻提示 "Saved"。
    *   如果请求失败 (500/Network Error): 弹出错误提示，并**回滚** Pinia 中的值为修改前的值。

### 4.3 必须重启的提示
对于 `app_config` 中属于 **冷启动配置** 的项（如 `kernel_worker_threads`）：
1.  前端 Store 中会标记哪些 Key 是需要重启的 (`REQUIRE_RESTART_KEYS = ['kernel_worker_threads', ...]`)。
2.  当检测到这些 Key 的值发生变更并保存成功后，前端主动弹出全局提示：
    > "配置已保存，但检测到核心参数变更。为了确保系统稳定，请**重启服务**以生效。"
3.  提供一个 "立即重启" 按钮 (调用 `/api/restart`)。
