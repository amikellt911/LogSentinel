# LogSentinel 校招简历优化版

## 推荐投递版

项目名称：LogSentinel - 轻量级日志 / Trace 实时分析与告警平台

技术栈：C++17、MiniMuduo、Reactor、ThreadPool、SQLite、FastAPI、Vue 3、GTest、GitHub Actions、wrk

项目描述：

面向中小规模服务的日志排障场景，基于 C++17 开发轻量级日志 / Trace 实时分析平台。系统负责接收 Span 数据并按 Trace 聚合，在单机线程池、SQLite 与 Python AI proxy 之间做异步调度，缓解高并发接入与低频 AI 分析之间的速率失配；支持历史查询、Webhook 告警和前端可视化页面。

项目亮点：

1. 基于 MiniMuduo EventLoop + TcpServer 实现 Reactor 风格 HTTP 接入层，设计 `TraceSessionManager` 管理活跃 Trace 会话；当 buffered spans、active sessions、pending tasks 触发高水位时拒绝新 Trace，请求侧返回 `503 + Retry-After`，严重水位时进一步拒绝存量 Trace 的后续 Span，优先保护服务不被积压拖垮。

2. 围绕 Trace 聚合实现多条件分发：按 `trace_end`、Span 容量上限、Token 估算阈值和 idle timeout 触发提交；通过时间轮维护超时会话与重试会话，恢复父子调用链并序列化为树结构；在线程池满载时将会话回滚到内存，并按 `1/2/4/8/16` tick 指数退避重投。

3. 实现 `BufferedTraceRepository` 双缓冲批量写入器，将 `trace_summary / trace_span` 主数据与 `trace_analysis` 分两条缓冲线追加，由后台 flush 线程按大小 / 时间阈值批量写入 SQLite（WAL）；分析阶段通过 Python FastAPI proxy 调用模型，对 critical 风险触发 Webhook 告警，并补齐 GTest/CTest、smoke 测试、wrk 压测脚本和 GitHub Actions 流程。

## 写法说明

- 这一版故意不写具体 QPS 数字，因为仓库里有 wrk 脚本和压测编排，但没有一组稳定、可复述、已经固化进文档的最终指标。
- 这一版把“多维度日志聚合”收窄成“Span -> Trace 会话聚合”，因为代码主线确实是按 `trace_key/span_id/parent_span_id` 组织数据，不是通用日志全文聚合。
- 这一版保留“AI 驱动”表述，但不吹成“实时大模型推理平台”，因为当前做法是 C++ worker 线程经由 Python proxy 调模型，请求失败时会写失败态分析记录，不是强一致实时推理。

## 面试时不要乱吹

- 不要说“无差别降级”。当前策略是高水位优先拒绝新 Trace，critical 才会连已有 Trace 的后续 Span 一起拒绝。
- 不要说“2/4/8/16 退避”。实际实现是 `1/2/4/8/16` tick。
- 不要把“双缓冲落库”扩大成“整个系统所有日志链路都这么做”。这条描述主要对应 Trace 主链路。
- 不要把前端说成“所有监控指标都是真实采集”。当前部分图表数据仍有 mock 补位。
