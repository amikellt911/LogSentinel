#!/usr/bin/env bash

set -euo pipefail

# 这条 wrapper 固定 24 核拓扑搜索口径。
# 它只回答“T1/T2/T3 哪个更适合作为主曲线基线”，不承担主扩展曲线职责。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

SEARCH_ROOT="${SUITE_D_SEARCH_ROOT:-/tmp/suite_d_topology_search_24c}"
PORT_BASE="${SUITE_D_PORT_BASE:-18200}"
SERVER_CPUSET="${SUITE_D_SERVER_CPUSET:-4-23}"
WRK_CPUSET="${SUITE_D_WRK_CPUSET:-0-3}"

exec "${PYTHON_BIN}" "${ROOT_DIR}/server/tests/benchmark/suite_d/run_suite_d_topology_search.py" \
  --search-root "${SEARCH_ROOT}" \
  --port-base "${PORT_BASE}" \
  --server-bin "${ROOT_DIR}/server/build/LogSentinel" \
  --server-cpuset "${SERVER_CPUSET}" \
  --wrk-cpuset "${WRK_CPUSET}" \
  --sender-cores 4 \
  --backend-cores 20 \
  --wrk-threads 4 \
  --connections 120 \
  --duration 15s \
  --warmup-duration 3s \
  "$@"
