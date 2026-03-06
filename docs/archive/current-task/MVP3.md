# CurrentTask Archive

## 阶段信息
阶段：MVP3
开始日期：
归档日期：2026-03-06

## 当前阶段
MVP3：以 Trace 为主线，打通 `/logs/spans` 接入、聚合、落库、AI 分析、critical webhook 的最小闭环。

## 阶段目标
- [x] 打通链路闭环：从 `/logs/spans` 接入到 Trace 聚合、落库、AI 分析、critical webhook 的一条龙流程
- [x] 以 Trace 为主线完成聚合能力，其他模块保持最小改动，不做无关扩散

## 本轮范围
要做：
- [x] Trace 聚合主链路
- [x] 时间轮超时收口
- [x] TokenEstimator 最小可用实现
- [x] Webhook 告警打通
- [x] unit / integration / smoke / CI 回归验证

不做：
- [x] ConfigRepository 全面重构
- [x] History / Dashboard 全量后端补齐
- [x] 前后端完整联调收尾

## 核心任务
- [x] 明确 Trace 聚合的缓冲与超时策略并落地实现（时间轮 + 容量/结束标志/token_limit 触发）
- [x] 保证 Trace 聚合结果可被后续流程消费并输出验证结果（落库、AI 分析、critical webhook）
- [x] 复核接入与存储流程，确保链路可回归测试（unit / integration / smoke 已补齐）
- [x] 增加 token 上限配置占位，并升级为最小可用字符估算触发条件

## 验收标准
- [x] Trace 聚合链路可从接入到输出稳定跑通
- [x] 聚合结果在至少一个场景下可验证正确性
- [x] 关键流程有可重复的验证步骤或记录

## 备注
- 时间轮、token_limit、Gemini e2e、critical webhook 已完成最小验证。
- `CurrentTask.md` 自此只保留“当前轮次”，历史轮次归档到 `docs/archive/current-task/`。
