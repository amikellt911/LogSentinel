#!/usr/bin/env bash

set -euo pipefail

# 这个 wrapper 固化 Suite A 主线版的 AI-off 扫描口径。
# 目的不是图省事，而是把“主线 build-main + disable-ai + disable-webhook + 线程拓扑”
# 一次性写死，避免手工复制时把参数改花，最后又测出一堆脏结果。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

SCAN_ROOT="${SUITE_A_SCAN_ROOT:-/tmp/suite_a_main_ai_off_scan}"
GAPS_MS="${SUITE_A_GAPS_MS:-25,20,15}"
REPEATS="${SUITE_A_REPEATS:-5}"
TRACE_COUNT="${SUITE_A_TRACE_COUNT:-800}"
SPANS_PER_TRACE="${SUITE_A_SPANS_PER_TRACE:-8}"
SEND_WORKERS="${SUITE_A_SEND_WORKERS:-1}"
PORT_BASE="${SUITE_A_PORT_BASE:-18180}"
PORT_STRIDE="${SUITE_A_PORT_STRIDE:-1}"
SERVER_CPUSET="${SUITE_A_SERVER_CPUSET:-2-3}"

exec "${PYTHON_BIN}" "${ROOT_DIR}/server/tests/benchmark/suite_a/run_suite_a_scan.py" \
  --scan-root "${SCAN_ROOT}" \
  --gaps-ms "${GAPS_MS}" \
  --repeats "${REPEATS}" \
  --port-base "${PORT_BASE}" \
  --port-stride "${PORT_STRIDE}" \
  --server-command "taskset -c ${SERVER_CPUSET} ${ROOT_DIR}/server/build-main/LogSentinel --db {sqlite_db} --port {port} --disable-ai --disable-webhook --server-io-threads 1 --worker-threads 32 --dispatch-worker-threads 1" \
  --trace-count "${TRACE_COUNT}" \
  --spans-per-trace "${SPANS_PER_TRACE}" \
  --send-workers "${SEND_WORKERS}" \
  --request-timeout-ms 1000 \
  --poll-interval-ms 50 \
  --stable-rounds 3 \
  --confirm-sleep-ms 100 \
  --max-drain-wait-ms 30000 \
  "$@"
