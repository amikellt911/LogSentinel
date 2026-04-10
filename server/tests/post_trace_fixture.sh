#!/usr/bin/env bash
set -euo pipefail

# 用法：
#   bash server/tests/post_trace_fixture.sh
#   bash server/tests/post_trace_fixture.sh http://127.0.0.1:8080
#   TRACE_KEY=123456 bash server/tests/post_trace_fixture.sh
#
# 这个脚本的目标很单纯：
# 1) 不起随机数据库
# 2) 直接往“你当前正在运行的后端实例”灌一条真实 trace
# 3) 灌完后把 trace_key 打出来，方便你去前端直接搜

BASE_URL="${1:-http://127.0.0.1:8080}"
NOW_MS="$(date +%s%3N)"

# trace_key 必须是整数；默认用当前毫秒时间裁一刀，基本够本地手工测试去重。
TRACE_KEY="${TRACE_KEY:-$((NOW_MS / 10))}"

# span_id 也走纯整数，和当前 /logs/spans 入参约束保持一致。
ROOT_SPAN_ID=$((TRACE_KEY * 10 + 1))
AUTH_SPAN_ID=$((TRACE_KEY * 10 + 2))
ORDER_SPAN_ID=$((TRACE_KEY * 10 + 3))
PAYMENT_SPAN_ID=$((TRACE_KEY * 10 + 4))
INVENTORY_SPAN_ID=$((TRACE_KEY * 10 + 5))

# 这里用“接近当前时间”的绝对毫秒时间，目的是保证前端默认 24h 搜索窗口能直接看到。
TRACE_START_MS=$((NOW_MS - 8000))

ROOT_START_MS=$((TRACE_START_MS + 0))
ROOT_END_MS=$((TRACE_START_MS + 3000))

AUTH_START_MS=$((TRACE_START_MS + 50))
AUTH_END_MS=$((TRACE_START_MS + 230))

ORDER_START_MS=$((TRACE_START_MS + 300))
ORDER_END_MS=$((TRACE_START_MS + 2600))

PAYMENT_START_MS=$((TRACE_START_MS + 500))
PAYMENT_END_MS=$((TRACE_START_MS + 1900))

INVENTORY_START_MS=$((TRACE_START_MS + 1950))
INVENTORY_END_MS=$((TRACE_START_MS + 2250))

post_span() {
  local payload="$1"
  curl -sS \
    -X POST "${BASE_URL}/logs/spans" \
    -H "Content-Type: application/json" \
    -d "${payload}"
  printf '\n'
}

echo "[trace-fixture] POST ${BASE_URL}/logs/spans"
echo "[trace-fixture] trace_key=${TRACE_KEY}"
echo "[trace-fixture] root_span_id=${ROOT_SPAN_ID}"

# 先发子 span，最后发 root + trace_end=true。
# 这样手工看日志时更容易确认“前面几条都进 session 了，最后一条显式闭合 trace”。

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${AUTH_SPAN_ID},
  \"parent_span_id\": ${ROOT_SPAN_ID},
  \"start_time_ms\": ${AUTH_START_MS},
  \"end_time_ms\": ${AUTH_END_MS},
  \"name\": \"auth-service verify-jwt\",
  \"service_name\": \"auth-service\",
  \"status\": \"OK\",
  \"kind\": \"INTERNAL\",
  \"attributes\": {
    \"component\": \"jwt\",
    \"user.id\": \"demo-user\"
  }
}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${PAYMENT_SPAN_ID},
  \"parent_span_id\": ${ORDER_SPAN_ID},
  \"start_time_ms\": ${PAYMENT_START_MS},
  \"end_time_ms\": ${PAYMENT_END_MS},
  \"name\": \"payment-service charge-card-timeout\",
  \"service_name\": \"payment-service\",
  \"status\": \"ERROR\",
  \"kind\": \"CLIENT\",
  \"attributes\": {
    \"http.method\": \"POST\",
    \"http.route\": \"/api/v1/payments\",
    \"error.type\": \"timeout\"
  }
}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${INVENTORY_SPAN_ID},
  \"parent_span_id\": ${ORDER_SPAN_ID},
  \"start_time_ms\": ${INVENTORY_START_MS},
  \"end_time_ms\": ${INVENTORY_END_MS},
  \"name\": \"inventory-service reserve-stock\",
  \"service_name\": \"inventory-service\",
  \"status\": \"OK\",
  \"kind\": \"CLIENT\",
  \"attributes\": {
    \"db.system\": \"sqlite\",
    \"inventory.sku\": \"demo-sku-001\"
  }
}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${ORDER_SPAN_ID},
  \"parent_span_id\": ${ROOT_SPAN_ID},
  \"start_time_ms\": ${ORDER_START_MS},
  \"end_time_ms\": ${ORDER_END_MS},
  \"name\": \"order-service submit-order\",
  \"service_name\": \"order-service\",
  \"status\": \"ERROR\",
  \"kind\": \"SERVER\",
  \"attributes\": {
    \"http.method\": \"POST\",
    \"http.route\": \"/api/v1/orders\",
    \"order.id\": \"demo-order-001\"
  }
}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${ROOT_SPAN_ID},
  \"start_time_ms\": ${ROOT_START_MS},
  \"end_time_ms\": ${ROOT_END_MS},
  \"name\": \"gateway-service POST /api/v1/checkout\",
  \"service_name\": \"gateway-service\",
  \"status\": \"OK\",
  \"kind\": \"SERVER\",
  \"trace_end\": true,
  \"attributes\": {
    \"http.method\": \"POST\",
    \"http.route\": \"/api/v1/checkout\",
    \"tenant\": \"demo\"
  }
}"

echo
echo "[trace-fixture] 已发送完成。"
echo "[trace-fixture] 前端可直接搜索 trace_id=${TRACE_KEY}"
echo "[trace-fixture] 也可以手动请求："
echo "  curl -sS -X POST ${BASE_URL}/traces/search -H 'Content-Type: application/json' -d '{\"trace_id\":\"${TRACE_KEY}\",\"page\":1,\"page_size\":20}'"
