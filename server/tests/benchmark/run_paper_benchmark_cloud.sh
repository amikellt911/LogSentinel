#!/usr/bin/env bash

set -euo pipefail

# 这条总入口只负责把已经冻结的论文 benchmark wrapper 串起来。
# 它不重新发明参数矩阵；子实验仍然由各自 suite 的 frozen wrapper 维护，
# 这样后面只需要复核单项脚本，不会出现“一键脚本”和“正式脚本”两套口径。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

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

  # 容器里经常只给 160-191 这种高位核区间。
  # 这里默认取第一个可用核作为基址，后面所有 frozen cpuset 都整体平移，
  # 避免继续手工把 0-23 改成 160-183。
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

usage() {
  cat <<'USAGE'
Usage:
  bash server/tests/benchmark/run_paper_benchmark_cloud.sh \
    --main-server-bin /root/work/LogSentinel/server/build-main/LogSentinel \
    --cmp-server-bin /root/work/LogSentinel-old/server/build-cmp/LogSentinel

Key options:
  --main-server-bin PATH       当前主版本 LogSentinel 二进制
  --cmp-server-bin PATH        历史对照版本 LogSentinel 二进制
  --core-base-offset N         cpuset 起始核；不传时自动读 cgroup，云机常见值是 160
  --campaign-root PATH         总实验目录前缀；脚本会自动追加时间后缀
  --suite-b-seeds CSV          Suite B campaign seeds
  --suite-d-connections CSV    Suite D 连接数确认搜索候选
  --suite-d-repeats N          Suite D 连接数确认搜索复跑次数
  --include-d-topology-search  顺手跑 Suite D 24 核拓扑搜索
  --skip-suite-a               跳过 Suite A 两个 16 核入口
  --skip-suite-b               跳过 Suite B 16 核 campaign
  --skip-suite-d               跳过 Suite D 24 核连接确认和主曲线
  --dry-run                    只打印将要执行的命令，不真正压测

Environment overrides:
  BENCHMARK_CORE_BASE_OFFSET / BENCHMARK_CAMPAIGN_ROOT / PYTHON_BIN
USAGE
}

MAIN_SERVER_BIN="${BENCHMARK_MAIN_SERVER_BIN:-$(default_main_server_bin)}"
CMP_SERVER_BIN="${BENCHMARK_CMP_SERVER_BIN:-$(default_cmp_server_bin)}"
CORE_BASE_OFFSET="${BENCHMARK_CORE_BASE_OFFSET:-$(detect_core_base_offset)}"
CAMPAIGN_ROOT_PREFIX="${BENCHMARK_CAMPAIGN_ROOT:-/tmp/logsentinel_paper_benchmark_cloud}"
SUITE_B_SEEDS="${BENCHMARK_SUITE_B_SEEDS:-20260415,20260416,20260417,20260418,20260419}"
SUITE_D_CONNECTIONS="${BENCHMARK_SUITE_D_CONNECTIONS:-90,108,120}"
SUITE_D_REPEATS="${BENCHMARK_SUITE_D_REPEATS:-3}"

RUN_SUITE_A=1
RUN_SUITE_B=1
RUN_SUITE_D=1
RUN_D_TOPOLOGY_SEARCH=0
DRY_RUN=0

while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --main-server-bin)
      MAIN_SERVER_BIN="$2"
      shift 2
      ;;
    --cmp-server-bin)
      CMP_SERVER_BIN="$2"
      shift 2
      ;;
    --core-base-offset)
      CORE_BASE_OFFSET="$2"
      shift 2
      ;;
    --campaign-root)
      CAMPAIGN_ROOT_PREFIX="$2"
      shift 2
      ;;
    --suite-b-seeds)
      SUITE_B_SEEDS="$2"
      shift 2
      ;;
    --suite-d-connections)
      SUITE_D_CONNECTIONS="$2"
      shift 2
      ;;
    --suite-d-repeats)
      SUITE_D_REPEATS="$2"
      shift 2
      ;;
    --include-d-topology-search)
      RUN_D_TOPOLOGY_SEARCH=1
      shift
      ;;
    --skip-suite-a)
      RUN_SUITE_A=0
      shift
      ;;
    --skip-suite-b)
      RUN_SUITE_B=0
      shift
      ;;
    --skip-suite-d)
      RUN_SUITE_D=0
      shift
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Fatal Error: unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
CAMPAIGN_ROOT="${CAMPAIGN_ROOT_PREFIX}-${TIMESTAMP}"
LOG_ROOT="${CAMPAIGN_ROOT}/logs"

