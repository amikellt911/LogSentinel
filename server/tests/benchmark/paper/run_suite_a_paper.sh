#!/usr/bin/env bash

set -euo pipefail

# 这条脚本固定 Suite A 的论文正式运行口径。
# 远端只需要运行本脚本，不再手工拼 main/cmp、绑核和 output-summary，避免长命令复制时把参数抄串。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
STATE_DIR="${BENCHMARK_STATE_DIR:-/tmp/logsentinel_paper_latest}"
STATE_FILE="${STATE_DIR}/suite_a.env"
RUN_ROOT="${SUITE_A_PAPER_RUN_ROOT:-/tmp/suite_a_paper_final-${TIMESTAMP}}"
LOG_ROOT="${RUN_ROOT}/logs"

default_main_server_bin() {
  if [[ -x "${ROOT_DIR}/server/build-main/LogSentinel" ]]; then
    printf '%s\n' "${ROOT_DIR}/server/build-main/LogSentinel"
    return
  fi
  printf '%s\n' "${ROOT_DIR}/server/build/LogSentinel"
}

default_cmp_server_bin() {
  if [[ -x "${ROOT_DIR}/server/build-cmp/LogSentinel" ]]; then
    printf '%s\n' "${ROOT_DIR}/server/build-cmp/LogSentinel"
    return
  fi
  printf '%s\n' "${ROOT_DIR}/server/build-cmp/LogSentinel"
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

run_logged() {
  local name="$1"
  shift
  local log_path="${LOG_ROOT}/${name}.log"
  echo "[step] ${name} log=${log_path}"
  "$@" 2>&1 | tee "${log_path}"
}

MAIN_SERVER_BIN="${BENCHMARK_MAIN_SERVER_BIN:-$(default_main_server_bin)}"
CMP_SERVER_BIN="${BENCHMARK_CMP_SERVER_BIN:-$(default_cmp_server_bin)}"
CORE_BASE_OFFSET="${BENCHMARK_CORE_BASE_OFFSET:-$(detect_core_base_offset)}"
SENDER_CPUSET="${SUITE_A_SENDER_CPUSET:-$(build_cpuset "${CORE_BASE_OFFSET}" 4)}"
SERVER_CPUSET="${SUITE_A_SERVER_CPUSET:-$(build_cpuset "$((CORE_BASE_OFFSET + 4))" 12)}"
MAIN_SUMMARY_JSON="${SUITE_A_MAIN_SUMMARY_JSON:-${RUN_ROOT}/main_vs_cmp_summary.json}"
BUFFER_SUMMARY_JSON="${SUITE_A_BUFFER_SUMMARY_JSON:-${RUN_ROOT}/buffer_compare_summary.json}"

mkdir -p "${LOG_ROOT}" "${STATE_DIR}"

[[ -x "${MAIN_SERVER_BIN}" ]] || { echo "Fatal Error: main server bin is not executable: ${MAIN_SERVER_BIN}" >&2; exit 1; }
[[ -x "${CMP_SERVER_BIN}" ]] || { echo "Fatal Error: cmp server bin is not executable: ${CMP_SERVER_BIN}" >&2; exit 1; }

cat <<EOF
[suite_a_paper] run_root=${RUN_ROOT}
[suite_a_paper] sender_cpuset=${SENDER_CPUSET}
[suite_a_paper] server_cpuset=${SERVER_CPUSET}
[suite_a_paper] main_server_bin=${MAIN_SERVER_BIN}
[suite_a_paper] cmp_server_bin=${CMP_SERVER_BIN}
EOF

# 主对比回答“引入生命周期保护后，主线性能代价是否可接受”。
run_logged "suite_a_main_vs_cmp_16c" \
  env \
    SUITE_A_COMPARE_ROOT="${RUN_ROOT}/main_vs_cmp_16c" \
    SUITE_A_SERVER_CPUSET="${SERVER_CPUSET}" \
  taskset -c "${SENDER_CPUSET}" \
  bash "${ROOT_DIR}/server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp_16c.sh" \
    --main-server-bin "${MAIN_SERVER_BIN}" \
    --cmp-server-bin "${CMP_SERVER_BIN}" \
    --output-summary "${MAIN_SUMMARY_JSON}"

# buffer 辅助归因回答“写入缓冲本身是否是主要收益来源”。
run_logged "suite_a_buffer_compare_16c" \
  env \
    SUITE_A_COMPARE_ROOT="${RUN_ROOT}/buffer_compare_16c" \
    SUITE_A_SERVER_CPUSET="${SERVER_CPUSET}" \
  taskset -c "${SENDER_CPUSET}" \
  bash "${ROOT_DIR}/server/tests/benchmark/suite_a/run_suite_a_buffer_compare_16c.sh" \
    --server-bin "${MAIN_SERVER_BIN}" \
    --output-summary "${BUFFER_SUMMARY_JSON}"

{
  printf 'SUITE_A_RUN_ROOT=%q\n' "${RUN_ROOT}"
  printf 'SUITE_A_MAIN_SUMMARY_JSON=%q\n' "${MAIN_SUMMARY_JSON}"
  printf 'SUITE_A_BUFFER_SUMMARY_JSON=%q\n' "${BUFFER_SUMMARY_JSON}"
} > "${STATE_FILE}"

echo "[suite_a_done] state=${STATE_FILE}"
echo "[suite_a_done] main_summary=${MAIN_SUMMARY_JSON}"
echo "[suite_a_done] buffer_summary=${BUFFER_SUMMARY_JSON}"
