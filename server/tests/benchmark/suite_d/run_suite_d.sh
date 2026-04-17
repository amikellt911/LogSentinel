#!/usr/bin/env bash

set -euo pipefail

# 这是给历史 wrk 压测留下来的 generic wrapper。
# 它不是论文正式命令；正式入口应走 run_suite_d_case.py / run_suite_d_topology_search.py / run_suite_d_scaling.py。
# 这里继续保留，只是为了让旧的 common runner 还能把结果稳定落到 suite_d 目录。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
export BENCH_SUITE="suite_d"

exec "${ROOT_DIR}/server/tests/benchmark/common/run_wrk_case.sh" "$@"
