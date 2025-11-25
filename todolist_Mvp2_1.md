# LogSentinel - MVP2.1 详细实施计划 (AI 优化与告警)

## 阶段目标
本阶段 (MVP2.1) 的核心目标是**优化现有 AI 集成**并**实现主动告警**。
用户已集成了 Gemini (通过 Python Proxy)，我们需要对其进行“提质增效”，使其输出结构化数据，并据此触发 Webhook 告警。

---

## 1. AI 输出结构化优化 (Structured Output)
目前的 AI 返回的是纯文本分析，无法被程序自动处理（如判断风险等级）。我们需要通过 Prompt Engineering 让 AI 返回 JSON。

- [ ] **优化 Python Proxy Prompt (`ai/proxy/main.py`)**
    - [x] 修改 `default_prompt`，强制要求返回 JSON 格式。
    - [ ] **目标格式**：
        ```json
        {
            "summary": "简短的错误摘要",
            "risk_level": "high" | "medium" | "low",
            "root_cause": "根本原因分析",
            "solution": "建议解决方案"
        }
        ```
    - [ ] 增加 System Instruction 强调不要输出 Markdown 代码块标记 (```json ... ```)。

- [ ] **增强 C++ 客户端解析 (`ai/GeminiApiAi.cpp`)**
    - [ ] 在 `analyze` 方法中，获取到 Proxy 返回的 `analysis` 字符串后，尝试将其解析为 `nlohmann::json` 对象。
    - [x] **接口调整**：虽然 `IAnalyzer` 返回 `string`，但我们可以约定返回的是 JSON string。或者，我们可以扩展接口返回一个结构体 `AnalysisResult`。
        - *决策*：为了保持接口简单，暂时仍返回 JSON string，但在业务层进行二次解析。

## 2. 实现 Webhook 告警 (Webhook Alerting)
实现一个通用的 Webhook 通知器，可以对接飞书、钉钉或 Slack。

- [ ] **定义接口 `core/INotifier.h`**
    ```cpp
    class INotifier {
    public:
        virtual ~INotifier() = default;
        virtual void notify(const std::string& title, const std::string& message) = 0;
    };
    ```

- [ ] **实现 `util/WebhookNotifier`**
    - [ ] 继承 `INotifier`。
    - [ ] **配置**：接收 Webhook URL (支持飞书/钉钉格式)。
    - [ ] **实现**：构建包含告警信息的 JSON payload，发送 POST 请求 (使用 `cpr` 库)。
    - [ ] **测试**：使用 [Webhook.site](https://webhook.site) 进行接收测试。

## 3. 业务逻辑串联 (Integration)
将“结构化分析”与“告警”结合起来。

- [ ] **改造 `src/main.cpp`**
    - [ ] **配置读取**：读取 `WEBHOOK_URL`。
    - [ ] **实例化**：创建 `WebhookNotifier` 实例。
    - [ ] **流程更新**：
        1. `GeminiApiAi::analyze(log)` 返回 JSON 字符串。
        2. 解析 JSON，提取 `risk_level`。
        3. **判断逻辑**：
            ```cpp
            if (json["risk_level"] == "high") {
                notifier->notify("High Risk Log Detected", json["summary"]);
            }
            ```
        4. 将结构化结果存入 SQLite (可能需要更新 `SqliteLogRepository` 以存储 JSON 字段，或者直接存为文本)。

## 4. 健壮性优化 (Resilience)
- [ ] **GeminiApiAi 重试机制**
    - [ ] 在 `cpr::Post` 失败或返回非 200 时，增加简单的重试循环 (Retry Loop, max_retries=3)。

---

## 预计产出物
- 修改后的 `ai/proxy/main.py`
- 新增 `core/INotifier.h`
- 新增 `util/WebhookNotifier.cpp/h`
- 更新后的 `src/main.cpp`
