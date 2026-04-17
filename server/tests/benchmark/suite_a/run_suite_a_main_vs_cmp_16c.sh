#!/usr/bin/env bash

set -euo pipefail

# 这个 wrapper 固化 Suite A 主叙事在 16 核机上的正式口径。
# 默认把后端压到 12 核区间，保留前面的核位给 sender/系统噪声，避免不同机器手抄命令时线程拓扑漂移。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

COMPARE_ROOT="${SUITE_A_COMPARE_ROOT:-/tmp/suite_a_main_vs_cmp_16c}"
PORT_BASE="${SUITE_A_PORT_BASE:-19180}"
PORT_STRIDE="${SUITE_A_PORT_STRIDE:-1}"
REPEATS="${SUITE_A_REPEATS:-3}"
SERVER_CPUSET="${SUITE_A_SERVER_CPUSET:-4-15}"
LOAD_POINTS="${SUITE_A_LOAD_POINTS:-light:1600:20:2,mid:3200:10:2,heavy:6400:5:2}"

exec "${PYTHON_BIN}" "${ROOT_DIR}/server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp.py" \
  --compare-root "${COMPARE_ROOT}" \
  --port-base "${PORT_BASE}" \
  --port-stride "${PORT_STRIDE}" \
  --repeats "${REPEATS}" \
  --main-server-bin "${ROOT_DIR}/server/build/LogSentinel" \
  --cmp-server-bin "${ROOT_DIR}/server/build-cmp/LogSentinel" \
  --server-cpuset "${SERVER_CPUSET}" \
  --server-io-threads 4 \
  --worker-threads 96 \
  --dispatch-worker-threads 3 \
  --load-points "${LOAD_POINTS}" \
  --spans-per-trace 8 \
  --request-timeout-ms 1000 \
  --poll-interval-ms 50 \
  --stable-rounds 3 \
  --confirm-sleep-ms 100 \
  --max-drain-wait-ms 30000 \
  "$@"
