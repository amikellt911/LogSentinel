# Benchmark 论文资产收口规程

## 1. 文档目的

这份文档只服务一件事：把论文正式实验资产收口成一套轻量、可搬运、可复核的交付物。

既然云机可能换机器、磁盘可能打满、`/tmp` 目录随时可能被清理，那么后面的正式实验不能再依赖“原始大目录一直留在云机上”。

从现在开始，正式实验的目标不是保留所有原始文件，而是保留：

- 能支撑论文主论点的结构化结果
- 能快速复核结论的诊断摘录
- 少量必要的图形资产

这份文档不服务调参过程留档，也不服务长期保存所有原始数据库和日志。

## 2. 总原则

论文正式资产统一遵守下面几条原则：

- `/tmp` 只作为实验运行工作区，不作为成果仓库。
- 每个 suite 正式跑完后，立刻导出该 suite 的论文资产包，不等全部实验结束后再统一收。
- 默认只保留 `summary.json`、`result.json`、`diagnostics.log` 和必要图形资产。
- 默认不保留 `*.db`、`*.db-wal`、`*.db-shm`、全量 `server.log`、全量 `wrk.log`。
- 只有出现异常 run 时，才额外保留更重的诊断包。
- 论文写作、结果复核和后续对照，都以导出的论文资产包为准，不再依赖 `/tmp` 原始目录。

## 3. 统一目录规范

每轮正式实验都导出到本机固定目录：

```text
~/paper_assets/<date>/
  suite_a/
    summary/
    cases/
    diagnostics/
  suite_b/
    summary/
    cases/
    diagnostics/
  suite_d/
    summary/
    cases/
    diagnostics/
    flamegraph/
```

说明：

- `<date>` 建议使用实验日期，例如 `20260418`。
- `summary/` 只放 suite 级汇总 JSON。
- `cases/` 只放单 case 的 `result.json` 或少量代表性 case JSON。
- `diagnostics/` 只放摘录后的 `diagnostics.log`，不放整份大日志。
- `flamegraph/` 只在该 suite 真的有 flamegraph 资产时创建。

## 4. 导出时机与流程

每个 suite 正式实验完成后，统一按下面流程导出：

1. 确认该 suite 的 `summary.json` 已生成。
2. 确认该 suite 需要保留的单 case `result.json` 已生成。
3. 从原始日志中摘出关键诊断行，生成一个小体积 `diagnostics.log`。
4. 把 `summary.json`、`result.json`、`diagnostics.log` 和必要图形资产复制到 `~/paper_assets/<date>/suite_x/`。
5. 复制完成后，再决定是否删除 `/tmp` 原始目录。

这条流程必须逐 suite 执行。不能把 `Suite A / B / D` 全部跑完后，再回头从 `/tmp` 慢慢挑资产，因为这样最容易在云机磁盘清理、目录覆盖或误删后丢完整轮结果。

## 5. Suite A 资产清单

Suite A 的职责是证明：为了获得生命周期保护，主链性能代价处于可接受范围内。

因此 Suite A 的论文资产包只保留下面这些文件：

### 5.1 必留文件

- `summary/main_vs_cmp_summary.json`
  - 对应 `run_suite_a_main_vs_cmp_*` 这一类主对比实验的总汇总。
- `summary/buffer_compare_summary.json`
  - 对应 `run_suite_a_buffer_compare_*` 这一类辅助归因实验的总汇总。
- `cases/`
  - 每档负载至少保留一份代表性 `result.json`。
- `diagnostics/diagnostics.log`
  - 只摘录关键运行时指标，不保留整份大日志。

### 5.2 诊断重点

`diagnostics.log` 里优先保留：

- `online_completed_traces_per_sec`
- `drain_tail_ms`
- `sqlite_counts_at_stop`
- `sqlite_counts_final`
- 必要的 `[TraceRuntimeStats]`
- 必要的 `[BufferedTraceRuntimeStats]`

### 5.3 默认不留

- `suite_a` 产生的原始 SQLite 数据库
- 全量 `server.log`
- 全量 `wrk.log`

