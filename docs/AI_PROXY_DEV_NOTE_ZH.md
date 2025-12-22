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

## 2. 新手指南 & 踩坑记录

### 2.1 Google GenAI Client 的复用
**知识点**: `google.genai.Client` 建立连接需要开销。
**踩坑**: 之前每次收到带 Key 的请求都创建一个 `new Client()`，这在高并发下是不可接受的。
**解决**: 实现了 `_get_client_and_model` 辅助函数。只有当传入的 Key 真的发生变化时，才重新初始化 Client。

### 2.2 Pydantic Schema 校验
**知识点**: FastAPI 使用 Pydantic 进行请求体验证。
**注意**: `LogAnalysisResult` 中的 `risk_level` 字段类型被设为 `str` 而不是 `Enum`。
**原因**: 大模型输出有时不可控（例如输出 "Critical" 或 "CRITICAL"），如果强制 Enum 校验失败会导致整个 Batch 失败。在应用层（Pydantic 验证后）再做清洗或归一化更安全。

### 2.3 C++ 与 Python 的 JSON 交互
**踩坑**: C++ 的 `nlohmann::json` 库在序列化时非常严格。如果 Python 端返回的 JSON 缺少字段（如 `summary`），C++ 端可能会抛出异常。
**解决**: 在 `GeminiProvider` 中添加了 `try-except` 块，如果 API 调用失败，手动构造一个包含错误信息的 JSON 字符串返回，保证 C++ 端能收到合法的 JSON 结构，而不是 HTTP 500 崩溃。

### 2.4 调试技巧
- 运行 `python3 server/ai/proxy/main.py` 可以独立启动 Proxy。
- 使用 `curl` 模拟 C++ 发送的 Batch 请求来测试 Prompt 效果。
- 环境变量 `GEMINI_API_KEY` 仅作为默认值，请求中的 `api_key` 优先级更高。
