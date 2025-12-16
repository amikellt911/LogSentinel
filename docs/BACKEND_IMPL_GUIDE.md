# 后端接口实现指南 (Backend Implementation Guide)

本文档针对配置管理模块 (`Settings`)，详细阐述 C++ 后端的代码结构、数据模型设计以及 JSON 序列化策略。

## 1. 核心思路

您的想法非常正确：**利用 C++ 结构体 (Struct) 配合 `nlohmann/json` 的宏 (`NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` 或 `INTRUSIVE`) 是最优雅、最高效的方案。**

*   **强类型:** 业务逻辑层操作的是 `AppConfig` 对象，而不是散乱的字符串 Map，编译器能帮我们检查拼写错误。
*   **自动化:** 宏可以自动处理 JSON 字符串与 C++ 对象之间的双向转换，极大减少重复代码。
*   **适配性:** 对于数据库中的 KV 表，我们可以通过中间层转换，使其适配结构体。

---

## 2. 数据结构定义 (C++ Structs)

**重要决策:** 建议将这些结构体定义在 **`server/persistence/ConfigTypes.h`** (新建文件)，而不是 `core/` 下。

*   **设计理由:**
    *   `Core` 模块（如 `LogBatcher`）通常会依赖 `Persistence` 模块（如 `SqliteLogRepository`）。
    *   如果我们将 `ConfigTypes.h` 放在 `Core` 中，而 `Persistence` 又需要引用它来读写数据库，就会形成 `Core <-> Persistence` 的双向循环依赖。
    *   将类型定义下沉到 `Persistence` 模块（或者独立的 `Types` 模块），可以打破这个环，形成清晰的 `Core -> Persistence` 单向依赖。

```cpp
// server/persistence/ConfigTypes.h
#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// 1. 通用配置 (对应 app_config 表)
struct AppConfig {
    // AI 设置
    std::string ai_provider = "openai";
    std::string ai_model = "gpt-4-turbo";
    std::string ai_api_key = "";
    std::string ai_language = "English";

    // 内核设置
    int kernel_worker_threads = 4;
    int kernel_max_batch = 50;
    int kernel_refresh_interval = 200;
    std::string kernel_io_buffer = "256MB";
    bool kernel_adaptive_mode = true;

    // 序列化宏 (注意：字段名需与 JSON key 及 DB config_key 一致)
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AppConfig,
        ai_provider, ai_model, ai_api_key, ai_language,
        kernel_worker_threads, kernel_max_batch, kernel_refresh_interval, kernel_io_buffer, kernel_adaptive_mode
    )
};

// 2. Prompt 配置 (对应 ai_prompts 表)
struct PromptConfig {
    int id = 0; // 0 表示新增
    std::string name;
    std::string content;
    bool is_active = true;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(PromptConfig, id, name, content, is_active)
};

// 3. 通知渠道配置 (对应 alert_channels 表)
struct AlertChannel {
    int id = 0;
    std::string name;
    std::string provider; // "DingTalk", "Slack"
    std::string webhook_url;
    std::string alert_threshold; // "Critical", "Warning"
    std::string msg_template; // 消息模板
    bool is_active = false;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AlertChannel, id, name, provider, webhook_url, alert_threshold, msg_template, is_active)
};

// 4. 聚合大对象 (用于 GET /settings/all)
struct AllSettings {
    AppConfig config;
    std::vector<PromptConfig> prompts;
    std::vector<AlertChannel> channels;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AllSettings, config, prompts, channels)
};
```

---

## 3. 数据库交互与转换逻辑

这是本方案最巧妙的地方：如何把数据库里那张 Key-Value 的 `app_config` 表变成强类型的 `AppConfig` 结构体？

### 3.1 读 (Load from DB)

我们不需要手写 `if (key == "ai_provider") config.ai_provider = val`。我们可以利用 JSON 作为中间媒介。

**实现伪代码 (`SqliteConfigRepository.cpp`):**

```cpp
#include "persistence/ConfigTypes.h"

AppConfig SqliteConfigRepository::getAppConfig() {
    // 1. 从数据库查出所有 KV 对
    std::map<std::string, std::string> kv_map = queryAllConfigs();

    // 2. 构造成 JSON 对象
    nlohmann::json j;
    for (const auto& [key, val] : kv_map) {
        // 注意类型转换：数据库存的都是字符串，但结构体里有 int/bool
        // 这里需要简单的尝试转换逻辑，或者全存字符串在结构体里处理
        // 为了方便，建议 struct 里全用 string，或者在这里做判断
        if (key == "kernel_worker_threads") j[key] = std::stoi(val);
        else if (key == "kernel_adaptive_mode") j[key] = (val == "1");
        else j[key] = val;
    }

    // 3. 自动反序列化为 Struct (利用宏)
    // 如果数据库缺某些字段，结构体通过默认值兜底
    AppConfig config = j.get<AppConfig>();
    return config;
}
```

### 3.2 写 (Save to DB)

对于 `POST` 请求，前端发来的是 JSON。

**Handler 伪代码 (`SettingsHandler.cpp`):**

```cpp
#include "persistence/ConfigTypes.h"

void handleUpdateConfig(const HttpRequest& req, HttpResponse* resp) {
    // 1. 解析请求 JSON 为 Struct
    // 前端可能只发了部分更新，所以我们用 map 接收可能更灵活？
    // 或者前端发来一个 items 数组: [{"key": "...", "value": "..."}]
    // 按照我们设计的 API，前端发的是 Key-Value 数组。

    auto j = nlohmann::json::parse(req.body);

    // 2. 遍历更新
    for (const auto& item : j["items"]) {
        std::string key = item["key"];
        std::string val = item["value"];
        // 存入数据库 (UPSERT)
        repo->updateConfig(key, val);
    }

    resp->setStatusCode(200);
}
```

---

## 4. 接口实现细节

### 4.1 `GET /settings/all`
实现非常简单，聚合三个部分即可：

```cpp
void handleGetAll(const HttpRequest& req, HttpResponse* resp) {
    AllSettings all;

    // 从 DB 加载三个部分
    all.config = repo->getAppConfig();       // 转化逻辑见上文 3.1
    all.prompts = repo->getAllPrompts();     // 简单的 SELECT *
    all.channels = repo->getAllChannels();   // 简单的 SELECT *

    // 转 JSON 字符串
    nlohmann::json j = all;
    resp->setBody(j.dump());
}
```

---

## 5. 总结

*   **优点:** 代码极其整洁，`ConfigTypes.h` 既是文档又是代码，添加新配置只需改这个头文件和数据库初始数据。
*   **架构优势:** 通过将 `ConfigTypes.h` 放置在 `persistence/` 目录，成功避免了 `Core` 与 `Persistence` 之间的循环依赖，保持了单向依赖流 (`Core -> Persistence`)。