SUITE_A_SENDER_CPUSET="$(build_cpuset "${CORE_BASE_OFFSET}" 4)"
SUITE_A_SERVER_CPUSET="$(build_cpuset "$((CORE_BASE_OFFSET + 4))" 12)"
SUITE_B_SENDER_CPUSET="$(build_cpuset "${CORE_BASE_OFFSET}" 3)"
SUITE_B_SERVER_CPUSET="$(build_cpuset "$((CORE_BASE_OFFSET + 3))" 13)"
SUITE_D_WRK_CPUSET_4C="$(build_cpuset "${CORE_BASE_OFFSET}" 4)"
SUITE_D_SERVER_CPUSET_20C="$(build_cpuset "$((CORE_BASE_OFFSET + 4))" 20)"

print_command() {
  printf '+'
  printf ' %q' "$@"
  printf '\n'
}

require_runtime() {
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    return
  fi

  command -v taskset >/dev/null
  command -v "${PYTHON_BIN}" >/dev/null

  if [[ "${RUN_SUITE_A}" -eq 1 || "${RUN_SUITE_B}" -eq 1 || "${RUN_SUITE_D}" -eq 1 ]]; then
    [[ -x "${MAIN_SERVER_BIN}" ]] || {
      echo "Fatal Error: main server bin is not executable: ${MAIN_SERVER_BIN}" >&2
      exit 1
    }
  fi
  if [[ "${RUN_SUITE_A}" -eq 1 ]]; then
    [[ -x "${CMP_SERVER_BIN}" ]] || {
      echo "Fatal Error: cmp server bin is not executable: ${CMP_SERVER_BIN}" >&2
      exit 1
    }
  fi
}

run_logged() {
  local name="$1"
  shift
  local log_path="${LOG_ROOT}/${name}.log"

  echo "[step] ${name} log=${log_path}"
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    print_command "$@"
    return
  fi

  if "$@" 2>&1 | tee "${log_path}"; then
    echo "[ok] ${name}"
    return
  fi

  local status=$?
  echo "[fail] ${name} status=${status} log=${log_path}" >&2
  exit "${status}"
}

run_quiet_json() {
  local name="$1"
  local summary_json="$2"
  shift 2
  local log_path="${LOG_ROOT}/${name}.log"

  echo "[step] ${name} log=${log_path} summary=${summary_json}"
  if [[ "${DRY_RUN}" -eq 1 ]]; then
    print_command "$@"
    return
  fi

  # Suite B campaign 会打印完整 summary JSON，直接刷屏不利于云机复制结果。
  # 所以 stdout 全量落日志，终端只回显几行聚合摘要。
  if "$@" > "${log_path}" 2>&1; then
    echo "[ok] ${name}"
    "${PYTHON_BIN}" - "${summary_json}" <<'PY'
import json
import sys
from pathlib import Path

summary_path = Path(sys.argv[1])
payload = json.loads(summary_path.read_text(encoding="utf-8"))
aggregate = payload.get("aggregate", {})
print(f"[suite_b] summary={summary_path}")

correctness = aggregate.get("correctness_by_case", {})
for case_id in sorted(correctness):
    metrics = correctness[case_id]
    completeness = metrics.get("trace_completeness_rate", {}).get("median")
    pollution = metrics.get("trace_pollution_rate", {}).get("median")
    duplicate = metrics.get("duplicate_persistence_rate", {}).get("median")
    print(
        "[suite_b_case] "
        f"case={case_id} "
        f"completeness_median={completeness} "
        f"pollution_median={pollution} "
        f"duplicate_median={duplicate}"
    )

p95 = aggregate.get("ingest_p95_latency_delta_by_profile", {})
for profile in sorted(p95):
    absolute = p95[profile].get("absolute_ms", {}).get("median")
    relative = p95[profile].get("relative", {}).get("median")
    print(
        "[suite_b_p95_delta] "
        f"profile={profile} "
        f"absolute_ms_median={absolute} "
        f"relative_median={relative}"
    )
PY
    return
  fi

  local status=$?
  echo "[fail] ${name} status=${status} log=${log_path}" >&2
  tail -120 "${log_path}" >&2 || true
  exit "${status}"
}

require_runtime
mkdir -p "${LOG_ROOT}"

cat <<EOF
[campaign] root=${CAMPAIGN_ROOT}
[campaign] core_base_offset=${CORE_BASE_OFFSET}
[campaign] main_server_bin=${MAIN_SERVER_BIN}
[campaign] cmp_server_bin=${CMP_SERVER_BIN}
[campaign] suite_a sender=${SUITE_A_SENDER_CPUSET} server=${SUITE_A_SERVER_CPUSET}
[campaign] suite_b sender=${SUITE_B_SENDER_CPUSET} server=${SUITE_B_SERVER_CPUSET}
[campaign] suite_d wrk=${SUITE_D_WRK_CPUSET_4C} server=${SUITE_D_SERVER_CPUSET_20C}
EOF

