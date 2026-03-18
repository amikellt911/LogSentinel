#!/usr/bin/env bash
set -euo pipefail

# 用法：
#   bash server/tests/post_trace_fixture_success.sh
#   bash server/tests/post_trace_fixture_success.sh http://127.0.0.1:8080
#   TRACE_KEY=123456 bash server/tests/post_trace_fixture_success.sh
#
# 这个脚本专门造一条“全成功、低风险、结构更简单”的 trace，
# 用来补第二轮联调，验证：
# 1) 列表/详情仍然正常
# 2) analysis 会不会走到更低风险
# 3) 瀑布图在纯成功链路下也正常

BASE_URL="${1:-http://127.0.0.1:8080}"
NOW_MS="$(date +%s%3N)"

TRACE_KEY="${TRACE_KEY:-$((NOW_MS / 10))}"

ROOT_SPAN_ID=$((TRACE_KEY * 10 + 1))
AUTH_SPAN_ID=$((TRACE_KEY * 10 + 2))
ORDER_SPAN_ID=$((TRACE_KEY * 10 + 3))
PAYMENT_SPAN_ID=$((TRACE_KEY * 10 + 4))

TRACE_START_MS=$((NOW_MS - 6000))

ROOT_START_MS=$((TRACE_START_MS + 0))
ROOT_END_MS=$((TRACE_START_MS + 2200))

AUTH_START_MS=$((TRACE_START_MS + 40))
AUTH_END_MS=$((TRACE_START_MS + 180))

ORDER_START_MS=$((TRACE_START_MS + 220))
ORDER_END_MS=$((TRACE_START_MS + 1850))

PAYMENT_START_MS=$((TRACE_START_MS + 420))
PAYMENT_END_MS=$((TRACE_START_MS + 950))

post_span() {
  local payload="$1"
  curl -sS \
    -X POST "${BASE_URL}/logs/spans" \
    -H "Content-Type: application/json" \
    -d "${payload}"
  printf '\n'
}

echo "[trace-fixture-success] POST ${BASE_URL}/logs/spans"
echo "[trace-fixture-success] trace_key=${TRACE_KEY}"
echo "[trace-fixture-success] root_span_id=${ROOT_SPAN_ID}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${AUTH_SPAN_ID},
  \"parent_span_id\": ${ROOT_SPAN_ID},
  \"start_time_ms\": ${AUTH_START_MS},
  \"end_time_ms\": ${AUTH_END_MS},
  \"name\": \"auth-service verify-session\",
  \"service_name\": \"auth-service\",
  \"status\": \"OK\",
  \"kind\": \"INTERNAL\",
  \"attributes\": {
    \"component\": \"session\",
    \"user.id\": \"demo-user-success\"
  }
}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${PAYMENT_SPAN_ID},
  \"parent_span_id\": ${ORDER_SPAN_ID},
  \"start_time_ms\": ${PAYMENT_START_MS},
  \"end_time_ms\": ${PAYMENT_END_MS},
  \"name\": \"payment-service authorize-card\",
  \"service_name\": \"payment-service\",
  \"status\": \"OK\",
  \"kind\": \"CLIENT\",
  \"attributes\": {
    \"http.method\": \"POST\",
    \"http.route\": \"/api/v1/payments/authorize\",
    \"payment.channel\": \"mock-card\"
  }
}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${ORDER_SPAN_ID},
  \"parent_span_id\": ${ROOT_SPAN_ID},
  \"start_time_ms\": ${ORDER_START_MS},
  \"end_time_ms\": ${ORDER_END_MS},
  \"name\": \"order-service submit-order-success\",
  \"service_name\": \"order-service\",
  \"status\": \"OK\",
  \"kind\": \"SERVER\",
  \"attributes\": {
    \"http.method\": \"POST\",
    \"http.route\": \"/api/v1/orders\",
    \"order.id\": \"demo-order-success-001\"
  }
}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${ROOT_SPAN_ID},
  \"start_time_ms\": ${ROOT_START_MS},
  \"end_time_ms\": ${ROOT_END_MS},
  \"name\": \"gateway-service POST /api/v1/checkout-success\",
  \"service_name\": \"gateway-service\",
  \"status\": \"OK\",
  \"kind\": \"SERVER\",
  \"trace_end\": true,
  \"attributes\": {
    \"http.method\": \"POST\",
    \"http.route\": \"/api/v1/checkout-success\",
    \"tenant\": \"demo\"
  }
}"

echo
echo "[trace-fixture-success] 已发送完成。"
echo "[trace-fixture-success] 前端可直接搜索 trace_id=${TRACE_KEY}"
echo "[trace-fixture-success] 也可以手动请求："
echo "  curl -sS -X POST ${BASE_URL}/traces/search -H 'Content-Type: application/json' -d '{\"trace_id\":\"${TRACE_KEY}\",\"page\":1,\"page_size\":20}'"
