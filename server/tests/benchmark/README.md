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
- `run_suite_a_main_scan.sh / run_suite_a_cmp_scan.sh`
  - 是当前论文主图口径的固定 wrapper；
  - 两者都默认走 `AI-off`，因为如果把 mock AI 一起开着，`600ms` 量级的推理等待会把 `2~4ms` 量级的存储差异压得很扁；
  - 这两个脚本的目标是把“主线 buffered 路径”和“旧版 direct SQLite 路径”放到更干净的主数据可见性对比里。

这样拆的原因很直接：

- `common` 负责“怎么跑一轮实验”
- `suite_*` 负责“这轮实验属于哪条论文叙事”
- `results/*` 负责“结果最后落到哪里”

后面继续补 Suite B 时，不要再把新 sender 塞回 `common/wrk/`。
