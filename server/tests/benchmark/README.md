# Benchmark 目录说明

这里统一存放 benchmark 相关资产，不再把压流脚本、suite runner 和结果目录混在 `server/tests/wrk/` 里。

当前目录语义固定如下：

- `common/`
  - 存放所有 suite 共用的 runner、Lua 脚本、load generator 和校验工具。
- `suite_a/`
  - 存放 Suite A 的入口脚本和后续只属于主链守护对照实验的 profile 配置。
- `suite_b/`
  - 预留给 Suite B 的 sender、manifest、evaluator 和 profile。
- `suite_d/`
  - 存放 Suite D 的入口脚本和后续只属于资源扩展性实验的 profile 配置。
- `results/`
  - 统一存放 benchmark 结果，并按 `suite_a / suite_b / suite_d / archive` 再次分桶。

当前用户优先入口：

- `server/tests/benchmark/suite_a/run_suite_a_case.py`
- `server/tests/benchmark/suite_a/run_suite_a_scan.py`
- `server/tests/benchmark/suite_a/run_suite_a_search_stage1.py`
- `server/tests/benchmark/suite_a/run_suite_a_search_stage1_4c.sh`
- `server/tests/benchmark/suite_a/run_suite_a_buffer_compare_4c.sh`
- `server/tests/benchmark/suite_a/run_suite_a_buffer_compare_16c.sh`
- `server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp_4c.sh`
- `server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp_16c.sh`
- `server/tests/benchmark/suite_a/run_suite_a_main_scan.sh`
- `server/tests/benchmark/suite_a/run_suite_a_cmp_scan.sh`
- `server/tests/benchmark/suite_a/run_suite_a.sh`
- `server/tests/benchmark/suite_d/run_suite_d.sh`
- `server/tests/benchmark/common/run_flamegraph_case.sh`

其中：

- `run_suite_a_case.py`
  - 是 Suite A 第一版 fixed clean sender 入口；
  - 它自己负责发送 clean trace、记录 `t_stop`、轮询 SQLite 主数据并输出
    `visible_completion_rate_at_stop / drain_tail_ms`；
  - 当前 clean 流量默认按 `trace_summary == trace_count` 判定 drain 完成；
    只有在超时前追不到目标 trace 数时，才会把当时的 SQLite 计数记成最终结果；
  - 当前优先服务 `4` 核三档探测，不再复用 `wrk` 当主发生器。
- `run_suite_a.sh`
  - 仍然保留为旧的 wrk wrapper；
  - 这条线后续是否继续保留，取决于 Suite A 主图是否还需要附录里的 wrk 压力证据。
- `run_suite_a_scan.py`
  - 是 Suite A 当前的 gap 扫描编排器；
  - 负责批量跑 `gap x repeat`，并把单轮 `result.json` 聚合成一个 `summary.json`；
  - 当前默认服务 `25/20/15ms` 这类小范围压力扫描，不承担脏时序职责。
- `run_suite_a_search_stage1.py`
  - 是 Suite A 当前的 4 核 Stage 1 粗搜入口；
  - 它先固定一个 clean gap 点位，再按 `Phase A(protected 下的 grace+sweep) -> Phase B(buffer) -> Phase C(AI-on smoke)` 做两阶段剪枝；
  - stdout 只输出“每组一行摘要 + 最终 top-k”，完整明细统一写进 `summary.json` 和每个 case 的 `result.json`；
  - 当前默认只搜索 `protected + buffered`，不再让 `minimal` 抢主候选；
  - 当前 benchmark-only CLI 已额外支持：
    - `--trace-sealed-grace-window-ms`
    - `--trace-primary-flush-span-threshold`
    - `--trace-primary-flush-interval-ms`
  - 这两个参数只用于实验，不会写回正式 Settings。
- `run_suite_a_main_scan.sh / run_suite_a_cmp_scan.sh`
  - 是当前论文主图口径的固定 wrapper；
  - 两者都默认走 `AI-off`，因为如果把 mock AI 一起开着，`600ms` 量级的推理等待会把 `2~4ms` 量级的存储差异压得很扁；
  - 这两个脚本的目标是把“主线 buffered 路径”和“旧版 direct SQLite 路径”放到更干净的主数据可见性对比里。
- `run_suite_a_search_stage1_4c.sh`
  - 是当前唯一保留的调参入口；
  - 它只服务 `4` 核 tuned 值复核，不再承担正式结果图的批跑职责。
- `run_suite_a_buffer_compare_4c.sh / run_suite_a_buffer_compare_16c.sh`
  - 是当前辅助归因图的固定 wrapper；
  - 两者都固定 `protected + grace=100 + sweep=200 + flush=512/5`，只比较 buffered/no-buffer；
  - `4` 核默认压在 `1600 traces / gap=10ms / send_workers=2`；
  - `16` 核默认压在 `6400 traces / gap=5ms / send_workers=2`。
- `run_suite_a_main_vs_cmp_4c.sh / run_suite_a_main_vs_cmp_16c.sh`
  - 是当前论文主叙事的固定 wrapper；
  - 两者都比较 `build-cmp` 和 `build-main(tuned protected buffered)`；
  - `4` 核默认固定三档：`light(800/20/1)`、`mid(1600/10/2)`、`heavy(3200/5/2)`；
  - `16` 核默认固定三档：`light(1600/20/2)`、`mid(3200/10/2)`、`heavy(6400/5/2)`。

这样拆的原因很直接：

- `common` 负责“怎么跑一轮实验”
- `suite_*` 负责“这轮实验属于哪条论文叙事”
- `results/*` 负责“结果最后落到哪里”

后面继续补 Suite B 时，不要再把新 sender 塞回 `common/wrk/`。
