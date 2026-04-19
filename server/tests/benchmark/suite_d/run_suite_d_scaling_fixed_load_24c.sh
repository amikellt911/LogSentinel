#!/usr/bin/env bash

set -euo pipefail

# 这条 wrapper 专门补 Suite D 的固定负载扩展曲线。
# sender/wrk 固定在 0-3，connections 固定为 90，只让 backend 从 4 核扫到 24 核；
# 它和 run_suite_d_scaling_24c.sh 的“总核数联动压力曲线”不是同一个论文口径。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

SCALING_ROOT="${SUITE_D_SCALING_ROOT:-/tmp/suite_d_scaling_fixed90_24backend}"
OUTPUT_SUMMARY="${SUITE_D_OUTPUT_SUMMARY:-/tmp/suite_d_scaling_fixed90_24backend_summary.json}"
PORT_BASE="${SUITE_D_PORT_BASE:-18680}"
BACKEND_CORE_POINTS="${SUITE_D_BACKEND_CORE_POINTS:-4,8,12,16,20,24}"
WRK_CPUSET="${SUITE_D_WRK_CPUSET:-0-3}"
WRK_THREADS="${SUITE_D_WRK_THREADS:-4}"
CONNECTIONS="${SUITE_D_CONNECTIONS:-90}"
BACKEND_CORE_OFFSET="${SUITE_D_BACKEND_CORE_OFFSET:-4}"

exec "${PYTHON_BIN}" "${ROOT_DIR}/server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load.py" \
  --scaling-root "${SCALING_ROOT}" \
  --output-summary "${OUTPUT_SUMMARY}" \
  --port-base "${PORT_BASE}" \
  --server-bin "${ROOT_DIR}/server/build/LogSentinel" \
  --backend-core-points "${BACKEND_CORE_POINTS}" \
  --wrk-cpuset "${WRK_CPUSET}" \
  --wrk-threads "${WRK_THREADS}" \
  --connections "${CONNECTIONS}" \
  --backend-core-offset "${BACKEND_CORE_OFFSET}" \
  --duration 15s \
  --warmup-duration 3s \
  --spans-per-trace 8 \
  --trace-lifecycle-profile protected \
  --trace-sealed-grace-window-ms 100 \
  --trace-sweep-interval-ms 20 \
  --trace-primary-flush-span-threshold 512 \
  --trace-primary-flush-interval-ms 5 \
  --trace-max-dispatch-per-tick 256 \
  "$@"
