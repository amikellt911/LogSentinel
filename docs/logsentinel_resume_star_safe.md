# LogSentinel 校招简历优化版（STAR）

## 简历正文

项目名称：LogSentinel - 轻量级日志与 Trace 实时分析平台

技术栈：C++17、MiniMuduo、Reactor、ThreadPool、SQLite（WAL）、FastAPI、Vue 3、GTest、GitHub Actions、wrk

项目描述：

面向中小规模服务的日志排障场景，开发基于 C++17 的日志 / Trace 实时分析平台。项目核心问题是高并发 Span 接入与低频 AI 分析、SQLite 落库之间存在明显速率失配：前端接收快，后端计算和持久化慢，峰值流量下容易出现会话堆积、线程池阻塞和请求雪崩。我的工作重点是把接入、聚合、落库和分析拆成异步阶段，在单机内存缓冲和分级背压的前提下，把 Trace 主链路跑通，并补齐查询、告警和测试脚本。

核心工作：

1. 针对高并发请求容易把后端拖垮的问题，基于 MiniMuduo 搭建 Reactor 风格 HTTP 接入层，抽象 `TraceSessionManager` 统一管理活跃 Trace 会话，并按 buffered spans、active sessions、pending tasks 三类指标做分级准入；高水位时向请求侧返回 `503 + Retry-After` 优先拒绝新 Trace，critical 水位才进一步拒绝存量 Trace 的后续 Span，把过载影响限制在入口阶段。

2. 针对离散 Span 难以还原调用链、以及下游线程池满载会导致任务丢失的问题，围绕 `trace_end`、Span 容量阈值、Token 估算阈值和 idle timeout 设计多条件分发；使用时间轮维护 collecting / retry 会话，按 `parent_span_id` 恢复父子关系并序列化树状 Trace；当 submit 失败时把会话回滚到内存，并按 `1/2/4/8/16` tick 指数退避重投，避免客户端拿到已接收状态后服务端内部静默丢数据。

3. 针对 SQLite 单文件写入容易阻塞主链路的问题，实现 `BufferedTraceRepository` 双缓冲批量写入器，将 `trace_summary / trace_span` 主数据与 `trace_analysis` 分两条缓冲线交给后台 flush 线程按大小 / 时间阈值批量落库，底层启用 SQLite WAL；分析阶段通过 FastAPI proxy 对接模型服务，critical 风险触发 Webhook 告警，并补齐 GTest/CTest、basic smoke、手动 advanced / integration workflow 和 wrk 压测脚本，形成可验证的工程闭环。

## 使用说明

- 这一版比上一版更偏 STAR 写法：先讲场景和问题，再讲动作，最后落到系统结果。
- 这一版故意不写固定 QPS 数字，因为仓库里有压测脚本，但没有已经固化、可稳定复现实验环境下的最终指标表。
- 如果要投校招，这版可以直接放简历；如果面试官继续追问，再沿着 `TraceSessionManager`、时间轮、双缓冲、`503 + Retry-After` 这些真实实现展开。

## 面试时别乱说

- 不要把当前系统说成“全链路实时监控平台”，前端有些图表仍有 mock 补位。
- 不要把 FastAPI proxy 说成“异步大模型推理引擎”，准确说法是“通过 Python proxy 对接模型服务”。
- 不要把 GitHub Actions 说成“所有链路自动化门禁”，现在是 unit 和 basic smoke 常规跑，advanced smoke 与 integration 仍偏手动触发。
