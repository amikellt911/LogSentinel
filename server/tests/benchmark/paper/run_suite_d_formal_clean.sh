#!/usr/bin/env bash

set -euo pipefail

# Suite D fixed-load 正式复跑脚本。
# 每个 backend case 完成后由 Python runner 删除 SQLite DB，并等待 cooldown，避免 overlay/DB 文件污染后续点位。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ASSET_DATE="${BENCHMARK_ASSET_DATE:-$(date +%Y%m%d)}"

default_main_server_bin() {
  if [[ -x "${ROOT_DIR}/server/build-main/LogSentinel" ]]; then
    printf '%s\n' "${ROOT_DIR}/server/build-main/LogSentinel"
    return
  fi
  printf '%s\n' "${ROOT_DIR}/server/build/LogSentinel"
}

SERVER_BIN="${BENCHMARK_MAIN_SERVER_BIN:-$(default_main_server_bin)}"
TAG="${SUITE_D_FORMAL_TAG:-optimized_d512_f1024}"
RUN_ROOT_PREFIX="${SUITE_D_FORMAL_RUN_ROOT:-/tmp/suite_d_formal_${TAG}}"
SUMMARY_JSON="${SUITE_D_FORMAL_SUMMARY_JSON:-/tmp/suite_d_formal_${TAG}_summary.json}"
ASSET_DIR="${SUITE_D_FORMAL_ASSET_DIR:-${HOME}/paper_assets/${ASSET_DATE}/suite_d_formal/${TAG}}"
DIAGNOSTICS_LOG="${ASSET_DIR}/diagnostics.log"

BACKEND_CORE_POINTS="${SUITE_D_BACKEND_CORE_POINTS:-16,20,24}"
WRK_CPUSET="${SUITE_D_WRK_CPUSET:-0-3}"
BACKEND_CORE_OFFSET="${SUITE_D_BACKEND_CORE_OFFSET:-4}"
TRACE_MAX_DISPATCH_PER_TICK="${SUITE_D_TRACE_MAX_DISPATCH_PER_TICK:-512}"
TRACE_PRIMARY_FLUSH_SPAN_THRESHOLD="${SUITE_D_TRACE_PRIMARY_FLUSH_SPAN_THRESHOLD:-1024}"
TRACE_PRIMARY_FLUSH_INTERVAL_MS="${SUITE_D_TRACE_PRIMARY_FLUSH_INTERVAL_MS:-5}"
COOLDOWN_SEC="${SUITE_D_COOLDOWN_SEC:-60}"

mkdir -p "${ASSET_DIR}/summary" "${ASSET_DIR}/cases" "${ASSET_DIR}/logs"
[[ -x "${SERVER_BIN}" ]] || { echo "fatal: server bin is not executable: ${SERVER_BIN}" >&2; exit 1; }

cat <<EOF
[suite_d_formal_clean] tag=${TAG}
[suite_d_formal_clean] server_bin=${SERVER_BIN}
[suite_d_formal_clean] backend_core_points=${BACKEND_CORE_POINTS}
[suite_d_formal_clean] wrk_cpuset=${WRK_CPUSET}
[suite_d_formal_clean] backend_core_offset=${BACKEND_CORE_OFFSET}
[suite_d_formal_clean] dispatch_per_tick=${TRACE_MAX_DISPATCH_PER_TICK}
[suite_d_formal_clean] flush_threshold=${TRACE_PRIMARY_FLUSH_SPAN_THRESHOLD}
[suite_d_formal_clean] flush_interval_ms=${TRACE_PRIMARY_FLUSH_INTERVAL_MS}
[suite_d_formal_clean] cooldown_sec=${COOLDOWN_SEC}
[suite_d_formal_clean] summary=${SUMMARY_JSON}
[suite_d_formal_clean] asset_dir=${ASSET_DIR}
EOF

env \
  SUITE_D_WRK_CPUSET="${WRK_CPUSET}" \
  SUITE_D_BACKEND_CORE_OFFSET="${BACKEND_CORE_OFFSET}" \
  SUITE_D_BACKEND_CORE_POINTS="${BACKEND_CORE_POINTS}" \
  SUITE_D_SCALING_ROOT="${RUN_ROOT_PREFIX}" \
  SUITE_D_OUTPUT_SUMMARY="${SUMMARY_JSON}" \
  SUITE_D_CLEANUP_SQLITE_DB=1 \
  SUITE_D_COOLDOWN_SEC="${COOLDOWN_SEC}" \
  bash "${ROOT_DIR}/server/tests/benchmark/suite_d/run_suite_d_scaling_fixed_load_24c.sh" \
    --server-bin "${SERVER_BIN}" \
    --trace-max-dispatch-per-tick "${TRACE_MAX_DISPATCH_PER_TICK}" \
    --trace-primary-flush-span-threshold "${TRACE_PRIMARY_FLUSH_SPAN_THRESHOLD}" \
    --trace-primary-flush-interval-ms "${TRACE_PRIMARY_FLUSH_INTERVAL_MS}" \
    "$@"

cp "${SUMMARY_JSON}" "${ASSET_DIR}/summary/scaling_summary.json"

"${PYTHON_BIN}" "${ROOT_DIR}/server/tests/benchmark/paper/diagnose_suite_d_fixed_load.py" \
  --summary "${SUMMARY_JSON}" \
  --tail-lines "${SUITE_D_DIAGNOSTIC_TAIL_LINES:-40}" \
  | tee "${DIAGNOSTICS_LOG}"

ACTUAL_ROOT="$("${PYTHON_BIN}" - "${SUMMARY_JSON}" <<'PY'
import json
import sys
with open(sys.argv[1], "r", encoding="utf-8") as fh:
    print(json.load(fh)["actual_scaling_root"])
PY
)"

# SQLite DB 已在每个 case 后删除；这里只复制 result/log，保证论文资产足够复核但不会继续撑爆 /tmp。
find "${ACTUAL_ROOT}" -type f \( -name 'result.json' -o -name 'server.log' -o -name 'wrk.log' \) | sort | while read -r file_path; do
  rel_path="${file_path#${ACTUAL_ROOT}/}"
  mkdir -p "${ASSET_DIR}/cases/$(dirname "${rel_path}")"
  cp "${file_path}" "${ASSET_DIR}/cases/${rel_path}"
done

du -sh "${ASSET_DIR}"
echo "[suite_d_formal_clean_done] summary=${ASSET_DIR}/summary/scaling_summary.json"
echo "[suite_d_formal_clean_done] diagnostics=${DIAGNOSTICS_LOG}"
echo "[suite_d_formal_clean_done] actual_root=${ACTUAL_ROOT}"
