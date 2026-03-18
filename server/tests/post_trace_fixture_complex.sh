#!/usr/bin/env bash
set -euo pipefail

# 用法：
#   bash server/tests/post_trace_fixture_complex.sh
#   bash server/tests/post_trace_fixture_complex.sh http://127.0.0.1:8080
#   TRACE_KEY=123456 bash server/tests/post_trace_fixture_complex.sh
#
# 这条脚本专门给瀑布图做压力一点的手工联调样本：
# - 有 root -> child -> grandchild 的多层结构
# - 有同层分叉
# - 有多个 service 在时间上重叠
# - 仍然保持规模可控，便于人工肉眼检查

BASE_URL="${1:-http://127.0.0.1:8080}"
NOW_MS="$(date +%s%3N)"

TRACE_KEY="${TRACE_KEY:-$((NOW_MS / 10))}"

ROOT_SPAN_ID=$((TRACE_KEY * 10 + 1))
AUTH_SPAN_ID=$((TRACE_KEY * 10 + 2))
ORDER_SPAN_ID=$((TRACE_KEY * 10 + 3))
RISK_SPAN_ID=$((TRACE_KEY * 10 + 4))
PAYMENT_SPAN_ID=$((TRACE_KEY * 10 + 5))
INVENTORY_SPAN_ID=$((TRACE_KEY * 10 + 6))
BANK_SPAN_ID=$((TRACE_KEY * 10 + 7))
LEDGER_SPAN_ID=$((TRACE_KEY * 10 + 8))
NOTIFY_SPAN_ID=$((TRACE_KEY * 10 + 9))

TRACE_START_MS=$((NOW_MS - 9000))

ROOT_START_MS=$((TRACE_START_MS + 0))
ROOT_END_MS=$((TRACE_START_MS + 4300))

AUTH_START_MS=$((TRACE_START_MS + 40))
AUTH_END_MS=$((TRACE_START_MS + 240))

ORDER_START_MS=$((TRACE_START_MS + 260))
ORDER_END_MS=$((TRACE_START_MS + 3600))

RISK_START_MS=$((TRACE_START_MS + 420))
RISK_END_MS=$((TRACE_START_MS + 1250))

PAYMENT_START_MS=$((TRACE_START_MS + 560))
PAYMENT_END_MS=$((TRACE_START_MS + 3150))

INVENTORY_START_MS=$((TRACE_START_MS + 620))
INVENTORY_END_MS=$((TRACE_START_MS + 1780))

BANK_START_MS=$((TRACE_START_MS + 760))
BANK_END_MS=$((TRACE_START_MS + 2720))

LEDGER_START_MS=$((TRACE_START_MS + 2780))
LEDGER_END_MS=$((TRACE_START_MS + 3350))

NOTIFY_START_MS=$((TRACE_START_MS + 3450))
NOTIFY_END_MS=$((TRACE_START_MS + 3950))

post_span() {
  local payload="$1"
  curl -sS \
    -X POST "${BASE_URL}/logs/spans" \
    -H "Content-Type: application/json" \
    -d "${payload}"
  printf '\n'
}

echo "[trace-fixture-complex] POST ${BASE_URL}/logs/spans"
echo "[trace-fixture-complex] trace_key=${TRACE_KEY}"
echo "[trace-fixture-complex] root_span_id=${ROOT_SPAN_ID}"

