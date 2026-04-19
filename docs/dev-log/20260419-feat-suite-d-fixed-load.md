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

---

## 追加：Suite D formal clean runner

### Git Commit Message

`feat(benchmark): 增加 Suite D 正式清理复跑脚本`

### Modification

- `server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load.py`
- `server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_unit_test.py`
- `server/tests/benchmark/paper/run_suite_d_formal_clean.sh`
- `docs/todo-list/Todo_Benchmark.md`

### Summary

- fixed-load runner 增加 `--cleanup-sqlite-db`：
  - 每个 case 完成后只删除该 case 的 SQLite DB；
  - 保留 `result.json / server.log / wrk.log`，保证后续诊断和论文资产可复核。
- fixed-load runner 增加 `--cooldown-sec`：
  - 每个 case 清理后等待指定秒数；
  - 目标是减少 overlay/page-cache/writeback 状态对后续 case 的污染。
- 新增 `run_suite_d_formal_clean.sh`：
  - 默认固定 `dispatch=512 / flush_threshold=1024 / flush_interval=5ms`；
  - 默认打开 `cleanup_sqlite_db=1` 和 `cooldown_sec=60`；
  - 自动导出 summary、diagnostics、result/log 到 `${HOME}/paper_assets/<date>/suite_d_formal/<tag>`。

### Verification

- `python3 server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_unit_test.py -k test_cleanup_sqlite_db_and_cooldown_after_each_case`
- `python3 server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_unit_test.py`
- `python3 -m py_compile server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load.py server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_unit_test.py`
- `bash -n server/tests/benchmark/paper/run_suite_d_formal_clean.sh`

### Learning Tips

#### Newbie Tips

- benchmark 的“数据文件清理”和“结果可复核”不冲突：大 DB 可以删，小 JSON/log 必须留。
- SQLite 压测很容易被文件系统状态污染，尤其容器 overlay 接近满盘时，单次 flush 延迟会从十几毫秒飙到上百毫秒。

#### Function Explanation

- `cleanup_case_sqlite_db(...)`：从 case result 里读取 `sqlite_db` 路径，删除 DB 文件但保留 run 目录。
- `sleeper` 注入：单测用 list append 代替真实 `time.sleep`，这样能验证 cooldown 调用次数又不拖慢测试。

#### Pitfalls

- 不能在 case 结束后直接删整个 run 目录，否则诊断脚本会丢失 `result.json/server.log`。
- 不能只靠外层 shell `sleep`；如果一个 runner 内部连续跑 `16/20/24`，case 之间仍然需要 runner 内部 cooldown。

---

## 追加：Suite D fixed-load 容量闸门覆写

### Git Commit Message

`feat(benchmark): 支持 Suite D 固定负载容量闸门覆写`

### Modification

- `server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load.py`
- `server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_unit_test.py`
- `server/tests/benchmark/paper/run_suite_d_formal_clean.sh`
- `docs/todo-list/Todo_Benchmark.md`

### Summary

- fixed-load runner 新增：
  - `--trace-active-session-limit`
  - `--trace-buffered-span-limit`
- 这两个参数会直接覆写每个点位原本按 backend 核数派生的 session/buffer 闸门，便于验证高核退化到底是 CPU、线程拓扑还是容量闸门导致。
- summary metadata 增加 `forced_trace_limits`，后续看 JSON 就能知道这轮 probe 有没有真的打进后端。
- `run_suite_d_formal_clean.sh` 增加 `"$@"` 透传，允许正式 clean runner 直接携带额外 probe 参数，不需要手改脚本。

### Verification

- `python3 server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_unit_test.py -k test_trace_limit_override_replaces_derived_limits`
- `python3 server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_unit_test.py`
- `python3 -m py_compile server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load.py server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_unit_test.py`
- `bash -n server/tests/benchmark/paper/run_suite_d_formal_clean.sh`

### Learning Tips

#### Newbie Tips

- “线程数不够”和“容量闸门太紧”是两类完全不同的瓶颈，不能混着猜。
- 如果实验参数没有写进 summary metadata，后面很容易把“没生效的 probe”误当成真实结果。

#### Function Explanation

- `forced_trace_limits_from_args(...)`：只收集用户明确指定的容量覆写字段，方便 summary 直接留痕。
- `build_case_args(...)`：先按 backend 核数派生默认值，再叠加 CLI/env 覆写，保证默认口径不回归。

#### Pitfalls

- formal clean 脚本如果不透传 `"$@"`，你在命令行补的 probe 参数会在 shell 层被吞掉，看起来像“跑成功了”，实际上完全没生效。
- `trace_buffered_span_limit` 不能强制跟 `active_session_limit * 8` 绑死；做 probe 时必须允许两者拆开验证。
