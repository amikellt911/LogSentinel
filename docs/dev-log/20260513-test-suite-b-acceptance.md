# 2026-05-13 test(benchmark): 增加 Suite B 验收小矩阵 wrapper

## Git Commit Message

`test(benchmark): 增加 Suite B 验收小矩阵 wrapper`

## Modification

- `server/tests/benchmark/suite_b/run_suite_b_acceptance.py`
- `docs/todo-list/Todo_Benchmark.md`

## Learning Tips

### Newbie Tips

- 正式论文 campaign 和验收现场复跑不是一回事。正式结果要多 seed 聚合，现场复跑要短、稳定、能说明趋势。
- protected/minimal 的核心指标不是 QPS，而是 `trace_completeness_rate / trace_pollution_rate / duplicate_persistence_rate`。完整率越接近 1 越好，污染和重复越接近 0 越好。

### Function Explanation

- `socket.connect_ex((host, port))`：返回 0 表示端口有人监听；这里用它扫描连续 6 个空闲端口，避免旧进程占用默认端口时脚本直接失败。
- `run_suite_b_matrix.parse_args(...)` 和 `run_suite_b_matrix.run_suite_b_matrix(...)`：wrapper 复用现有矩阵 runner，不复制实验逻辑，只负责固定验收参数和打印核心指标。

### Pitfalls

- 小矩阵只有单 seed、少量 trace，用来现场验证趋势；不要拿它替代正式 `suite_b_correctness.png` 对应的多 seed 聚合图。
- 自动换端口只能避免端口占用，不能保证机器负载稳定。验收前最好先跑一遍，确认耗时和趋势。
