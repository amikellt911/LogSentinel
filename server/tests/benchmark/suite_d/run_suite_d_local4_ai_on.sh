#!/usr/bin/env bash

set -euo pipefail

# 这条 wrapper 只服务“4 核本机 + AI-on 的完整链路证明图”。
# 它不是主扩展曲线的一部分，所以只固定一个代表性负载点，不再往外展开成矩阵。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

RUN_ROOT="${SUITE_D_RUN_ROOT:-/tmp/suite_d_local4_ai_on}"
PORT_BASE="${SUITE_D_PORT_BASE:-18180}"
SERVER_CPUSET="${SUITE_D_SERVER_CPUSET:-1-3}"
WRK_CPUSET="${SUITE_D_WRK_CPUSET:-0}"
TRACE_AI_PROVIDER="${SUITE_D_TRACE_AI_PROVIDER:-mock}"

exec "${PYTHON_BIN}" "${ROOT_DIR}/server/tests/benchmark/suite_d/run_suite_d_case.py" \
  --server-bin "${ROOT_DIR}/server/build/LogSentinel" \
  --run-root "${RUN_ROOT}" \
  --port-base "${PORT_BASE}" \
  --server-cpuset "${SERVER_CPUSET}" \
  --wrk-cpuset "${WRK_CPUSET}" \
  --trace-ai-provider "${TRACE_AI_PROVIDER}" \
  --server-io-threads 1 \
  --dispatch-worker-threads 1 \
  --worker-threads 8 \
  --worker-queue-size 4096 \
  --trace-active-session-limit 512 \
  --trace-buffered-span-limit 4096 \
  --trace-max-dispatch-per-tick 64 \
  --trace-lifecycle-profile protected \
  --trace-sealed-grace-window-ms 100 \
  --trace-sweep-interval-ms 200 \
  --trace-primary-flush-span-threshold 512 \
  --trace-primary-flush-interval-ms 5 \
  --wrk-threads 1 \
  --connections 30 \
  --duration 10s \
  --warmup-duration 2s \
  --disable-webhook \
  "$@"
