# 20260419 - feat(benchmark): 补 Suite D 固定负载扩展曲线

## Git Commit Message

`feat(benchmark): 补 Suite D 固定负载扩展曲线`

## Modification

- `server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load.py`
- `server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_24c.sh`
- `server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_unit_test.py`
- `server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py`
- `server/tests/benchmark/paper/export_suite_d_paper_assets.sh`
- `server/tests/benchmark/paper/export_suite_d_fixed_load_assets.sh`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- 新增 Suite D fixed-load runner：
  - 固定 sender/wrk 为 `0-3`；
  - 固定 `wrk_threads=4`；
  - 固定 `connections=90`；
  - 只扫描 backend `4/8/12/16/20/24` 核；
  - backend 默认从 CPU `4` 开始，也就是最大点位为 `4-27`，给 32 vCPU 机器保留 `28-31` 给系统抖动。
- 新增 `run_suite_d_scaling_fixed_load_24c.sh` wrapper：
  - 默认输出 summary 到 `/tmp/suite_d_scaling_fixed90_24backend_summary.json`；
  - 默认结果根目录为 `/tmp/suite_d_scaling_fixed90_24backend-*`；
  - 保留 `--server-bin` 等透传能力，远端可以直接覆盖为 `build-main/LogSentinel`。
- 更新 Suite D paper export：
  - 自动识别 `/tmp/suite_d_scaling_fixed90_24backend_summary.json`；
  - 导出到 `summary/scaling_summary_fixed90.json`；
  - 复制 case result 到 `cases/scaling_fixed90/`；
  - 生成 `diagnostics/diagnostics_scaling_fixed90.log`。
- 新增 fixed-load 专用导出脚本：
  - 用于新 32 vCPU 实例只补跑 fixed90 的场景；
  - 不依赖旧 connection/default scaling 的 `/tmp` 结果目录；
  - 默认导出到 `${HOME}/paper_assets/<date>/suite_d_fixed90`。

## Verification

- `python3 server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_unit_test.py`
- `python3 server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py`
- `bash -n server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_24c.sh server/tests/benchmark/paper/export_suite_d_paper_assets.sh`
- 使用 `/tmp/suite_d_fixed_export_mock.*` 临时目录模拟 fixed90 summary 和 result.json，确认 export 脚本会生成 `summary/scaling_summary_fixed90.json`、`cases/scaling_fixed90/backend_24/run_01/result.json`、`diagnostics/diagnostics_scaling_fixed90.log`
- `bash -n server/tests/benchmark/paper/export_suite_d_fixed_load_assets.sh`

## Learning Tips

### Newbie Tips

- 扩展性实验必须分清“固定输入负载”和“输入负载随资源一起涨”。
- 旧 Suite D 的 `connections = sender_cores * connections_per_sender_core` 是端到端联动压力曲线，不适合直接证明同负载下 24 核比 20 核更好或更差。
- fixed-load 曲线把 sender/wrk 和 connections 固定住后，backend 的 drain 和 online completion 才更适合横向比较。

### Function Explanation

- `run_fixed_load_scaling(...)`：按 backend 核数逐点生成单 case 参数，调用 `run_suite_d_case`，再聚合 summary。
- `build_case_args(...)`：把固定 wrk 负载和递增 backend cpuset 拼成单 case runner 能消费的 CLI。
- `export_scaling_variant(...)`：兼容 `by_total_cores` 与 `by_backend_cores` 两种 summary，统一导出 summary、case result 和诊断摘录。

### Pitfalls

- 不能复用旧 `run_suite_d_scaling_24c.sh` 来假装 fixed-load，因为旧脚本会让 sender 核数、wrk threads 和 connections 一起变化。
- 不能把 fixed90 结果覆盖旧 scaling summary；这两组实验口径不同，必须用独立文件名和独立 cases 目录。

---

## 追加：Suite D fixed-load 拓扑覆写诊断

### Git Commit Message

`feat(benchmark): 支持 Suite D 固定负载线程拓扑覆写`

### Modification

- `server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load.py`
- `server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_unit_test.py`
- `docs/todo-list/Todo_Benchmark.md`

### Summary

- 为 fixed-load runner 增加 `--force-server-io-threads / --force-dispatch-worker-threads / --force-worker-threads`。
- 同时支持 `SUITE_D_FORCE_SERVER_IO_THREADS / SUITE_D_FORCE_DISPATCH_WORKER_THREADS / SUITE_D_FORCE_WORKER_THREADS` 环境变量，方便远端 wrapper 直接透传。
- 覆写只改变线程拓扑，不改变 `backend_cores` 对应的 `server_cpuset`，用于验证高核退化是否来自过度线程化。
- summary metadata 增加 `forced_topology`，避免后续导出资产时看不出该轮是否使用了诊断拓扑。

### Verification

- `python3 server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_unit_test.py -k test_thread_topology_override_keeps_backend_cpuset`
- `python3 server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_unit_test.py`
- `python3 -m py_compile server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load.py server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_unit_test.py`

### Learning Tips

#### Newbie Tips

- CPU 资源窗口和线程拓扑是两回事：`server_cpuset=68-91` 表示进程可用 24 个核，但 `worker_threads=28` 是应用内部调度策略。
- 高核退化不一定是“核不够”，也可能是线程太多导致共享锁、队列和 SQLite flush 竞争变重。

#### Function Explanation

- `parse_optional_positive_int(...)`：把 CLI/env 输入统一解析成正整数或 `None`，`None` 表示沿用默认拓扑。
- `resolve_backend_topology(...)`：先取默认 backend 拓扑，再叠加用户明确指定的线程覆写。

#### Pitfalls

- 不能用缩小 `server_cpuset` 来模拟 20 核线程拓扑；那会同时改变 CPU 资源和线程数，诊断变量不干净。
- forced topology 只适合定位瓶颈，不应该直接覆盖原始 fixed-load 主曲线，除非后续重新冻结论文口径。
