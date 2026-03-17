# CurrentTask Archive

## 阶段信息
阶段：MVP4.0
开始日期：2026-03-10
归档日期：2026-03-17

## 当前阶段
MVP4.0：围绕 Trace 主链路做稳定化收口，接通 SQLite 双缓冲批量写入，修复晚到 Span 导致的重复落库问题，并形成可压测、可回归的后端稳定版本。

## 阶段目标
- [x] 把 `BufferedTraceRepository` 接入 Trace 主调用链，收口 `TraceSessionManager` 持久化依赖
- [x] 修复 `trace_end / capacity / token_limit` 触发后过早分发带来的旧 Trace 复活与重复落库问题
- [x] 补齐主链路运行时埋点、回归测试与 benchmark 验证，形成可复现的稳定检查点

## 本轮范围
要做：
- [x] SQLite Trace 双缓冲主数据 / 分析数据批量写入接入主路径
- [x] `sealed grace window + TIME_WAIT tombstone` 两阶段延迟关闭
- [x] `submit` 失败回滚语义收紧，避免 `retry_later` 状态继续吃新 Span
- [x] 单元测试、集成测试、wrk benchmark、README 与 dev-log 补齐

不做：
- [x] 前后端完整联调收尾
- [x] Dashboard / History / Results 读侧全面切到新 Trace 存储
- [x] AI proxy 并发能力专项优化
- [x] 跨进程 / 跨重启的全局 Trace 去重

## 核心任务
- [x] 用双缓冲把 `trace_summary / trace_span / trace_analysis` 从热路径移到后台批量落库
- [x] 让 `trace_end / capacity / token_limit` 先进入 sealed，而不是立刻 dispatch
- [x] 只在 `thread_pool_->submit(...)` 成功后写入 tombstone，拦截晚到 Span，避免已完成 Trace 被复活
- [x] 修正 benchmark 埋点口径，确认 `primary_flush_fail_count=0` 且不再出现 `UNIQUE constraint failed: trace_summary.trace_id`
- [x] 回归 `TraceSessionManager` unit / integration 测试，确保状态机和落库行为与新时间线一致

## 验收标准
- [x] Trace 主链路可稳定完成接入、聚合、封口、落库、可选 AI 分析
- [x] benchmark 下不再出现 `trace_summary.trace_id` 主键冲突，主数据 flush 失败计数归零
- [x] `test_trace_session_manager_unit` 与 `test_trace_session_manager_integration` 全量通过
- [x] 有明确的文档、日志与压测记录支撑后续联调和简历表述

## 备注
- 这一轮本质上是“Trace 主链路稳定化检查点”，不是“整套产品闭环完成”。
- 下一阶段应把重心转到前后端联调、读侧接口与页面真实数据打通，而不是继续深挖极限压测。
- 建议对应预发布版本号：`v0.4.0-beta1`
- 建议 Release 标题：`Release v0.4.0-beta1: Stabilize Trace Pipeline`
