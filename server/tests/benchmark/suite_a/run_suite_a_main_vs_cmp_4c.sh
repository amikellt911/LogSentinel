#!/usr/bin/env bash

set -euo pipefail

# 这个 wrapper 固化 Suite A 主叙事在 4 核机上的正式口径。
# 目的不是省几个参数，而是彻底堵住“手抄命令时把 main/cmp 或 tuned 参数抄串”的脏结果。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

COMPARE_ROOT="${SUITE_A_COMPARE_ROOT:-/tmp/suite_a_main_vs_cmp_4c}"
PORT_BASE="${SUITE_A_PORT_BASE:-18180}"
PORT_STRIDE="${SUITE_A_PORT_STRIDE:-1}"
REPEATS="${SUITE_A_REPEATS:-3}"
SERVER_CPUSET="${SUITE_A_SERVER_CPUSET:-2-3}"
LOAD_POINTS="${SUITE_A_LOAD_POINTS:-light:800:20:1,mid:1600:10:2,heavy:3200:5:2}"

exec "${PYTHON_BIN}" "${ROOT_DIR}/server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp.py" \
  --compare-root "${COMPARE_ROOT}" \
  --port-base "${PORT_BASE}" \
  --port-stride "${PORT_STRIDE}" \
  --repeats "${REPEATS}" \
  --main-server-bin "${ROOT_DIR}/server/build/LogSentinel" \
  --cmp-server-bin "${ROOT_DIR}/server/build-cmp/LogSentinel" \
  --server-cpuset "${SERVER_CPUSET}" \
  --server-io-threads 1 \
  --worker-threads 32 \
  --dispatch-worker-threads 1 \
  --load-points "${LOAD_POINTS}" \
  --spans-per-trace 8 \
  --request-timeout-ms 1000 \
  --poll-interval-ms 50 \
  --stable-rounds 3 \
  --confirm-sleep-ms 100 \
  --max-drain-wait-ms 30000 \
  "$@"
