# 20260419 - fix(benchmark): 修复 Suite D fixed-load 诊断脚本结果路径

## Git Commit Message

`fix(benchmark): 修复 Suite D fixed-load 诊断脚本结果路径`

## Modification

- `server/tests/benchmark/paper/diagnose_suite_d_fixed_load.py`

## Summary

- 修复 fixed-load 诊断脚本只从 `actual_run_root/result.json` 读取结果的问题。
- 当前 Suite D single-case runner 的路径语义是：
  - `result.json` 写在 `requested_run_root/result.json`；
  - `server.log/sqlite` 写在带时间戳的 `actual_run_root`。
- 诊断脚本现在会优先读取 `requested_run_root/result.json`，再回退到 `actual_run_root/result.json`，两者都不存在时输出带 backend 核数的明确错误。

## Verification

- 用 `/tmp/diag_fixed_path_red.*` 复现旧脚本读 `actual_run_root/result.json` 失败。
- 用 `/tmp/diag_fixed_path_green.*` 验证新脚本可从 `requested_run_root/result.json` 读取结果，并继续从 result 中记录的 `server_log` 抽 runtime stats。
- `python3 -m py_compile server/tests/benchmark/paper/diagnose_suite_d_fixed_load.py`
- `git diff --check`

## Learning Tips

### Newbie Tips

- benchmark 脚本里经常同时存在 requested path 和 actual timestamp path，不能默认所有产物都在同一个目录。
- 诊断脚本要跟数据生产脚本的真实落盘规则对齐，否则会把“路径约定不同”误判成“实验数据丢失”。

### Function Explanation

- `resolve_result_path(...)`：按候选路径顺序查找 `result.json`，先查 requested run root，再查 actual run root。

### Pitfalls

- 不要用远端手工 Python 片段临时绕过路径问题；路径规则应该收进脚本，避免下一次复制命令又因为换行或缩进失败。
