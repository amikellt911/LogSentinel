#!/usr/bin/env bash

set -euo pipefail

# 这条脚本导出 Suite B 论文资产。
# 它把 campaign summary、每轮 matrix summary 和 result.json 复制出来，并生成一个小诊断文件。

STATE_FILE="${BENCHMARK_STATE_DIR:-/tmp/logsentinel_paper_latest}/suite_b.env"
if [[ -f "${STATE_FILE}" ]]; then
  # state 文件来自 run_suite_b_paper.sh，优先使用它记录的真实 campaign root。
  # shellcheck disable=SC1090
  source "${STATE_FILE}"
fi

latest_dir() {
  local pattern="$1"
  find /tmp -maxdepth 1 -type d -name "${pattern}" -printf '%T@ %p\n' 2>/dev/null | sort -nr | awk 'NR==1{print $2}'
}

DATE_TAG="${BENCHMARK_ASSET_DATE:-$(date +%Y%m%d)}"
ASSET_DIR="${SUITE_B_ASSET_DIR:-${HOME}/paper_assets/${DATE_TAG}/suite_b}"
CAMPAIGN_ROOT="${SUITE_B_CAMPAIGN_ROOT:-}"
SUMMARY_JSON="${SUITE_B_SUMMARY_JSON:-}"
SUMMARY_NAME="${SUITE_B_CAMPAIGN_SUMMARY_NAME:-campaign_summary_x10.json}"
DIAG_NAME="${SUITE_B_DIAGNOSTICS_NAME:-diagnostics_x10.log}"

if [[ -z "${CAMPAIGN_ROOT}" ]]; then
  CAMPAIGN_ROOT="$(latest_dir 'suite_b_paper_x10-*')"
fi
if [[ -z "${CAMPAIGN_ROOT}" ]]; then
  CAMPAIGN_ROOT="$(latest_dir 'suite_b_campaign_remote16_x10-*')"
fi
if [[ -z "${SUMMARY_JSON}" ]]; then
  SUMMARY_JSON="/tmp/suite_b_campaign_remote16_x10_summary.json"
fi

[[ -d "${CAMPAIGN_ROOT}" ]] || { echo "Fatal Error: missing Suite B campaign root: ${CAMPAIGN_ROOT}" >&2; exit 1; }
[[ -f "${SUMMARY_JSON}" ]] || { echo "Fatal Error: missing Suite B campaign summary: ${SUMMARY_JSON}" >&2; exit 1; }

mkdir -p "${ASSET_DIR}/summary" "${ASSET_DIR}/cases" "${ASSET_DIR}/diagnostics"
cp "${SUMMARY_JSON}" "${ASSET_DIR}/summary/${SUMMARY_NAME}"

# 每个 seed 的 matrix summary 很小，全部保留，方便后面核对 run-level 波动。
while IFS= read -r summary_json; do
  run_dir="$(basename "$(dirname "${summary_json}")")"
  cp "${summary_json}" "${ASSET_DIR}/summary/${run_dir}_summary.json"
done < <(find "${CAMPAIGN_ROOT}" -name summary.json -type f | sort)

# 单 case result.json 是复核 correctness 的最小资产；manifest、SQLite 和完整 server.log 默认不搬。
while IFS= read -r result_json; do
  rel="${result_json#${CAMPAIGN_ROOT}/}"
  mkdir -p "${ASSET_DIR}/cases/$(dirname "${rel}")"
  cp "${result_json}" "${ASSET_DIR}/cases/${rel}"
done < <(find "${CAMPAIGN_ROOT}" -name result.json -type f | sort)

python3 - "${ASSET_DIR}/summary/${SUMMARY_NAME}" "${ASSET_DIR}/diagnostics/${DIAG_NAME}" <<'PY'
import json
import sys

summary_path, out_path = sys.argv[1:3]
with open(summary_path, "r", encoding="utf-8") as fh:
    payload = json.load(fh)
agg = payload["aggregate"]

def fmt(value):
    if value is None:
        return "NA"
    return f"{float(value):.4f}"

lines = []
lines.append("[suite_b_campaign_x10]")
lines.append(f"actual_campaign_root={payload.get('actual_campaign_root')}")
lines.append(f"total_runs={payload.get('total_runs')}")

for case_id, metrics in sorted(agg["correctness_by_case"].items()):
    comp = metrics["trace_completeness_rate"]
    pol = metrics["trace_pollution_rate"]
    dup = metrics["duplicate_persistence_rate"]
    lines.append(
        f"{case_id}: completeness_median={fmt(comp.get('median'))} "
        f"completeness_min={fmt(comp.get('min'))} "
        f"pollution_median={fmt(pol.get('median'))} "
        f"duplicate_median={fmt(dup.get('median'))}"
    )

lines.append("")
lines.append("[sqlite_unique_constraint_fail_count_by_case]")
for case_id, stats in sorted(agg["sqlite_unique_constraint_fail_count_by_case"].items()):
    lines.append(
        f"{case_id}: median={fmt(stats.get('median'))} "
        f"mean={fmt(stats.get('mean'))} max={fmt(stats.get('max'))}"
    )

lines.append("")
lines.append("[ingest_p95_latency_delta_by_profile]")
for profile, fields in sorted(agg["ingest_p95_latency_delta_by_profile"].items()):
    abs_stats = fields["absolute_ms"]
    rel_stats = fields["relative"]
    lines.append(
        f"{profile}: absolute_median_ms={fmt(abs_stats.get('median'))} "
        f"absolute_max_ms={fmt(abs_stats.get('max'))} "
        f"relative_median={fmt(rel_stats.get('median'))}"
    )

with open(out_path, "w", encoding="utf-8") as fh:
    fh.write("\n".join(lines) + "\n")
PY

echo "[suite_b_exported] asset_dir=${ASSET_DIR}"
du -sh "${ASSET_DIR}"
sed -n '1,120p' "${ASSET_DIR}/diagnostics/${DIAG_NAME}"
find "${ASSET_DIR}" -type f | sort