## 6. Suite B 资产清单

Suite B 的职责是证明：生命周期保护确实解决了乱序、晚到、多线程并发条件下的 trace 聚合正确性问题。

因此 Suite B 的论文资产包只保留下列文件：

### 6.1 必留文件

- `summary/campaign_summary.json`
  - 对应正式 campaign 级别的总汇总。
- `summary/`
  - 每个 matrix 运行的 `summary.json`。
- `cases/`
  - 少量代表性单 case 的 `result.json`，不用把所有 run 都搬走。
- `diagnostics/diagnostics.log`
  - correctness 相关诊断摘录。

### 6.2 诊断重点

`diagnostics.log` 里优先保留：

- `sqlite_unique_constraint_fail_count`
- completeness
- pollution
- duplicate
- 必要时再补 lifecycle 相关关键日志摘录

### 6.3 默认不留

- sender manifest 原始大文件
- 原始 SQLite 数据库
- 全量服务端日志

如果某次结果异常，manifest 可以临时单独导出做诊断，但不进入默认论文资产包。

## 7. Suite D 资产清单

Suite D 的职责是证明：在 clean 高压输入与扩展部署条件下，系统仍能保持最终完整落库能力和稳定扩展性。

因此 Suite D 的论文资产包要围绕 `final-aware` 汇总来组织。

### 7.1 必留文件

- `summary/connection_search_summary.json`
  - 对应连接数确认搜索的最终汇总。
- `summary/scaling_summary.json`
  - 对应 `24` 核主扩展曲线的最终汇总。
- `cases/`
  - 每个正式点位对应的 `result.json`。
- `diagnostics/diagnostics.log`
  - 高压条件下的关键诊断摘录。
- `flamegraph/trace.svg`
  - 只在正式 flamegraph 运行存在时保留。
- `flamegraph/run-summary.json`
  - 对应 flamegraph 的结构化元数据。

### 7.2 诊断重点

`diagnostics.log` 里优先保留：

- `UNIQUE constraint`
- `[TraceRuntimeStats]`
- `[BufferedTraceRuntimeStats]`
- `stop=...`
- `final=...`
- `drain=...`

### 7.3 口径要求

Suite D 的正式口径必须以 `final-aware` 结果为准。

也就是说，正文和图表里的主要依据应当优先引用：

- `median_final_trace_summary`
- `median_final_completion_ratio`

`online_completed_traces_per_sec`、`requests_per_sec`、`drain_tail_ms` 属于辅助指标，不得重新退化成只按 `online/QPS` 选 winner 的旧口径。

### 7.4 默认不留

- 原始 `.db`
- 原始 `.db-wal`
- 原始 `.db-shm`
- 全量 `server.log`

## 8. 异常诊断包触发条件与协作流程

### 8.1 异常诊断包触发条件

只有在下面这些情况出现时，才额外保留更重的原始诊断资产：

- 出现 `UNIQUE constraint`
- `submit_fail_count > 0`
- `primary_flush_fail_count > 0`
- `final completion` 明显异常
- 某轮结果显著好于或差于同批次其它 run，需要回头查根因

此时允许临时额外保留：

- 对应 run 的全量 `server.log`
- 对应 run 的全量 `wrk.log`
- 必要时保留该 run 的 SQLite 数据库

但这些文件属于“异常诊断包”，不属于默认论文资产包。

### 8.2 协作流程

后续协作统一按下面流程执行：

- 你负责在每个 suite 跑完后，按本规程把该 suite 的论文资产包导出到本机固定目录。
- 我负责按本规程逐项核对文件是否齐全、字段是否够支撑论文主论点。
- 如果我发现缺字段、缺 case、缺诊断摘录，我会按本规程指出具体缺口，而不是临时改变收口口径。
- 如果没有触发异常诊断包条件，就不再要求你保留整份大目录。

这套规程的目标不是保存所有实验痕迹，而是保证：

- 正式论文结果不丢
- 复核成本可控
- 云机磁盘不会再次被原始数据库和日志拖满
