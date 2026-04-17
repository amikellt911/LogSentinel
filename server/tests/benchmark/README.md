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

- `server/tests/benchmark/run_paper_benchmark_cloud.sh`
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
- `server/tests/benchmark/suite_d/run_suite_d_local4_ai_on.sh`
- `server/tests/benchmark/suite_d/run_suite_d_topology_search_24c.sh`
- `server/tests/benchmark/suite_d/run_suite_d_connection_search_24c.sh`
- `server/tests/benchmark/suite_d/run_suite_d_scaling_24c.sh`
- `server/tests/benchmark/suite_d/run_suite_d_flamegraph_24c.sh`
- `server/tests/benchmark/suite_d/run_suite_d.sh`
- `server/tests/benchmark/common/run_flamegraph_case.sh`

结果资产口径也已经固定：

- `Suite A / Suite B / Suite D` 的主输出文件不再只落业务指标。
- 每份 `result.json` 和 `summary.json` 现在都会直接带上：
  - `experiment_context`
  - `artifacts`
- `experiment_context` 里会收口：
  - 运行时机器信息
  - CPU 分配 / 绑核口径
  - 线程拓扑
  - workload 与有效实验参数
  - 关键命令上下文
- 机器信息不靠手填备注，而是运行时用 Linux 命令采集：
  - `hostname`
  - `uname -a`
  - `lscpu`
- 如果 `lscpu` 不可用，会自动回退到 `nproc --all` 和 `/proc/cpuinfo`，至少把总逻辑核数和 CPU 型号补齐。
- flamegraph 入口除了原来的 `run-summary.log`，现在还会额外生成 `run-summary.json`。
- 这样后面从云机拷贝单个 JSON 文件回来时，不需要再额外找“这份结果到底是哪台机器、多少核、什么线程配置”。

一键云机入口：

```bash
bash server/tests/benchmark/run_paper_benchmark_cloud.sh \
  --main-server-bin /root/work/LogSentinel/server/build-main/LogSentinel \
  --cmp-server-bin /root/work/LogSentinel-old/server/build-cmp/LogSentinel
```

- 这条总入口会自动读取 cgroup cpuset 起始核，例如 `160-191` 会自动把基址设成 `160`。
- 它默认串行执行 Suite A `16` 核主叙事、Suite A `16` 核 buffer 归因、Suite B `16` 核 campaign、Suite D `24` 核连接确认搜索和 Suite D `24` 核主扩展曲线。
- 如果只想先看命令，不真正压测，加 `--dry-run`。
- 如果暂时不跑 Suite B 这种较长 campaign，加 `--skip-suite-b`。
- 如果要顺手补跑 Suite D 拓扑搜索，加 `--include-d-topology-search`。
- 总入口只负责串联 frozen wrapper，不重新定义实验参数；单项实验的正式口径仍然以各 suite wrapper 为准。

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
- `run_suite_d_local4_ai_on.sh`
  - 是 Suite D 的本机 `4` 核完整链路证明图入口；
  - 它只固定一个代表性负载点，目标是证明“轻量环境下 AI-on 真实可运行”；
  - 这张图不和 `24` 核主扩展曲线混在一起解释。
- `run_suite_d_topology_search_24c.sh`
  - 是 Suite D 的 `24` 核小拓扑搜索入口；
  - 它固定 `sender=4 / backend=20` 和 `AI-off`；
  - 只比较 `T1/T2/T3` 三组 `io/dispatch/worker` 候选，不再扩成大矩阵。
- `run_suite_d_connection_search_24c.sh`
  - 是 Suite D 的 `24` 核连接数确认搜索入口；
  - 它固定 `3/21` sender/backend 拆分和当前 gate 参数；
  - 默认交错复跑 `90/108/120` 三档连接数，按中位数确认最终冻结点。
- `run_suite_d_scaling_24c.sh`
  - 是 Suite D 的 `24` 核主扩展曲线入口；
  - 它固定总核数点位 `4/8/12/16/20/24`；
  - 固定 sender/backend 拆分、T2 比例映射和水位派生；
  - 主指标不是单纯 `QPS`，而是 `online_completed_traces_per_sec`。
- `run_suite_d_flamegraph_24c.sh`
  - 是 Suite D 的 `24` 核 `AI-off` flamegraph 解释图入口；
  - 它和主曲线共用同一套 `24` 核拓扑与 Suite D Lua；
  - 只服务热点解释，不再承担参数搜索职责；
  - 当前会同时落：
    - 人看的 `run-summary.log`
    - 结构化的 `run-summary.json`
    - `trace.svg / perf.data / perf.script / trace-flame.db` 这些原始产物路径
- `run_suite_d.sh`
  - 现在只是历史兼容的 generic wrapper；
  - 它继续把结果落到 `suite_d` 目录，但已经不是论文正式命令。

这样拆的原因很直接：

- `common` 负责“怎么跑一轮实验”
- `suite_*` 负责“这轮实验属于哪条论文叙事”
- `results/*` 负责“结果最后落到哪里”

后面继续补 Suite B 时，不要再把新 sender 塞回 `common/wrk/`。
