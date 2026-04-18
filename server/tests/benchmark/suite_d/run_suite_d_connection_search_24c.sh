#!/usr/bin/env bash

set -euo pipefail

# 这条 wrapper 只服务 24 核下的“连接数确认搜索”。
# 前面的探索已经说明主变量不再是 io/dispatch/worker，而是固定 3/21 拆分后，
# 90/108/120 这几档连接数谁更适合作为最终冻结点。
# 这里故意把顺序做成交错复跑，避免 3 个候选总是按同一顺序跑，热机抖动被误当成参数优势。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

SEARCH_ROOT="${SUITE_D_SEARCH_ROOT:-/tmp/suite_d_connection_search_24c}"
PORT_BASE="${SUITE_D_PORT_BASE:-18720}"
SERVER_BIN="${SUITE_D_SERVER_BIN:-${ROOT_DIR}/server/build/LogSentinel}"
CONNECTION_SET="${SUITE_D_CONNECTION_SET:-90,108,120}"
REPEATS="${SUITE_D_REPEATS:-3}"
CORE_BASE_OFFSET="${SUITE_D_CORE_BASE_OFFSET:-0}"

build_cpuset() {
  local start_core="$1"
  local core_count="$2"
  local end_core=$((start_core + core_count - 1))
  if [[ "${core_count}" -eq 1 ]]; then
    printf '%s\n' "${start_core}"
    return
  fi
  printf '%s-%s\n' "${start_core}" "${end_core}"
}

# 24 核确认搜索固定 sender/backend=3/21。
# 默认仍然按 0 起始生成 cpuset，但云机容器可以只改一个 CORE_BASE_OFFSET，
# 直接把整套实验平移到 160-183 这类高位核区间。
DEFAULT_WRK_CPUSET="$(build_cpuset "${CORE_BASE_OFFSET}" 3)"
DEFAULT_SERVER_CPUSET="$(build_cpuset "$((CORE_BASE_OFFSET + 3))" 21)"
WRK_CPUSET="${SUITE_D_WRK_CPUSET:-${DEFAULT_WRK_CPUSET}}"
SERVER_CPUSET="${SUITE_D_SERVER_CPUSET:-${DEFAULT_SERVER_CPUSET}}"

SERVER_IO_THREADS="${SUITE_D_SERVER_IO_THREADS:-6}"
DISPATCH_WORKER_THREADS="${SUITE_D_DISPATCH_WORKER_THREADS:-4}"
WORKER_THREADS="${SUITE_D_WORKER_THREADS:-32}"
WORKER_QUEUE_SIZE="${SUITE_D_WORKER_QUEUE_SIZE:-8192}"
TRACE_ACTIVE_SESSION_LIMIT="${SUITE_D_TRACE_ACTIVE_SESSION_LIMIT:-2048}"
TRACE_BUFFERED_SPAN_LIMIT="${SUITE_D_TRACE_BUFFERED_SPAN_LIMIT:-16384}"
TRACE_MAX_DISPATCH_PER_TICK="${SUITE_D_TRACE_MAX_DISPATCH_PER_TICK:-256}"
TRACE_LIFECYCLE_PROFILE="${SUITE_D_TRACE_LIFECYCLE_PROFILE:-protected}"
TRACE_SEALED_GRACE_MS="${SUITE_D_TRACE_SEALED_GRACE_MS:-100}"
TRACE_SWEEP_INTERVAL_MS="${SUITE_D_TRACE_SWEEP_INTERVAL_MS:-20}"
TRACE_PRIMARY_FLUSH_SPAN_THRESHOLD="${SUITE_D_TRACE_PRIMARY_FLUSH_SPAN_THRESHOLD:-512}"
TRACE_PRIMARY_FLUSH_INTERVAL_MS="${SUITE_D_TRACE_PRIMARY_FLUSH_INTERVAL_MS:-5}"
WRK_THREADS="${SUITE_D_WRK_THREADS:-3}"
DURATION="${SUITE_D_DURATION:-15s}"
WARMUP_DURATION="${SUITE_D_WARMUP_DURATION:-3s}"
SPANS_PER_TRACE="${SUITE_D_SPANS_PER_TRACE:-8}"

IFS=',' read -r -a CONNECTION_VALUES <<< "${CONNECTION_SET}"
if [[ "${#CONNECTION_VALUES[@]}" -lt 1 ]]; then
  echo "Fatal Error: SUITE_D_CONNECTION_SET must contain at least one integer" >&2
  exit 1
fi

mkdir -p "${SEARCH_ROOT}"

run_case() {
  local name="$1"
  local port="$2"
  local connections="$3"

  "${PYTHON_BIN}" "${ROOT_DIR}/server/tests/benchmark/suite_d/run_suite_d_case.py" \
    --run-root "${SEARCH_ROOT}/${name}" \
    --output-json "${SEARCH_ROOT}/${name}.json" \
    --port-base "${port}" \
    --server-bin "${SERVER_BIN}" \
    --server-cpuset "${SERVER_CPUSET}" \
    --wrk-cpuset "${WRK_CPUSET}" \
    --wrk-threads "${WRK_THREADS}" \
    --connections "${connections}" \
    --duration "${DURATION}" \
    --warmup-duration "${WARMUP_DURATION}" \
    --spans-per-trace "${SPANS_PER_TRACE}" \
    --server-io-threads "${SERVER_IO_THREADS}" \
    --dispatch-worker-threads "${DISPATCH_WORKER_THREADS}" \
    --worker-threads "${WORKER_THREADS}" \
    --worker-queue-size "${WORKER_QUEUE_SIZE}" \
    --trace-active-session-limit "${TRACE_ACTIVE_SESSION_LIMIT}" \
    --trace-buffered-span-limit "${TRACE_BUFFERED_SPAN_LIMIT}" \
    --trace-max-dispatch-per-tick "${TRACE_MAX_DISPATCH_PER_TICK}" \
    --trace-lifecycle-profile "${TRACE_LIFECYCLE_PROFILE}" \
    --trace-sealed-grace-window-ms "${TRACE_SEALED_GRACE_MS}" \
    --trace-sweep-interval-ms "${TRACE_SWEEP_INTERVAL_MS}" \
    --trace-primary-flush-span-threshold "${TRACE_PRIMARY_FLUSH_SPAN_THRESHOLD}" \
    --trace-primary-flush-interval-ms "${TRACE_PRIMARY_FLUSH_INTERVAL_MS}" \
    --disable-ai \
    --disable-webhook \
    --no-auto-start-proxy
}

port="${PORT_BASE}"
for ((repeat_index = 0; repeat_index < REPEATS; ++repeat_index)); do
  for ((offset = 0; offset < ${#CONNECTION_VALUES[@]}; ++offset)); do
    candidate_index=$(((repeat_index + offset) % ${#CONNECTION_VALUES[@]}))
    connections="${CONNECTION_VALUES[${candidate_index}]}"
    run_name="$(printf 'r%d_c%03d' "$((repeat_index + 1))" "${connections}")"
    run_case "${run_name}" "${port}" "${connections}"
    port=$((port + 1))
  done
done

# 连接确认搜索现在不能再只看 online 中位数。
# online 只能说明测量窗口内已经落了多少，final completion 才能说明 case 收尾后真正留下了多少。
# 这里改成统一走 Python helper，把 final ratio/final count 一起纳入 summary 与 winner 判定。
"${PYTHON_BIN}" "${ROOT_DIR}/server/tests/benchmark/suite_d/run_suite_d_connection_search_summary.py" \
  --search-root "${SEARCH_ROOT}" \
  --connection-set "${CONNECTION_SET}"
