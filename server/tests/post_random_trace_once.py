#!/usr/bin/env python3
import argparse
import json
import random
import time
import urllib.error
import urllib.request


def post_span(base_url: str, payload: dict) -> None:
    data = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        url=f"{base_url}/logs/spans",
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=3) as response:
        body = response.read().decode("utf-8", errors="replace")
        print(body)


def build_complex_trace(trace_key: int, now_ms: int) -> list[dict]:
    root_span_id = trace_key * 10 + 1
    auth_span_id = trace_key * 10 + 2
    order_span_id = trace_key * 10 + 3
    risk_span_id = trace_key * 10 + 4
    payment_span_id = trace_key * 10 + 5
    inventory_span_id = trace_key * 10 + 6
    bank_span_id = trace_key * 10 + 7
    ledger_span_id = trace_key * 10 + 8
    notify_span_id = trace_key * 10 + 9

    trace_start_ms = now_ms - 9000

    def span_time(offset_start: int, offset_end: int) -> tuple[int, int]:
        return trace_start_ms + offset_start, trace_start_ms + offset_end

    auth_start_ms, auth_end_ms = span_time(40, 240)
    order_start_ms, order_end_ms = span_time(260, 3600)
    risk_start_ms, risk_end_ms = span_time(420, 1250)
    payment_start_ms, payment_end_ms = span_time(560, 3150)
    inventory_start_ms, inventory_end_ms = span_time(620, 1780)
    bank_start_ms, bank_end_ms = span_time(760, 2720)
    ledger_start_ms, ledger_end_ms = span_time(2780, 3350)
    notify_start_ms, notify_end_ms = span_time(3450, 3950)
    root_start_ms, root_end_ms = span_time(0, 4300)

    # 这里继续复用之前复杂 fixture 的拓扑：
    # 多服务、多层级、并行分叉、局部错误都保留，只把 trace_key 变成时间种子的随机值。
    return [
        {
            "trace_key": trace_key,
            "span_id": auth_span_id,
            "parent_span_id": root_span_id,
            "start_time_ms": auth_start_ms,
            "end_time_ms": auth_end_ms,
            "name": "auth-service load-user-profile",
            "service_name": "auth-service",
            "status": "OK",
            "kind": "INTERNAL",
            "attributes": {
                "component": "profile-cache",
                "user.id": f"random-user-{trace_key}",
            },
        },
        {
            "trace_key": trace_key,
            "span_id": risk_span_id,
            "parent_span_id": order_span_id,
            "start_time_ms": risk_start_ms,
            "end_time_ms": risk_end_ms,
            "name": "risk-service evaluate-order-risk",
            "service_name": "risk-service",
            "status": "OK",
            "kind": "CLIENT",
            "attributes": {
                "risk.rule_set": "default-v2",
                "order.amount": "128.50",
            },
        },
        {
            "trace_key": trace_key,
            "span_id": inventory_span_id,
            "parent_span_id": order_span_id,
            "start_time_ms": inventory_start_ms,
            "end_time_ms": inventory_end_ms,
            "name": "inventory-service reserve-stock-batch",
            "service_name": "inventory-service",
            "status": "OK",
            "kind": "CLIENT",
            "attributes": {
                "inventory.sku_count": "3",
                "warehouse": "hz-a",
            },
        },
        {
            "trace_key": trace_key,
            "span_id": bank_span_id,
            "parent_span_id": payment_span_id,
            "start_time_ms": bank_start_ms,
            "end_time_ms": bank_end_ms,
            "name": "bank-gateway authorize-payment",
            "service_name": "bank-gateway",
            "status": "ERROR",
            "kind": "CLIENT",
            "attributes": {
                "http.method": "POST",
                "http.route": "/bank/authorize",
                "error.type": "upstream-timeout",
            },
        },
        {
            "trace_key": trace_key,
            "span_id": ledger_span_id,
            "parent_span_id": payment_span_id,
            "start_time_ms": ledger_start_ms,
            "end_time_ms": ledger_end_ms,
            "name": "ledger-service append-payment-audit",
            "service_name": "ledger-service",
            "status": "OK",
            "kind": "CLIENT",
            "attributes": {
                "db.system": "sqlite",
                "ledger.topic": "payment-audit",
            },
        },
        {
            "trace_key": trace_key,
            "span_id": payment_span_id,
            "parent_span_id": order_span_id,
            "start_time_ms": payment_start_ms,
            "end_time_ms": payment_end_ms,
            "name": "payment-service process-payment-complex",
            "service_name": "payment-service",
            "status": "ERROR",
            "kind": "CLIENT",
            "attributes": {
                "payment.channel": "bank-transfer",
                "payment.retry": "1",
            },
        },
        {
            "trace_key": trace_key,
            "span_id": notify_span_id,
            "parent_span_id": root_span_id,
            "start_time_ms": notify_start_ms,
            "end_time_ms": notify_end_ms,
            "name": "notification-service publish-order-event",
            "service_name": "notification-service",
            "status": "OK",
            "kind": "PRODUCER",
            "attributes": {
                "messaging.system": "kafka",
                "messaging.destination": "order-events",
            },
        },
        {
            "trace_key": trace_key,
            "span_id": order_span_id,
            "parent_span_id": root_span_id,
            "start_time_ms": order_start_ms,
            "end_time_ms": order_end_ms,
            "name": "order-service submit-complex-order",
            "service_name": "order-service",
            "status": "ERROR",
            "kind": "SERVER",
            "attributes": {
                "http.method": "POST",
                "http.route": "/api/v1/orders/complex",
                "order.id": f"complex-order-{trace_key}",
            },
        },
        {
            "trace_key": trace_key,
            "span_id": root_span_id,
            "start_time_ms": root_start_ms,
            "end_time_ms": root_end_ms,
            "name": "gateway-service POST /api/v1/checkout-complex",
            "service_name": "gateway-service",
            "status": "OK",
            "kind": "SERVER",
            "trace_end": True,
            "attributes": {
                "http.method": "POST",
                "http.route": "/api/v1/checkout-complex",
                "tenant": "demo",
            },
        },
    ]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="发送 1 条随机 trace_key 的复杂 trace，用于服务监控和 TraceExplorer 联调。"
    )
    parser.add_argument(
        "base_url",
        nargs="?",
        default="http://127.0.0.1:8080",
        help="后端基础地址，默认 http://127.0.0.1:8080",
    )
    parser.add_argument(
        "--seed-ms",
        type=int,
        default=None,
        help="可选，手动指定毫秒种子；默认取当前时间毫秒。",
    )
    args = parser.parse_args()

    seed_ms = args.seed_ms if args.seed_ms is not None else int(time.time() * 1000)
    rng = random.Random(seed_ms)
    # trace_key 还是整数，但现在不再简单直接等于时间戳，而是用时间做种子再随机一刀。
    # 这样每次运行基本都不一样，又仍然能通过 seed_ms 复现。
    trace_key = rng.randint(100_000_000, 999_999_999)
    now_ms = int(time.time() * 1000)

    print(f"[post-random-trace-once] base_url={args.base_url}")
    print(f"[post-random-trace-once] seed_ms={seed_ms}")
    print(f"[post-random-trace-once] trace_key={trace_key}")

    try:
        for payload in build_complex_trace(trace_key, now_ms):
            post_span(args.base_url, payload)
    except urllib.error.HTTPError as exc:
        print(f"[post-random-trace-once] http error: status={exc.code}")
        print(exc.read().decode("utf-8", errors="replace"))
        return 1
    except urllib.error.URLError as exc:
        print(f"[post-random-trace-once] url error: {exc}")
        return 1

    print()
    print("[post-random-trace-once] 已发送完成。")
    print(f"[post-random-trace-once] 前端可直接搜索 trace_id={trace_key}")
    print(
        "[post-random-trace-once] 也可以手动请求："
        f" curl -sS -X POST {args.base_url}/traces/search "
        f"-H 'Content-Type: application/json' "
        f"-d '{{\"trace_id\":\"{trace_key}\",\"page\":1,\"page_size\":20}}'"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