# 先发叶子节点，再发中间层，最后发 root + trace_end=true。
# 这样能稳定覆盖“分叉 + 深度 + 并行”的展示形态。

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${AUTH_SPAN_ID},
  \"parent_span_id\": ${ROOT_SPAN_ID},
  \"start_time_ms\": ${AUTH_START_MS},
  \"end_time_ms\": ${AUTH_END_MS},
  \"name\": \"auth-service load-user-profile\",
  \"service_name\": \"auth-service\",
  \"status\": \"OK\",
  \"kind\": \"INTERNAL\",
  \"attributes\": {
    \"component\": \"profile-cache\",
    \"user.id\": \"complex-demo-user\"
  }
}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${RISK_SPAN_ID},
  \"parent_span_id\": ${ORDER_SPAN_ID},
  \"start_time_ms\": ${RISK_START_MS},
  \"end_time_ms\": ${RISK_END_MS},
  \"name\": \"risk-service evaluate-order-risk\",
  \"service_name\": \"risk-service\",
  \"status\": \"OK\",
  \"kind\": \"CLIENT\",
  \"attributes\": {
    \"risk.rule_set\": \"default-v2\",
    \"order.amount\": \"128.50\"
  }
}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${INVENTORY_SPAN_ID},
  \"parent_span_id\": ${ORDER_SPAN_ID},
  \"start_time_ms\": ${INVENTORY_START_MS},
  \"end_time_ms\": ${INVENTORY_END_MS},
  \"name\": \"inventory-service reserve-stock-batch\",
  \"service_name\": \"inventory-service\",
  \"status\": \"OK\",
  \"kind\": \"CLIENT\",
  \"attributes\": {
    \"inventory.sku_count\": \"3\",
    \"warehouse\": \"hz-a\"
  }
}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${BANK_SPAN_ID},
  \"parent_span_id\": ${PAYMENT_SPAN_ID},
  \"start_time_ms\": ${BANK_START_MS},
  \"end_time_ms\": ${BANK_END_MS},
  \"name\": \"bank-gateway authorize-payment\",
  \"service_name\": \"bank-gateway\",
  \"status\": \"ERROR\",
  \"kind\": \"CLIENT\",
  \"attributes\": {
    \"http.method\": \"POST\",
    \"http.route\": \"/bank/authorize\",
    \"error.type\": \"upstream-timeout\"
  }
}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${LEDGER_SPAN_ID},
  \"parent_span_id\": ${PAYMENT_SPAN_ID},
  \"start_time_ms\": ${LEDGER_START_MS},
  \"end_time_ms\": ${LEDGER_END_MS},
  \"name\": \"ledger-service append-payment-audit\",
  \"service_name\": \"ledger-service\",
  \"status\": \"OK\",
  \"kind\": \"CLIENT\",
  \"attributes\": {
    \"db.system\": \"sqlite\",
    \"ledger.topic\": \"payment-audit\"
  }
}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${PAYMENT_SPAN_ID},
  \"parent_span_id\": ${ORDER_SPAN_ID},
  \"start_time_ms\": ${PAYMENT_START_MS},
  \"end_time_ms\": ${PAYMENT_END_MS},
  \"name\": \"payment-service process-payment-complex\",
  \"service_name\": \"payment-service\",
  \"status\": \"ERROR\",
  \"kind\": \"CLIENT\",
  \"attributes\": {
    \"payment.channel\": \"bank-transfer\",
    \"payment.retry\": \"1\"
  }
}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${NOTIFY_SPAN_ID},
  \"parent_span_id\": ${ROOT_SPAN_ID},
  \"start_time_ms\": ${NOTIFY_START_MS},
  \"end_time_ms\": ${NOTIFY_END_MS},
  \"name\": \"notification-service publish-order-event\",
  \"service_name\": \"notification-service\",
  \"status\": \"OK\",
  \"kind\": \"PRODUCER\",
  \"attributes\": {
    \"messaging.system\": \"kafka\",
    \"messaging.destination\": \"order-events\"
  }
}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${ORDER_SPAN_ID},
  \"parent_span_id\": ${ROOT_SPAN_ID},
  \"start_time_ms\": ${ORDER_START_MS},
  \"end_time_ms\": ${ORDER_END_MS},
  \"name\": \"order-service submit-complex-order\",
  \"service_name\": \"order-service\",
  \"status\": \"ERROR\",
  \"kind\": \"SERVER\",
  \"attributes\": {
    \"http.method\": \"POST\",
    \"http.route\": \"/api/v1/orders/complex\",
    \"order.id\": \"complex-order-001\"
  }
}"

post_span "{
  \"trace_key\": ${TRACE_KEY},
  \"span_id\": ${ROOT_SPAN_ID},
  \"start_time_ms\": ${ROOT_START_MS},
  \"end_time_ms\": ${ROOT_END_MS},
  \"name\": \"gateway-service POST /api/v1/checkout-complex\",
  \"service_name\": \"gateway-service\",
  \"status\": \"OK\",
  \"kind\": \"SERVER\",
  \"trace_end\": true,
  \"attributes\": {
    \"http.method\": \"POST\",
    \"http.route\": \"/api/v1/checkout-complex\",
    \"tenant\": \"demo\"
  }
}"

echo
echo "[trace-fixture-complex] 已发送完成。"
echo "[trace-fixture-complex] 前端可直接搜索 trace_id=${TRACE_KEY}"
echo "[trace-fixture-complex] 也可以手动请求："
echo "  curl -sS -X POST ${BASE_URL}/traces/search -H 'Content-Type: application/json' -d '{\"trace_id\":\"${TRACE_KEY}\",\"page\":1,\"page_size\":20}'"
