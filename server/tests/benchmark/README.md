# Benchmark 目录说明

这里统一存放 benchmark 相关资产，不再把压流脚本、suite runner 和结果目录混在 `server/tests/wrk/` 里。

当前目录语义固定如下：

- `common/`
  - 存放所有 suite 共用的 runner、Lua 脚本、load generator 和校验工具。
- `suite_a/`
  - 存放 Suite A 的入口脚本和后续只属于功能成本实验的 profile 配置。
- `suite_b/`
  - 预留给 Suite B 的 sender、manifest、evaluator 和 profile。
- `suite_d/`
  - 存放 Suite D 的入口脚本和后续只属于资源扩展性实验的 profile 配置。
- `results/`
  - 统一存放 benchmark 结果，并按 `suite_a / suite_b / suite_d / archive` 再次分桶。

当前用户优先入口：

- `server/tests/benchmark/suite_a/run_suite_a.sh`
- `server/tests/benchmark/suite_d/run_suite_d.sh`
- `server/tests/benchmark/common/run_flamegraph_case.sh`

这样拆的原因很直接：

- `common` 负责“怎么跑一轮实验”
- `suite_*` 负责“这轮实验属于哪条论文叙事”
- `results/*` 负责“结果最后落到哪里”

后面继续补 Suite B 时，不要再把新 sender 塞回 `common/wrk/`。
