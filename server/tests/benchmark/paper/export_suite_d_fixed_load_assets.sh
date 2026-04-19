#!/usr/bin/env bash

set -euo pipefail

# 这条脚本只导出 Suite D fixed-load 补充实验资产。
# 它适合在新 32 vCPU 实例上单独跑 fixed90 后使用，不依赖旧 Suite D connection/default scaling 结果目录。

DATE_TAG="${BENCHMARK_ASSET_DATE:-$(date +%Y%m%d)}"
ASSET_DIR="${SUITE_D_FIXED_ASSET_DIR:-${HOME}/paper_assets/${DATE_TAG}/suite_d_fixed90}"
FIXED90_SCALING_SUMMARY_JSON="${SUITE_D_FIXED90_SCALING_SUMMARY_JSON:-/tmp/suite_d_scaling_fixed90_24backend_summary.json}"

[[ -f "${FIXED90_SCALING_SUMMARY_JSON}" ]] || {
  echo "Fatal Error: missing Suite D fixed-load summary: ${FIXED90_SCALING_SUMMARY_JSON}" >&2
  exit 1
}

# fixed-load summary 自带 actual_scaling_root。
# 导出脚本从 JSON 读取真实目录，避免远端手动复制 timestamp 路径时被终端换行打断。
ACTUAL_ROOT="$(python3 - "${FIXED90_SCALING_SUMMARY_JSON}" <<'PY'
import json
import sys
with open(sys.argv[1], "r", encoding="utf-8") as fh:
    print(json.load(fh)["actual_scaling_root"])
PY
)"
[[ -d "${ACTUAL_ROOT}" ]] || {
  echo "Fatal Error: missing Suite D fixed-load root: ${ACTUAL_ROOT}" >&2
  exit 1
}

mkdir -p "${ASSET_DIR}/summary" "${ASSET_DIR}/cases/scaling_fixed90" "${ASSET_DIR}/diagnostics"
cp "${FIXED90_SCALING_SUMMARY_JSON}" "${ASSET_DIR}/summary/scaling_summary_fixed90.json"

rm -rf "${ASSET_DIR}/cases/scaling_fixed90"
mkdir -p "${ASSET_DIR}/cases/scaling_fixed90"
while IFS= read -r result_json; do
  rel="${result_json#${ACTUAL_ROOT}/}"
  mkdir -p "${ASSET_DIR}/cases/scaling_fixed90/$(dirname "${rel}")"
  cp "${result_json}" "${ASSET_DIR}/cases/scaling_fixed90/${rel}"
done < <(find "${ACTUAL_ROOT}" -name result.json -type f | sort)

# 诊断文件只摘 fixed-load 曲线最常用字段。
# 完整字段仍保留在 summary/result JSON，诊断文件只用于快速贴表和人工复核。
python3 - "${FIXED90_SCALING_SUMMARY_JSON}" "${ASSET_DIR}/diagnostics/diagnostics_scaling_fixed90.log" <<'PY'
import json
import sys

summary_path, out_path = sys.argv[1:3]
with open(summary_path, "r", encoding="utf-8") as fh:
    payload = json.load(fh)

def fmt(value):
    if value is None:
        return "NA"
    return f"{float(value):.4f}"

lines = ["[suite_d_scaling_fixed90]"]
for item in payload.get("by_backend_cores", []):
    qps = item.get("requests_per_sec")
    if qps is None:
        qps = item.get("wrk_metrics", {}).get("requests_per_sec")
    lines.append(
        f"backend_cores={item.get('backend_cores')} "
        f"server_cpuset={item.get('server_cpuset')} "
        f"wrk_cpuset={item.get('wrk_cpuset')} "
        f"connections={item.get('connections')} "
        f"online={fmt(item.get('online_completed_traces_per_sec'))} "
        f"ratio={fmt(item.get('online_completion_ratio'))} "
        f"drain_ms={item.get('drain_tail_ms')} "
        f"qps={fmt(qps)}"
    )
overall = payload.get("overall", {})
lines.append(
    f"overall: best_backend_cores={overall.get('best_backend_cores')} "
    f"best_online={fmt(overall.get('best_online_completed_traces_per_sec'))}"
)

with open(out_path, "w", encoding="utf-8") as fh:
    fh.write("\n".join(lines) + "\n")
PY

echo "[suite_d_fixed90_exported] asset_dir=${ASSET_DIR}"
du -sh "${ASSET_DIR}"
sed -n '1,120p' "${ASSET_DIR}/diagnostics/diagnostics_scaling_fixed90.log"
find "${ASSET_DIR}" -type f | sort
