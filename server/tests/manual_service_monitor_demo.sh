#!/usr/bin/env bash
set -euo pipefail

# 用法：
#   bash server/tests/manual_service_monitor_demo.sh
#   bash server/tests/manual_service_monitor_demo.sh http://127.0.0.1:18080
#
# 这个脚本只做手工联调，不碰随机数据库和自动起服务：
# 1) 复用现有复杂 trace fixture 往当前后端实例灌一条多服务异常链路
# 2) 等服务监控快照发布 tick 和 AI 摘要回填
# 3) 直接拉 /service-monitor/runtime，方便你肉眼看原型页吃到的 JSON

BASE_URL="${1:-http://127.0.0.1:18080}"
WAIT_SECONDS="${WAIT_SECONDS:-6}"

echo "[service-monitor-demo] step1: post complex trace fixture -> ${BASE_URL}"
bash server/tests/post_trace_fixture_complex.sh "${BASE_URL}"

echo
echo "[service-monitor-demo] step2: wait ${WAIT_SECONDS}s for OnTick publish and AI summary backfill"
sleep "${WAIT_SECONDS}"

echo
echo "[service-monitor-demo] step3: GET ${BASE_URL}/service-monitor/runtime"
curl -sS "${BASE_URL}/service-monitor/runtime" | python3 -m json.tool
