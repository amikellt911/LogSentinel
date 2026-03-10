# Todo_TraceAiProvider

- [x] 实现 Python GeminiProvider 的 `analyze_trace`，补齐 Trace 真分析能力
- [x] 支持 C++ Trace AI 选择 provider（mock/gemini），默认保持 mock 兼容现有流程
- [x] 增加启动参数 `--trace-ai-provider`，用于本地切换真实分析
- [x] 本地编译验证（C++）+ 代理语法验证（Python）
- [x] 拆分 log/trace 默认 prompt，并将 prompt 模板沉淀到 `schemas.py`
- [x] 在 `smoke_trace_spans.py` 新增 `gemini-e2e` 模式，沉淀真实 Trace AI 端到端验证路径
- [x] 设计并记录一个“真实 AI Trace 分析”手工测试场景与执行命令
- [x] 修复 gemini-e2e 等待策略：根据 `trace-ai-timeout-ms` 自动提升 DB/analysis 等待时间，避免误报超时
- [x] 修复 TraceSessionManager AI 异常兜底：AI 超时/协议异常时仍落基础 summary/span（保持单事务写入）
- [x] AI 异常时写入失败态 trace_analysis（`AI_ANALYSIS_FAILED`），保证失败可见并为后续重试预留状态
- [x] 追加同日 dev-log 记录

## Python Proxy 并发改造

- [x] 重新确认 `main.py -> provider.analyze_trace` 当前同步调用链，锁定真正阻塞 event loop 的位置
- [x] 采用框架自带线程池方案，在 `main.py` 路由层把同步 provider 调用移出 event loop
- [x] 保持 `AIProvider` / `GeminiProvider` / `MockProvider` 现有同步接口不变，先不做整套 async 化改造
- [ ] 验证 `/analyze/trace/mock` 与 `/analyze/trace/gemini` 在新调用方式下都能正常返回
- [ ] 补最小验证：并发请求下 mock 不再因 `time.sleep` 把整个 proxy 单车道堵死
- [x] 追加同日 dev-log，记录“为什么先走线程池桥接，而不是 provider 全 async 化”
