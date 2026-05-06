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
- [ ] 验证 `/analyze/trace/mock` 在新调用方式下能正常返回（本次不拿 gemini 做并发验证）
- [x] 补一个最小并发验证脚本，固定并发打 `/analyze/trace/mock`，通过总墙钟时间判断 mock 是否仍是单车道
- [ ] 实跑并发验证脚本，确认 mock 不再因 `time.sleep` 把整个 proxy 单车道堵死
- [x] 追加同日 dev-log，记录“为什么先走线程池桥接，而不是 provider 全 async 化”

## DeepSeek Provider 接入

- [x] 刷新任务边界：确认当前只追加 Trace AI provider，不改 Trace 聚合、数据库 schema、线程模型和 provider 热切路由语义
- [x] 先补 Python proxy 单测红灯：DeepSeek 必须走 `https://api.deepseek.com/chat/completions`、Bearer 鉴权、`response_format=json_object`、usage 归一
- [x] 实现 `server/ai/proxy/providers/deepseek.py`，复用 GLM 的 HTTP/JSON 校验策略并补中文注释说明数据流
- [x] 在 `server/ai/proxy/main.py` 注册 `deepseek`，支持 `DEEPSEEK_API_KEY` / `DEEPSEEK_MODEL`
- [x] 在 C++ `TraceAiBackend` 增加 `DeepSeek` 路由识别，保持 provider 仍是冷启动语义
- [x] 在 Settings 页面主 provider/fallback 下拉中加入 DeepSeek，并给出 `deepseek-v4-flash` 默认模型提示
- [x] 补黑盒/协议测试里的 provider map，确认主备 provider 路由能识别 deepseek
- [x] 运行 Python proxy 协议测试、前端类型检查/构建和必要 C++ 测试
- [x] 追加同日 dev-log，记录 DeepSeek 接入范围、测试结果和新手注意事项

### DeepSeek 验证记录

- [x] `python3 server/tests/ai_proxy_trace_protocol_test.py -k deepseek -v`：通过
- [x] `python3 server/tests/ai_proxy_trace_protocol_test.py -v`：14 条通过
- [x] `cmake --build server/build --target test_trace_session_manager_unit`：通过
- [x] `./server/build/test_trace_session_manager_unit`：54 条通过
- [x] `./server/build/test_trace_session_manager_integration`：14 条通过
- [x] `./server/build/test_http_context`：10 条通过
- [x] `python3 -m py_compile server/ai/proxy/providers/deepseek.py server/ai/proxy/main.py`：通过
- [ ] `npm run build`：失败在既有 TypeScript 错误，失败文件包括 `AIEngineSearchBar.vue`、`BatchArchiveList.vue`、`BusinessHealthCards.vue`、`PromptDebugger.vue`、`RiskDistribution.vue`、`TraceWaterfall.vue`、`AIEngine.vue`、`Settings.vue`，不在本次 DeepSeek 修改点
- [ ] `python3 server/tests/smoke_settings_blackbox.py --server-bin ./server/build/LogSentinel --ready-timeout 15 --dispatch-timeout 15`：DeepSeek 主路场景已执行到后续阶段，最终失败在既有 `trace_lifecycle_profile=` 启动日志等待点