if [[ "${RUN_SUITE_A}" -eq 1 ]]; then
  run_logged "suite_a_main_vs_cmp_16c" \
    env \
      SUITE_A_COMPARE_ROOT="${CAMPAIGN_ROOT}/suite_a_main_vs_cmp_16c" \
      SUITE_A_SERVER_CPUSET="${SUITE_A_SERVER_CPUSET}" \
    taskset -c "${SUITE_A_SENDER_CPUSET}" \
    bash "${ROOT_DIR}/server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp_16c.sh" \
      --main-server-bin "${MAIN_SERVER_BIN}" \
      --cmp-server-bin "${CMP_SERVER_BIN}"

  run_logged "suite_a_buffer_compare_16c" \
    env \
      SUITE_A_COMPARE_ROOT="${CAMPAIGN_ROOT}/suite_a_buffer_compare_16c" \
      SUITE_A_SERVER_CPUSET="${SUITE_A_SERVER_CPUSET}" \
    taskset -c "${SUITE_A_SENDER_CPUSET}" \
    bash "${ROOT_DIR}/server/tests/benchmark/suite_a/run_suite_a_buffer_compare_16c.sh" \
      --server-bin "${MAIN_SERVER_BIN}"
fi

if [[ "${RUN_SUITE_B}" -eq 1 ]]; then
  SUITE_B_SUMMARY="${CAMPAIGN_ROOT}/suite_b_campaign_summary.json"
  run_quiet_json "suite_b_campaign_16c" "${SUITE_B_SUMMARY}" \
    taskset -c "${SUITE_B_SENDER_CPUSET}" \
    "${PYTHON_BIN}" "${ROOT_DIR}/server/tests/benchmark/suite_b/run_suite_b_campaign.py" \
      --campaign-root "${CAMPAIGN_ROOT}/suite_b_campaign_16c" \
      --output-summary "${SUITE_B_SUMMARY}" \
      --seeds "${SUITE_B_SEEDS}" \
      --port-base 19580 \
      --port-stride 20 \
      --server-bin "${MAIN_SERVER_BIN}" \
      --server-cpuset "${SUITE_B_SERVER_CPUSET}" \
      --server-io-threads 5 \
      --worker-threads 16 \
      --dispatch-worker-threads 4 \
      --worker-queue-size 8192 \
      --trace-capacity 12 \
      --trace-token-limit 0 \
      --trace-sweep-interval-ms 100 \
      --trace-idle-timeout-ms 800 \
      --trace-max-dispatch-per-tick 128 \
      --trace-buffered-span-limit 8192 \
      --trace-active-session-limit 2048 \
      --trace-count 10 \
      --spans-per-trace 8 \
      --send-workers 8 \
      --disable-ai \
      --disable-webhook \
      --no-auto-start-proxy \
      --sender-profiles clean_baseline,mixed_realistic,late_replay_stress \
      --trace-lifecycle-profiles protected,minimal
fi

if [[ "${RUN_SUITE_D}" -eq 1 ]]; then
  if [[ "${RUN_D_TOPOLOGY_SEARCH}" -eq 1 ]]; then
    run_logged "suite_d_topology_search_24c" \
      env \
        SUITE_D_SEARCH_ROOT="${CAMPAIGN_ROOT}/suite_d_topology_search_24c" \
        SUITE_D_WRK_CPUSET="${SUITE_D_WRK_CPUSET_4C}" \
        SUITE_D_SERVER_CPUSET="${SUITE_D_SERVER_CPUSET_20C}" \
      bash "${ROOT_DIR}/server/tests/benchmark/suite_d/run_suite_d_topology_search_24c.sh" \
        --server-bin "${MAIN_SERVER_BIN}"
  fi

  run_logged "suite_d_connection_search_24c" \
    env \
      SUITE_D_SEARCH_ROOT="${CAMPAIGN_ROOT}/suite_d_connection_search_24c" \
      SUITE_D_SERVER_BIN="${MAIN_SERVER_BIN}" \
      SUITE_D_CORE_BASE_OFFSET="${CORE_BASE_OFFSET}" \
      SUITE_D_CONNECTION_SET="${SUITE_D_CONNECTIONS}" \
      SUITE_D_REPEATS="${SUITE_D_REPEATS}" \
    bash "${ROOT_DIR}/server/tests/benchmark/suite_d/run_suite_d_connection_search_24c.sh"

  run_logged "suite_d_scaling_24c" \
    env \
      SUITE_D_SCALING_ROOT="${CAMPAIGN_ROOT}/suite_d_scaling_24c" \
      SUITE_D_CORE_BASE_OFFSET="${CORE_BASE_OFFSET}" \
    bash "${ROOT_DIR}/server/tests/benchmark/suite_d/run_suite_d_scaling_24c.sh" \
      --server-bin "${MAIN_SERVER_BIN}" \
      --trace-sweep-interval-ms 20 \
      --trace-max-dispatch-per-tick 256
fi

echo "[campaign_done] root=${CAMPAIGN_ROOT}"
