# Suite A Stage 1 Search Design

## Goal

在 4 核预算和约 30 分钟运行时长内，为 Suite A 找到一组“可解释、可复跑”的主数据可见性优化候选参数，而不是盲目做全排列暴力搜索。

## Current Problem

当前主线在 `AI-off` clean 流量下，`visible_completion_rate_at_stop` 明显落后于历史 compare target。

已经确认的根因不是单点，而是两类固定延迟叠加：

- `protected` 生命周期带来的 `sealed grace`
- `BufferedTraceRepository` 在主数据路径上的时间/批量 flush

与此同时，当前缓冲层的两个关键参数还没有 benchmark 入口：

- `primary_span_reserve`
- `primary_flush_interval_ms`

如果继续只扫 `worker_threads / sweep / lifecycle`，无法回答“buffer 还能不能被救回来”。

## Stage 1 Scope

Stage 1 只解决一个问题：

在 `4` 核、`Suite A`、固定 clean sender 条件下，主线 buffered 路径是否存在明显优于当前默认值的参数区间。

Stage 1 不做：

- 自适应/混合切换逻辑实现
- 过夜级大矩阵
- 16 核资源拓扑
- 论文图表最终排版

## Search Strategy

不做全排列暴力搜索，改做“两阶段剪枝搜索”。

### Phase A: Lifecycle Baseline

先只比较 lifecycle/sweep 的主效应，buffer 参数固定默认值：

- `trace_lifecycle_profile = protected|minimal`
- `trace_sweep_interval_ms = 500|200|100`
- buffer 固定：
  - `primary_span_reserve = 512`
  - `primary_flush_interval_ms = 200`

目标是先找出更适合当前 clean-path 的生命周期底座。

### Phase B: Buffer Main Effect

从 Phase A 挑出更优的 lifecycle/sweep 组合作为底座，再扫 buffer 两个关键参数：

- `primary_span_reserve = 512|256|128|64`
- `primary_flush_interval_ms = 200|50|5`

目标是判断：

- buffer 只是“参数写死得太保守”
- 还是在当前主数据可见性目标下，本身就不适合放在 primary path

### Phase C: AI-on Smoke

只拿前几名候选做少量 `AI-on` 冒烟，不做完整 AI-on 搜索。

目标不是给 `AI-on` 找最优，而是排除“只在 AI-off 好看、AI-on 立刻崩掉”的伪最优。

## Metrics

排序优先级固定为：

1. `visible_completion_rate_at_stop` 越高越好
2. `drain_tail_ms` 越低越好
3. `drain_timeout=false` 必须满足
4. 指标接近时优先更保守、更少改动的参数组

## Deliverables

实现完成后应提供：

- benchmark-only CLI：
  - `--trace-primary-flush-span-threshold`
  - `--trace-primary-flush-interval-ms`
- `Suite A` Stage 1 搜索脚本
- 简洁 stdout 摘要：
  - 每组一行
  - 最终 top-k 表
- 完整结果 JSON 写盘，便于后续 Stage 2 继续细搜

## Non-Goals

Stage 1 不试图证明“主线一定能赢历史 compare target”。

更现实的目标是：

- 找出 buffered 主线在当前 4 核环境下的最好可见性参数区间
- 判断 buffer 的主要问题到底是“参数太保守”，还是“位置放错了”
