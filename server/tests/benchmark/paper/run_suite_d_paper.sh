#!/usr/bin/env bash

set -euo pipefail

# 这条脚本固定 Suite D 的论文正式运行口径。
# 它串起 final-aware 连接确认搜索和 24 核 scaling 主曲线，避免手工重复设置一堆 SUITE_D_* 环境变量。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
STATE_DIR="${BENCHMARK_STATE_DIR:-/tmp/logsentinel_paper_latest}"
STATE_FILE="${STATE_DIR}/suite_d.env"
RUN_ROOT="${SUITE_D_PAPER_RUN_ROOT:-/tmp/suite_d_paper_final-${TIMESTAMP}}"
LOG_ROOT="${RUN_ROOT}/logs"
CONNECTION_ROOT="${SUITE_D_CONNECTION_ROOT:-${RUN_ROOT}/connection_search}"
SCALING_ROOT_PREFIX="${SUITE_D_SCALING_ROOT_PREFIX:-${RUN_ROOT}/scaling_24c}"
SCALING_SUMMARY_JSON="${SUITE_D_SCALING_SUMMARY_JSON:-${RUN_ROOT}/scaling_summary.json}"

default_main_server_bin() {
  if [[ -x "${ROOT_DIR}/server/build-main/LogSentinel" ]]; then
    printf '%s\n' "${ROOT_DIR}/server/build-main/LogSentinel"
    return
  fi
  printf '%s\n' "${ROOT_DIR}/server/build/LogSentinel"
}

detect_core_base_offset() {
  local cpuset_value=""
  local cpuset_file
  for cpuset_file in /sys/fs/cgroup/cpuset.cpus.effective /sys/fs/cgroup/cpuset/cpuset.cpus; do
    if [[ -r "${cpuset_file}" ]]; then
      cpuset_value="$(tr -d '[:space:]' < "${cpuset_file}")"
      if [[ -n "${cpuset_value}" ]]; then
        break
      fi
    fi
  done
  if [[ "${cpuset_value}" =~ ^([0-9]+) ]]; then
    printf '%s\n' "${BASH_REMATCH[1]}"
    return
  fi
  printf '0\n'
}

run_logged() {
  local name="$1"
  shift
  local log_path="${LOG_ROOT}/${name}.log"
  echo "[step] ${name} log=${log_path}"
  "$@" 2>&1 | tee "${log_path}"
}

SERVER_BIN="${BENCHMARK_MAIN_SERVER_BIN:-$(default_main_server_bin)}"
CORE_BASE_OFFSET="${BENCHMARK_CORE_BASE_OFFSET:-$(detect_core_base_offset)}"
CONNECTION_SUMMARY_JSON="${CONNECTION_ROOT}/summary.json"

mkdir -p "${LOG_ROOT}" "${STATE_DIR}"
[[ -x "${SERVER_BIN}" ]] || { echo "Fatal Error: server bin is not executable: ${SERVER_BIN}" >&2; exit 1; }

cat <<EOF
[suite_d_paper] run_root=${RUN_ROOT}
[suite_d_paper] connection_root=${CONNECTION_ROOT}
[suite_d_paper] scaling_root_prefix=${SCALING_ROOT_PREFIX}
[suite_d_paper] core_base_offset=${CORE_BASE_OFFSET}
[suite_d_paper] server_bin=${SERVER_BIN}
EOF

# 连接确认搜索用 final-aware summary 选点；默认采用 90 vs 120 的决赛口径。
run_logged "suite_d_connection_search_finalaware" \
  env \
    SUITE_D_SEARCH_ROOT="${CONNECTION_ROOT}" \
    SUITE_D_SERVER_BIN="${SERVER_BIN}" \
    SUITE_D_CORE_BASE_OFFSET="${CORE_BASE_OFFSET}" \
    SUITE_D_CONNECTION_SET="${SUITE_D_CONNECTION_SET:-90,120}" \
    SUITE_D_REPEATS="${SUITE_D_REPEATS:-5}" \
    SUITE_D_DURATION="${SUITE_D_DURATION:-20s}" \
    SUITE_D_WARMUP_DURATION="${SUITE_D_WARMUP_DURATION:-3s}" \
    SUITE_D_TRACE_SWEEP_INTERVAL_MS="${SUITE_D_TRACE_SWEEP_INTERVAL_MS:-20}" \
    SUITE_D_TRACE_MAX_DISPATCH_PER_TICK="${SUITE_D_TRACE_MAX_DISPATCH_PER_TICK:-1024}" \
    SUITE_D_DISPATCH_WORKER_THREADS="${SUITE_D_DISPATCH_WORKER_THREADS:-4}" \
    SUITE_D_WORKER_QUEUE_SIZE="${SUITE_D_WORKER_QUEUE_SIZE:-8192}" \
    SUITE_D_TRACE_ACTIVE_SESSION_LIMIT="${SUITE_D_TRACE_ACTIVE_SESSION_LIMIT:-3072}" \
    SUITE_D_TRACE_BUFFERED_SPAN_LIMIT="${SUITE_D_TRACE_BUFFERED_SPAN_LIMIT:-24576}" \
  bash "${ROOT_DIR}/server/tests/benchmark/suite_d/run_suite_d_connection_search_24c.sh"

# scaling 主曲线仍用 24c wrapper 的拓扑映射；这里只覆盖 sweep/max-dispatch 这两个正式高频 dispatch 参数。
run_logged "suite_d_scaling_24c" \
  env \
    SUITE_D_SCALING_ROOT="${SCALING_ROOT_PREFIX}" \
    SUITE_D_CORE_BASE_OFFSET="${CORE_BASE_OFFSET}" \
  bash "${ROOT_DIR}/server/tests/benchmark/suite_d/run_suite_d_scaling_24c.sh" \
    --server-bin "${SERVER_BIN}" \
    --output-summary "${SCALING_SUMMARY_JSON}" \
    --trace-sweep-interval-ms "${SUITE_D_SCALING_TRACE_SWEEP_INTERVAL_MS:-20}" \
    --trace-max-dispatch-per-tick "${SUITE_D_SCALING_TRACE_MAX_DISPATCH_PER_TICK:-256}"

ACTUAL_SCALING_ROOT="$(python3 - "${SCALING_SUMMARY_JSON}" <<'PY'
import json
import sys
with open(sys.argv[1], "r", encoding="utf-8") as fh:
    print(json.load(fh)["actual_scaling_root"])
PY
)"

{
  printf 'SUITE_D_RUN_ROOT=%q\n' "${RUN_ROOT}"
  printf 'SUITE_D_CONNECTION_ROOT=%q\n' "${CONNECTION_ROOT}"
  printf 'SUITE_D_CONNECTION_SUMMARY_JSON=%q\n' "${CONNECTION_SUMMARY_JSON}"
  printf 'SUITE_D_SCALING_ROOT=%q\n' "${ACTUAL_SCALING_ROOT}"
  printf 'SUITE_D_SCALING_SUMMARY_JSON=%q\n' "${SCALING_SUMMARY_JSON}"
} > "${STATE_FILE}"

echo "[suite_d_done] state=${STATE_FILE}"
echo "[suite_d_done] connection_summary=${CONNECTION_SUMMARY_JSON}"
echo "[suite_d_done] scaling_summary=${SCALING_SUMMARY_JSON}"
echo "[suite_d_done] actual_scaling_root=${ACTUAL_SCALING_ROOT}"
