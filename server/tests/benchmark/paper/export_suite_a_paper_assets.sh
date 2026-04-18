#!/usr/bin/env bash

set -euo pipefail

# 这条脚本导出 Suite A 论文资产。
# 它默认读取 run_suite_a_paper.sh 写下的 state 文件；如果没有 state，也可以通过环境变量覆盖 summary 路径。

STATE_FILE="${BENCHMARK_STATE_DIR:-/tmp/logsentinel_paper_latest}/suite_a.env"
if [[ -f "${STATE_FILE}" ]]; then
  # state 文件只由同目录运行脚本生成，里面保存本轮正式实验的 summary 路径。
  # 这里 source 它，是为了让远端导出阶段不再手工复制长路径。
  # shellcheck disable=SC1090
  source "${STATE_FILE}"
fi

DATE_TAG="${BENCHMARK_ASSET_DATE:-$(date +%Y%m%d)}"
ASSET_DIR="${SUITE_A_ASSET_DIR:-${HOME}/paper_assets/${DATE_TAG}/suite_a}"
MAIN_SUMMARY_JSON="${SUITE_A_MAIN_SUMMARY_JSON:-}"
BUFFER_SUMMARY_JSON="${SUITE_A_BUFFER_SUMMARY_JSON:-}"
RUN_ROOT="${SUITE_A_RUN_ROOT:-}"

if [[ -z "${RUN_ROOT}" ]]; then
  RUN_ROOT="$(find /tmp -maxdepth 1 -type d -name 'suite_a_paper_final-*' -printf '%T@ %p\n' 2>/dev/null | sort -nr | awk 'NR==1{print $2}')"
fi
if [[ -z "${MAIN_SUMMARY_JSON}" && -n "${RUN_ROOT}" ]]; then
  MAIN_SUMMARY_JSON="${RUN_ROOT}/main_vs_cmp_summary.json"
fi
if [[ -z "${BUFFER_SUMMARY_JSON}" && -n "${RUN_ROOT}" ]]; then
  BUFFER_SUMMARY_JSON="${RUN_ROOT}/buffer_compare_summary.json"
fi

[[ -f "${MAIN_SUMMARY_JSON}" ]] || { echo "Fatal Error: missing Suite A main summary: ${MAIN_SUMMARY_JSON}" >&2; exit 1; }
[[ -f "${BUFFER_SUMMARY_JSON}" ]] || { echo "Fatal Error: missing Suite A buffer summary: ${BUFFER_SUMMARY_JSON}" >&2; exit 1; }

mkdir -p "${ASSET_DIR}/summary" "${ASSET_DIR}/cases" "${ASSET_DIR}/diagnostics"

cp "${MAIN_SUMMARY_JSON}" "${ASSET_DIR}/summary/main_vs_cmp_summary.json"
cp "${BUFFER_SUMMARY_JSON}" "${ASSET_DIR}/summary/buffer_compare_summary.json"

# result.json 是论文复核需要的最小 case 资产；SQLite 和完整日志默认不搬，避免资产包膨胀。
if [[ -n "${RUN_ROOT}" && -d "${RUN_ROOT}" ]]; then
  while IFS= read -r result_json; do
    rel="${result_json#${RUN_ROOT}/}"
    mkdir -p "${ASSET_DIR}/cases/$(dirname "${rel}")"
    cp "${result_json}" "${ASSET_DIR}/cases/${rel}"
  done < <(find "${RUN_ROOT}" -name result.json -type f | sort)
fi

python3 - "${ASSET_DIR}/summary/main_vs_cmp_summary.json" "${ASSET_DIR}/summary/buffer_compare_summary.json" "${ASSET_DIR}/diagnostics/diagnostics.log" <<'PY'
import json
import sys

main_path, buffer_path, out_path = sys.argv[1:4]

def load(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)

def fmt(value):
    if value is None:
        return "NA"
    return f"{float(value):.4f}"

main = load(main_path)
buffer = load(buffer_path)
lines = []

lines.append("[suite_a_main_vs_cmp]")
for item in main.get("by_load", []):
    load = item.get("load", {})
    name = load.get("name", item.get("load_name", "unknown"))
    delta = item.get("delta_main_vs_cmp", {})
    main_variant = item.get("main", {})
    cmp_variant = item.get("cmp", {})
    lines.append(
        f"{name}: cmp_visible={fmt(cmp_variant.get('visible_completion_rate_at_stop'))} "
        f"main_visible={fmt(main_variant.get('visible_completion_rate_at_stop'))} "
        f"delta_visible={fmt(delta.get('visible_completion_rate_at_stop'))} "
        f"delta_drain_ms={fmt(delta.get('drain_tail_ms'))}"
    )
overall = main.get("overall", {})
lines.append(
    f"overall: worst_visible_delta={fmt(overall.get('worst_visible_delta_main_vs_cmp'))} "
    f"worst_drain_delta_ms={fmt(overall.get('worst_drain_delta_main_vs_cmp'))}"
)

lines.append("")
lines.append("[suite_a_buffer_compare]")
for item in buffer.get("top_candidates", []):
    lines.append(
        f"candidate={item.get('candidate_name', item.get('name', 'unknown'))} "
        f"visible={fmt(item.get('visible_completion_rate_at_stop'))} "
        f"drain_ms={fmt(item.get('drain_tail_ms'))}"
    )

with open(out_path, "w", encoding="utf-8") as fh:
    fh.write("\n".join(lines) + "\n")
PY

echo "[suite_a_exported] asset_dir=${ASSET_DIR}"
du -sh "${ASSET_DIR}"
sed -n '1,120p' "${ASSET_DIR}/diagnostics/diagnostics.log"
find "${ASSET_DIR}" -type f | sort
