#!/usr/bin/env bash

set -euo pipefail

# 这条 wrapper 固定 24 核云机主扩展曲线口径。
# 它统一走 AI-off，并用已经冻结的 T2 比例映射去跑 4/8/12/16/20/24 六个总核数点位。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

SCALING_ROOT="${SUITE_D_SCALING_ROOT:-/tmp/suite_d_scaling_24c}"
PORT_BASE="${SUITE_D_PORT_BASE:-18300}"
TOTAL_CORE_POINTS="${SUITE_D_TOTAL_CORE_POINTS:-4,8,12,16,20,24}"
CONNECTIONS_PER_SENDER_CORE="${SUITE_D_CONNECTIONS_PER_SENDER_CORE:-30}"

exec "${PYTHON_BIN}" "${ROOT_DIR}/server/tests/benchmark/suite_d/run_suite_d_scaling.py" \
  --scaling-root "${SCALING_ROOT}" \
  --port-base "${PORT_BASE}" \
  --server-bin "${ROOT_DIR}/server/build/LogSentinel" \
  --total-core-points "${TOTAL_CORE_POINTS}" \
  --connections-per-sender-core "${CONNECTIONS_PER_SENDER_CORE}" \
  --duration 15s \
  --warmup-duration 3s \
  --spans-per-trace 8 \
  --trace-lifecycle-profile protected \
  --trace-sealed-grace-window-ms 100 \
  --trace-sweep-interval-ms 200 \
  --trace-primary-flush-span-threshold 512 \
  --trace-primary-flush-interval-ms 5 \
  "$@"
