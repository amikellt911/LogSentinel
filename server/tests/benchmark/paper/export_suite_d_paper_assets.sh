#!/usr/bin/env bash

set -euo pipefail

# 这条脚本导出 Suite D 论文资产。
# 它只搬 final-aware summary、scaling summary 和 result.json，不搬 SQLite、wal/shm 或完整日志。

STATE_FILE="${BENCHMARK_STATE_DIR:-/tmp/logsentinel_paper_latest}/suite_d.env"
if [[ -f "${STATE_FILE}" ]]; then
  # state 文件来自 run_suite_d_paper.sh，里面记录 connection/scaling 两个真实结果目录。
  # shellcheck disable=SC1090
  source "${STATE_FILE}"
fi

latest_dir() {
  local pattern="$1"
  find /tmp -maxdepth 1 -type d -name "${pattern}" -printf '%T@ %p\n' 2>/dev/null | sort -nr | awk 'NR==1{print $2}'
}

DATE_TAG="${BENCHMARK_ASSET_DATE:-$(date +%Y%m%d)}"
ASSET_DIR="${SUITE_D_ASSET_DIR:-${HOME}/paper_assets/${DATE_TAG}/suite_d}"
RUN_ROOT="${SUITE_D_RUN_ROOT:-}"
CONNECTION_ROOT="${SUITE_D_CONNECTION_ROOT:-}"
CONNECTION_SUMMARY_JSON="${SUITE_D_CONNECTION_SUMMARY_JSON:-}"
SCALING_ROOT="${SUITE_D_SCALING_ROOT:-}"
SCALING_SUMMARY_JSON="${SUITE_D_SCALING_SUMMARY_JSON:-}"

if [[ -z "${RUN_ROOT}" ]]; then
  RUN_ROOT="$(latest_dir 'suite_d_paper_final-*')"
fi
if [[ -z "${CONNECTION_ROOT}" && -n "${RUN_ROOT}" ]]; then
  CONNECTION_ROOT="${RUN_ROOT}/connection_search"
fi
if [[ -z "${CONNECTION_SUMMARY_JSON}" && -n "${CONNECTION_ROOT}" ]]; then
  CONNECTION_SUMMARY_JSON="${CONNECTION_ROOT}/summary.json"
fi
if [[ -z "${SCALING_SUMMARY_JSON}" && -n "${RUN_ROOT}" ]]; then
  SCALING_SUMMARY_JSON="${RUN_ROOT}/scaling_summary.json"
fi
if [[ -z "${SCALING_ROOT}" && -f "${SCALING_SUMMARY_JSON}" ]]; then
  SCALING_ROOT="$(python3 - "${SCALING_SUMMARY_JSON}" <<'PY'
import json
import sys
with open(sys.argv[1], "r", encoding="utf-8") as fh:
    print(json.load(fh)["actual_scaling_root"])
PY
)"
fi

[[ -d "${CONNECTION_ROOT}" ]] || { echo "Fatal Error: missing Suite D connection root: ${CONNECTION_ROOT}" >&2; exit 1; }
[[ -f "${CONNECTION_SUMMARY_JSON}" ]] || { echo "Fatal Error: missing Suite D connection summary: ${CONNECTION_SUMMARY_JSON}" >&2; exit 1; }
[[ -f "${SCALING_SUMMARY_JSON}" ]] || { echo "Fatal Error: missing Suite D scaling summary: ${SCALING_SUMMARY_JSON}" >&2; exit 1; }
[[ -d "${SCALING_ROOT}" ]] || { echo "Fatal Error: missing Suite D scaling root: ${SCALING_ROOT}" >&2; exit 1; }

mkdir -p "${ASSET_DIR}/summary" "${ASSET_DIR}/cases/connection_search" "${ASSET_DIR}/cases/scaling" "${ASSET_DIR}/diagnostics"
cp "${CONNECTION_SUMMARY_JSON}" "${ASSET_DIR}/summary/connection_search_summary.json"
cp "${SCALING_SUMMARY_JSON}" "${ASSET_DIR}/summary/scaling_summary.json"

# 连接搜索的 r*_c*.json 就是单 case result；scaling 的 result.json 按 cores_xx/run_01 保存。
while IFS= read -r result_json; do
  cp "${result_json}" "${ASSET_DIR}/cases/connection_search/$(basename "${result_json}")"
done < <(find "${CONNECTION_ROOT}" -maxdepth 1 -name 'r*_c*.json' -type f | sort)

while IFS= read -r result_json; do
  rel="${result_json#${SCALING_ROOT}/}"
  mkdir -p "${ASSET_DIR}/cases/scaling/$(dirname "${rel}")"
  cp "${result_json}" "${ASSET_DIR}/cases/scaling/${rel}"
done < <(find "${SCALING_ROOT}" -name result.json -type f | sort)

python3 - "${ASSET_DIR}/summary/connection_search_summary.json" "${ASSET_DIR}/summary/scaling_summary.json" "${ASSET_DIR}/diagnostics/diagnostics.log" <<'PY'
import json
import sys

connection_path, scaling_path, out_path = sys.argv[1:4]

def load(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)

def fmt(value):
    if value is None:
        return "NA"
    return f"{float(value):.4f}"

connection = load(connection_path)
scaling = load(scaling_path)
lines = []

lines.append("[suite_d_connection_search_finalaware]")
for item in connection.get("by_connections", []):
    lines.append(
        f"connections={item.get('connections')} "
        f"final_ratio={fmt(item.get('median_final_completion_ratio'))} "
        f"final={fmt(item.get('median_final_trace_summary'))} "
        f"online={fmt(item.get('median_online_completed_traces_per_sec'))} "
        f"drain_ms={item.get('median_drain_tail_ms')}"
    )
best = connection.get("best", {})
lines.append(
    f"best: connections={best.get('connections')} "
    f"final_ratio={fmt(best.get('median_final_completion_ratio'))} "
    f"final={fmt(best.get('median_final_trace_summary'))}"
)

lines.append("")
lines.append("[suite_d_scaling_24c]")
for item in scaling.get("by_total_cores", []):
    lines.append(
        f"total_cores={item.get('total_cores')} "
        f"online={fmt(item.get('online_completed_traces_per_sec'))} "
        f"ratio={fmt(item.get('online_completion_ratio'))} "
        f"drain_ms={item.get('drain_tail_ms')}"
    )
overall = scaling.get("overall", {})
lines.append(
    f"overall: best_total_cores={overall.get('best_total_cores')} "
    f"best_online={fmt(overall.get('best_online_completed_traces_per_sec'))}"
)

with open(out_path, "w", encoding="utf-8") as fh:
    fh.write("\n".join(lines) + "\n")
PY

echo "[suite_d_exported] asset_dir=${ASSET_DIR}"
du -sh "${ASSET_DIR}"
sed -n '1,160p' "${ASSET_DIR}/diagnostics/diagnostics.log"
find "${ASSET_DIR}" -type f | sort
