#!/usr/bin/env bash

set -euo pipefail

# 这个 wrapper 固化 4 核 Stage 1 调参入口。
# 参数已经在当前仓库里跑通过一次，后面只有在明确要重搜 tuned 值时才该再动它。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

SEARCH_ROOT="${SUITE_A_SEARCH_ROOT:-/tmp/suite_a_stage1_4c}"
PORT_BASE="${SUITE_A_PORT_BASE:-18180}"
REPEATS="${SUITE_A_REPEATS:-1}"
SERVER_CPUSET="${SUITE_A_SERVER_CPUSET:-2-3}"

exec "${PYTHON_BIN}" "${ROOT_DIR}/server/tests/benchmark/suite_a/run_suite_a_search_stage1.py" \
  --search-root "${SEARCH_ROOT}" \
  --port-base "${PORT_BASE}" \
  --repeats "${REPEATS}" \
  --server-bin "${ROOT_DIR}/server/build/LogSentinel" \
  --server-cpuset "${SERVER_CPUSET}" \
  --server-io-threads 1 \
  --worker-threads 32 \
  --dispatch-worker-threads 1 \
  --trace-count 800 \
  --spans-per-trace 8 \
  --send-workers 1 \
  --request-timeout-ms 1000 \
  --poll-interval-ms 50 \
  --stable-rounds 3 \
  --confirm-sleep-ms 100 \
  --max-drain-wait-ms 30000 \
  "$@"
