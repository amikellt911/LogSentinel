#!/usr/bin/env bash

set -euo pipefail

# 这个 wrapper 固化 Suite A 旧版 compare_target 的 AI-off 扫描口径。
# 旧版没有显式 --disable-ai，所以这里用“根本不传 auto-start-proxy / trace-ai-provider”的方式关 AI。
# 这么做的目的就是彻底堵住“复制命令时把 auto-start-proxy 拆断”这类人为污染。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

SCAN_ROOT="${SUITE_A_SCAN_ROOT:-/tmp/suite_a_cmp_ai_off_scan}"
GAPS_MS="${SUITE_A_GAPS_MS:-25,20,15}"
REPEATS="${SUITE_A_REPEATS:-5}"
TRACE_COUNT="${SUITE_A_TRACE_COUNT:-800}"
SPANS_PER_TRACE="${SUITE_A_SPANS_PER_TRACE:-8}"
SEND_WORKERS="${SUITE_A_SEND_WORKERS:-1}"
PORT_BASE="${SUITE_A_PORT_BASE:-18186}"
PORT_STRIDE="${SUITE_A_PORT_STRIDE:-1}"
SERVER_CPUSET="${SUITE_A_SERVER_CPUSET:-2-3}"

exec "${PYTHON_BIN}" "${ROOT_DIR}/server/tests/benchmark/suite_a/run_suite_a_scan.py" \
  --scan-root "${SCAN_ROOT}" \
  --gaps-ms "${GAPS_MS}" \
  --repeats "${REPEATS}" \
  --port-base "${PORT_BASE}" \
  --port-stride "${PORT_STRIDE}" \
  --server-command "taskset -c ${SERVER_CPUSET} ${ROOT_DIR}/server/build-cmp/LogSentinel --db {sqlite_db} --port {port} --worker-threads 32" \
  --trace-count "${TRACE_COUNT}" \
  --spans-per-trace "${SPANS_PER_TRACE}" \
  --send-workers "${SEND_WORKERS}" \
  --request-timeout-ms 1000 \
  --poll-interval-ms 50 \
  --stable-rounds 3 \
  --confirm-sleep-ms 100 \
  --max-drain-wait-ms 30000 \
  "$@"
