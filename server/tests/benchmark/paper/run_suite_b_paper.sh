#!/usr/bin/env bash

set -euo pipefail

# 这条脚本固定 Suite B 的论文正式运行口径。
# 当前正式口径是 x10 总量：每个 case 约 800 条 span 请求，用来避免 80 条玩具规模被质疑。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
STATE_DIR="${BENCHMARK_STATE_DIR:-/tmp/logsentinel_paper_latest}"
STATE_FILE="${STATE_DIR}/suite_b.env"

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

SERVER_BIN="${BENCHMARK_MAIN_SERVER_BIN:-$(default_main_server_bin)}"
CORE_BASE_OFFSET="${BENCHMARK_CORE_BASE_OFFSET:-$(detect_core_base_offset)}"
SENDER_CPUSET="${SUITE_B_SENDER_CPUSET:-$(build_cpuset "${CORE_BASE_OFFSET}" 3)}"
SERVER_CPUSET="${SUITE_B_SERVER_CPUSET:-$(build_cpuset "$((CORE_BASE_OFFSET + 3))" 13)}"
CAMPAIGN_ROOT_PREFIX="${SUITE_B_CAMPAIGN_ROOT_PREFIX:-/tmp/suite_b_paper_x10-${TIMESTAMP}}"
SUMMARY_JSON="${SUITE_B_SUMMARY_JSON:-${CAMPAIGN_ROOT_PREFIX}_summary.json}"

mkdir -p "${STATE_DIR}"
[[ -x "${SERVER_BIN}" ]] || { echo "Fatal Error: server bin is not executable: ${SERVER_BIN}" >&2; exit 1; }

cat <<EOF
[suite_b_paper] campaign_root_prefix=${CAMPAIGN_ROOT_PREFIX}
[suite_b_paper] summary_json=${SUMMARY_JSON}
[suite_b_paper] sender_cpuset=${SENDER_CPUSET}
[suite_b_paper] server_cpuset=${SERVER_CPUSET}
[suite_b_paper] server_bin=${SERVER_BIN}
EOF

# Suite B 是生命周期正确性实验，不追求 Suite D 那种极限 QPS。
# 这里把 trace-count 放大到 100，把 send-workers 放到 16，保证样本量足够，但不改 grace/tombstone 等语义窗口。
taskset -c "${SENDER_CPUSET}" "${PYTHON_BIN}" "${ROOT_DIR}/server/tests/benchmark/suite_b/run_suite_b_campaign.py" \
  --campaign-root "${CAMPAIGN_ROOT_PREFIX}" \
  --output-summary "${SUMMARY_JSON}" \
  --seeds "${SUITE_B_SEEDS:-20260415,20260416,20260417,20260418,20260419}" \
  --port-base "${SUITE_B_PORT_BASE:-19780}" \
  --port-stride "${SUITE_B_PORT_STRIDE:-20}" \
  --server-bin "${SERVER_BIN}" \
  --server-cpuset "${SERVER_CPUSET}" \
  --server-io-threads "${SUITE_B_SERVER_IO_THREADS:-5}" \
  --worker-threads "${SUITE_B_WORKER_THREADS:-16}" \
  --dispatch-worker-threads "${SUITE_B_DISPATCH_WORKER_THREADS:-4}" \
  --worker-queue-size "${SUITE_B_WORKER_QUEUE_SIZE:-8192}" \
  --trace-capacity "${SUITE_B_TRACE_CAPACITY:-12}" \
  --trace-token-limit 0 \
  --trace-sweep-interval-ms "${SUITE_B_TRACE_SWEEP_INTERVAL_MS:-100}" \
  --trace-idle-timeout-ms "${SUITE_B_TRACE_IDLE_TIMEOUT_MS:-800}" \
  --trace-max-dispatch-per-tick "${SUITE_B_TRACE_MAX_DISPATCH_PER_TICK:-128}" \
  --trace-buffered-span-limit "${SUITE_B_TRACE_BUFFERED_SPAN_LIMIT:-8192}" \
  --trace-active-session-limit "${SUITE_B_TRACE_ACTIVE_SESSION_LIMIT:-2048}" \
  --trace-count "${SUITE_B_TRACE_COUNT:-100}" \
  --spans-per-trace "${SUITE_B_SPANS_PER_TRACE:-8}" \
  --trace-gap-ms "${SUITE_B_TRACE_GAP_MS:-40}" \
  --send-workers "${SUITE_B_SEND_WORKERS:-16}" \
  --disable-ai \
  --disable-webhook \
  --no-auto-start-proxy \
  --sender-profiles clean_baseline,mixed_realistic,late_replay_stress \
  --trace-lifecycle-profiles protected,minimal

ACTUAL_CAMPAIGN_ROOT="$("${PYTHON_BIN}" - "${SUMMARY_JSON}" <<'PY'
import json
import sys
with open(sys.argv[1], "r", encoding="utf-8") as fh:
    print(json.load(fh)["actual_campaign_root"])
PY
)"

{
  printf 'SUITE_B_CAMPAIGN_ROOT=%q\n' "${ACTUAL_CAMPAIGN_ROOT}"
  printf 'SUITE_B_SUMMARY_JSON=%q\n' "${SUMMARY_JSON}"
} > "${STATE_FILE}"

echo "[suite_b_done] state=${STATE_FILE}"
echo "[suite_b_done] actual_campaign_root=${ACTUAL_CAMPAIGN_ROOT}"
echo "[suite_b_done] summary_json=${SUMMARY_JSON}"
