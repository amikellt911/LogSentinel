# AI Proxy 开发笔记 (Python)

本文档记录了 `server/ai/proxy` 模块的开发细节、修改内容以及针对新手的建议。

## 1. 修改记录

### 1.1 支持动态配置 (Dynamic Configuration)
为了配合 C++ 后端的 Map-Reduce 逻辑（支持每个 Batch 使用不同的 Prompt 或 Model），Python Proxy 进行了以下更新：
- **Schema 更新**: `BatchRequestSchema` 和 `SummarizeRequest` 现在接收可选的 `api_key`, `model`, 和 `prompt` 参数。
- **Provider 更新**: `AIProvider` 接口及其实现类 (`GeminiProvider`, `MockProvider`) 现在接受这些动态参数。
- **Gemini 客户端优化**:
  - 当请求携带的 `api_key` 与当前缓存的 Key 不一致时，Proxy 会自动更新内部的 `default_client`，而不是每次请求都创建临时对象。这确保了如果用户在设置中切换了 Key，Proxy 能高效地切换上下文。

### 1.2 风险等级标准化 (Risk Level Standardization)
为了统一前端展示和后端逻辑，风险等级已更新为：
- `critical`: 系统崩溃、数据丢失、严重故障。
- `error`: 功能异常、非致命错误。
- `warning`: 警告信息。
- `info`: 普通信息。
- `safe`: 确认安全的操作。
- `unknown`: 无法识别。

`MockProvider` 已同步更新，不再返回旧版的 `high`/`medium`/`low`，而是返回上述标准等级。

### 1.3 启动策略变更：懒加载 (Lazy Initialization)
**原设计**: 如果环境变量中没有 API Key，`main.py` 会直接禁用 Gemini Provider。
**问题**: 这导致了“死锁”。如果用户初次部署没有配置 Key，Proxy 启动后 Gemini 功能不可用，即使后续通过前端（C++）传来了 Key，Proxy 也不会响应（因为它根本没加载该模块）。
**新设计**:
- `main.py` 无条件加载 `GeminiProvider`。
- `GeminiProvider.__init__` 允许空 Key 启动（此时 `default_client` 为 `None`）。
- 只有当第一个携带有效 Key 的请求到达时，才真正初始化 `genai.Client`。
- 如果在未配置 Key 的情况下强行调用（如单次分析接口），会返回明确的 JSON 错误信息。

### 1.4 Schema 严格校验 (Strict Schema Validation)
**变更**: `LogAnalysisResult.risk_level` 字段类型从 `str` 改回了 `RiskLevel` 枚举。
**移除**: 删除了 `RiskLevel` 中遗留的 `HIGH`, `MEDIUM`, `LOW` 成员。
**目的**: 强制 AI 返回符合标准协议的小写字符串。如果 LLM 返回了 "High"（大写）或非标准值，Pydantic 会直接抛出校验错误。这是一种 "Fail Fast" 策略，要求 Prompt 必须足够清晰，或 LLM 足够智能。

## 2. 新手指南 & 踩坑记录

### 2.1 Google GenAI Client 的复用
**知识点**: `google.genai.Client` 建立连接需要开销。
**踩坑**: 之前每次收到带 Key 的请求都创建一个 `new Client()`，这在高并发下是不可接受的。
**解决**: 实现了 `_get_client_and_model` 辅助函数。只有当传入的 Key 真的发生变化时，才重新初始化 Client。

### 2.2 Pydantic Schema 校验
**知识点**: FastAPI 使用 Pydantic 进行请求体验证。
**注意**: 现在 `LogAnalysisResult` 启用了严格枚举校验。这意味着 Prompt 必须明确指示 AI 输出小写的 `critical`, `error`, `warning` 等。如果 AI 输出了 "Critical" (首字母大写)，校验会失败。这是一个权衡：为了类型安全，牺牲了一点容错性。

### 2.3 C++ 与 Python 的 JSON 交互
**踩坑**: C++ 的 `nlohmann::json` 库在序列化时非常严格。如果 Python 端返回的 JSON 缺少字段（如 `summary`），C++ 端可能会抛出异常。
**解决**: 在 `GeminiProvider` 中添加了 `try-except` 块，如果 API 调用失败，手动构造一个包含错误信息的 JSON 字符串返回，保证 C++ 端能收到合法的 JSON 结构，而不是 HTTP 500 崩溃。

### 2.4 调试技巧
- 运行 `python3 server/ai/proxy/main.py` 可以独立启动 Proxy。
- 使用 `curl` 模拟 C++ 发送的 Batch 请求来测试 Prompt 效果。
- 环境变量 `GEMINI_API_KEY` 仅作为默认值，请求中的 `api_key` 优先级更高。
