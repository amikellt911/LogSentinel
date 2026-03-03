# Todo: TraceSessionManager 集成测试

- [x] 明确最小闭环测试范围（单 span、多个 span、容量触发）
- [x] 编写 TraceSessionManager 集成测试用例（依赖 MockTraceAi + SQLite）
- [x] 更新 CMakeLists 以编译并注册新测试
- [x] 修正与当前实现不一致的 3 条断言（risk_level、cycle、missing-parent）
- [x] 执行 TraceSessionManagerIntegrationTest 全量用例并记录结果（7/7 passed）
