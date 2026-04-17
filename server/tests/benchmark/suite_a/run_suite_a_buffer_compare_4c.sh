#!/usr/bin/env bash

set -euo pipefail

# 这个 wrapper 固化 4 核下的 buffer 辅助归因实验。
# 它不负责讲主叙事，只回答“在同一条 tuned protected 基线上，buffered 和 no-buffer 谁在背锅”。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

COMPARE_ROOT="${SUITE_A_COMPARE_ROOT:-/tmp/suite_a_buffer_compare_4c}"
PORT_BASE="${SUITE_A_PORT_BASE:-18380}"
REPEATS="${SUITE_A_REPEATS:-5}"
SERVER_CPUSET="${SUITE_A_SERVER_CPUSET:-2-3}"

exec "${PYTHON_BIN}" "${ROOT_DIR}/server/tests/benchmark/suite_a/run_suite_a_buffer_compare.py" \
  --compare-root "${COMPARE_ROOT}" \
  --port-base "${PORT_BASE}" \
  --repeats "${REPEATS}" \
  --server-bin "${ROOT_DIR}/server/build/LogSentinel" \
  --server-cpuset "${SERVER_CPUSET}" \
  --server-io-threads 1 \
  --worker-threads 32 \
  --dispatch-worker-threads 1 \
  --disable-webhook \
  --gap-ms 10 \
  --trace-count 1600 \
  --spans-per-trace 8 \
  --send-workers 2 \
  --request-timeout-ms 1000 \
  --poll-interval-ms 50 \
  --stable-rounds 3 \
  --confirm-sleep-ms 100 \
  --max-drain-wait-ms 30000 \
  --buffered-span-thresholds 512 \
  --buffered-flush-interval-ms 5 \
  "$@"
