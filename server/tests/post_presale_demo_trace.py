#!/usr/bin/env python3
import argparse
import json
import time
import urllib.error
import urllib.request
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Post the presale final-payment demo trace to LogSentinel."
    )
    parser.add_argument(
        "--base-url",
        default="http://127.0.0.1:8080",
        help="LogSentinel base URL, default: http://127.0.0.1:8080",
    )
    parser.add_argument(
        "--trace-key",
        type=int,
        default=9001001,
        help="Trace key used by the demo trace, default: 9001001",
    )
    parser.add_argument(
        "--timeout-sec",
        type=float,
        default=3.0,
        help="HTTP timeout per span request, default: 3.0",
    )
    return parser.parse_args()


def post_span(base_url: str, payload: dict[str, Any], timeout_sec: float) -> dict[str, Any]:
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(
        url=f"{base_url.rstrip('/')}/logs/spans",
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout_sec) as response:
            body = response.read().decode("utf-8", errors="replace")
            try:
                parsed_body = json.loads(body)
            except json.JSONDecodeError:
                parsed_body = {"raw_body": body}
            return {
                "http_status": response.status,
                "body": parsed_body,
            }
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"POST span failed: HTTP {exc.code}, body={body}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"POST span failed: {exc}") from exc


def build_presale_trace(trace_key: int, now_ms: int) -> list[dict[str, Any]]:
    trace_start_ms = now_ms - 10_000

    def at(offset_ms: int) -> int:
        return trace_start_ms + offset_ms

    api_gateway_span_id = trace_key * 10 + 1
    settle_span_id = trace_key * 10 + 2
    load_context_span_id = trace_key * 10 + 3
    promotion_quote_span_id = trace_key * 10 + 4
    calculate_span_id = trace_key * 10 + 5
    payment_span_id = trace_key * 10 + 6
    mark_paid_span_id = trace_key * 10 + 7

    # attributes 只记录业务侧埋点事实，不写 version_mismatch、same_source_violation 这类结论字段。
    # 这样 AI 必须结合 Trace 调用链、业务 Prompt 和字段证据自己推理，演示才不会像“把答案喂给模型”。
    spans_by_business_time = [
        {
            "trace_key": trace_key,
            "span_id": load_context_span_id,
            "parent_span_id": settle_span_id,
            "start_time_ms": at(20),
            "end_time_ms": at(45),
            "name": "load_order_and_settlement_context",
            "service_name": "order-service",
            "status": "OK",
            "attributes": {
                "config_snapshot_version": "promo_rule_v18",
                "service_instance": "order-7c9f6b8d5f-k2p9x",
                "order_id": "ORD202605070001",
                "deposit_paid_amount": "100",
                "original_final_amount": "1000",
                "campaign_id": "PRESALE_0428",
            },
        },
        {
            "trace_key": trace_key,
            "span_id": promotion_quote_span_id,
            "parent_span_id": settle_span_id,
            "start_time_ms": at(55),
            "end_time_ms": at(105),
            "name": "quote_presale_discount",
            "service_name": "promotion-service",
            "status": "OK",
            "attributes": {
                "config_snapshot_version": "promo_rule_v17",
                "service_instance": "promotion-6d4b7f5c8b-r8m2q",
                "quote_id": "QUOTE8821",
                "campaign_id": "PRESALE_0428",
                "discount_items": json.dumps(
                    [
                        {
                            "type": "deposit_expand",
                            "amount": 200,
                            "campaign_id": "PRESALE_0428",
                        },
                        {
                            "type": "coupon",
                            "amount": 100,
                            "campaign_id": "PRESALE_0428",
                        },
                    ],
                    ensure_ascii=False,
                    separators=(",", ":"),
                ),
                "discount_total": "300",
            },
        },
        {
            "trace_key": trace_key,
            "span_id": calculate_span_id,
            "parent_span_id": settle_span_id,
            "start_time_ms": at(115),
            "end_time_ms": at(140),
            "name": "calculate_final_pay_amount",
            "service_name": "order-service",
            "status": "OK",
            "attributes": {
                "config_snapshot_version": "promo_rule_v18",
                "service_instance": "order-7c9f6b8d5f-k2p9x",
                "quote_id": "QUOTE8821",
                "original_final_amount": "1000",
                "applied_discount_total": "300",
                "final_pay_amount": "700",
                "currency": "CNY",
            },
        },
        {
            "trace_key": trace_key,
            "span_id": payment_span_id,
            "parent_span_id": settle_span_id,
            "start_time_ms": at(150),
            "end_time_ms": at(210),
            "name": "create_final_payment_order",
            "service_name": "payment-service",
            "status": "OK",
            "attributes": {
                "payment_order_id": "PAY202605070001",
                "paid_amount": "700",
                "currency": "CNY",
                "payment_status": "success",
            },
        },
        {
            "trace_key": trace_key,
            "span_id": mark_paid_span_id,
            "parent_span_id": settle_span_id,
            "start_time_ms": at(220),
            "end_time_ms": at(240),
            "name": "mark_order_paid",
            "service_name": "order-service",
            "status": "OK",
            "attributes": {
                "order_id": "ORD202605070001",
                "order_status": "PAID",
                "paid_amount": "700",
            },
        },
        {
            "trace_key": trace_key,
            "span_id": settle_span_id,
            "parent_span_id": api_gateway_span_id,
            "start_time_ms": at(10),
            "end_time_ms": at(245),
            "name": "settle_presale_final_payment",
            "service_name": "order-service",
            "status": "OK",
            "attributes": {
                # 结算主编排 Span 记录当前订单实例读取到的规则快照。
                # 这只是正常排障事实，不能写成 version_mismatch 这类结论字段，否则 AI 不需要结合调用链推理。
                "config_snapshot_version": "promo_rule_v18",
                "service_instance": "order-7c9f6b8d5f-k2p9x",
                "order_id": "ORD202605070001",
                "campaign_id": "PRESALE_0428",
                "settlement_scene": "presale_final_payment",
            },
        },
        {
            "trace_key": trace_key,
            "span_id": api_gateway_span_id,
            "start_time_ms": at(0),
            "end_time_ms": at(260),
            "name": "POST /checkout/final-payment",
            "service_name": "api-gateway",
            "status": "OK",
            "trace_end": True,
            "attributes": {
                "route": "/checkout/final-payment",
                "request_channel": "mobile_app",
            },
        },
    ]

    # 演示脚本故意按“先子 Span、后父 Span、最后 trace_end”的顺序发送。
    # 这样既接近真实服务结束后上报 Span 的习惯，也能避免 root trace_end 过早触发封口。
    return spans_by_business_time


def main() -> int:
    args = parse_args()
    now_ms = int(time.time() * 1000)
    spans = build_presale_trace(args.trace_key, now_ms)

    print(f"[presale-demo] base_url={args.base_url.rstrip('/')}")
    print(f"[presale-demo] trace_key={args.trace_key}")
    print("[presale-demo] posting spans in stable child-to-root order")

    # 这个脚本只负责造一条业务 Trace，不修改 Settings。
    # Prompt、模型服务和飞书 Webhook 都应该在验收前配置并重启生效，避免现场引入冷启动配置变量。
    for index, span in enumerate(spans, start=1):
        result = post_span(args.base_url, span, args.timeout_sec)
        accepted = result["body"].get("accepted")
        print(
            f"[presale-demo] {index}/{len(spans)} "
            f"span_id={span['span_id']} service={span['service_name']} "
            f"name={span['name']} http={result['http_status']} accepted={accepted}"
        )

    print("[presale-demo] done")
    print(f"[presale-demo] Open Trace Explorer and search trace_id={args.trace_key}")
    print("[presale-demo] Wait for sealed grace / AI analysis if the trace is still pending.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
