#!/usr/bin/env python3
import argparse
import json
import random
import time
import urllib.error
import urllib.request
from typing import Any


TERMINAL_AI_STATUSES = {
    "completed",
    "failed_primary",
    "failed_both",
    "skipped_manual",
    "skipped_circuit",
}


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
        default=None,
        help="Trace key used by the demo trace. Omit it to generate a fresh key automatically.",
    )
    parser.add_argument(
        "--timeout-sec",
        type=float,
        default=3.0,
        help="HTTP timeout per span request, default: 3.0",
    )
    parser.add_argument(
        "--wait-ai",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Poll trace detail until AI status reaches a terminal state, default: enabled",
    )
    parser.add_argument(
        "--analysis-timeout-sec",
        type=float,
        default=180.0,
        help="Max seconds to wait for AI analysis after posting spans, default: 180.0",
    )
    parser.add_argument(
        "--poll-interval-sec",
        type=float,
        default=1.0,
        help="Seconds between trace detail polls, default: 1.0",
    )
    return parser.parse_args()


def generate_trace_key(now_ms: int) -> int:
    # 自动生成的 trace_key 使用时间戳低位叠加随机数，避免稳定性测试反复撞上历史 trace。
    # 如果验收现场需要固定搜索入口，仍然可以显式传 --trace-key 9001001 覆盖这里。
    return 920_000_000 + (now_ms % 10_000_000) * 100 + random.randint(0, 99)


def http_json(base_url: str,
              path: str,
              method: str = "GET",
              payload: Any | None = None,
              timeout_sec: float = 3.0) -> tuple[int, dict[str, Any]]:
    body = None
    if payload is not None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(
        url=f"{base_url.rstrip('/')}{path}",
        data=body,
        headers={"Content-Type": "application/json"},
        method=method,
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout_sec) as response:
            raw_body = response.read().decode("utf-8", errors="replace")
            if not raw_body:
                return response.status, {}
            parsed_body = json.loads(raw_body)
            if isinstance(parsed_body, dict):
                return response.status, parsed_body
            return response.status, {"raw_body": parsed_body}
    except urllib.error.HTTPError as exc:
        raw_body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"{method} {path} failed: HTTP {exc.code}, body={raw_body}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"{method} {path} failed: {exc}") from exc


def post_span(base_url: str, payload: dict[str, Any], timeout_sec: float) -> dict[str, Any]:
    status_code, response_body = http_json(base_url,
                                           "/logs/spans",
                                           method="POST",
                                           payload=payload,
                                           timeout_sec=timeout_sec)
    return {
        "http_status": status_code,
        "body": response_body,
    }


def wait_trace_ai_done(base_url: str,
                       trace_key: int,
                       timeout_sec: float,
                       poll_interval_sec: float,
                       request_timeout_sec: float) -> tuple[dict[str, Any], float]:
    trace_id = str(trace_key)
    deadline = time.monotonic() + timeout_sec
    last_status = "<missing>"

    # 这里统计的是“用户发完 trace 到前端能查到 AI 终态”的端到端等待时间。
    # 它包含 sealed grace、调度、proxy 调用、模型响应和落库，比单独测 provider HTTP 更接近验收现场体感。
    wait_started = time.monotonic()
    while time.monotonic() < deadline:
        try:
            _, detail = http_json(base_url,
                                  f"/traces/{trace_id}",
                                  timeout_sec=request_timeout_sec)
        except RuntimeError as exc:
            message = str(exc)
            if "HTTP 404" not in message and "HTTP 503" not in message:
                raise
            last_status = message
            time.sleep(poll_interval_sec)
            continue

        ai_status = str(detail.get("ai_status") or "")
        if ai_status:
            last_status = ai_status
        print(f"[presale-demo] poll trace_id={trace_id} ai_status={last_status}")
        if ai_status in TERMINAL_AI_STATUSES:
            return detail, time.monotonic() - wait_started
        time.sleep(poll_interval_sec)

    raise TimeoutError(
        f"AI analysis did not reach terminal status within {timeout_sec:.1f}s; "
        f"last_status={last_status}"
    )


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
    trace_key = args.trace_key if args.trace_key is not None else generate_trace_key(now_ms)
    spans = build_presale_trace(trace_key, now_ms)

    print(f"[presale-demo] base_url={args.base_url.rstrip('/')}")
    print(f"[presale-demo] trace_key={trace_key}")
    print("[presale-demo] posting spans in stable child-to-root order")

    # 这个脚本只负责造一条业务 Trace，不修改 Settings。
    # Prompt、模型服务和飞书 Webhook 都应该在验收前配置并重启生效，避免现场引入冷启动配置变量。
    total_started = time.monotonic()
    for index, span in enumerate(spans, start=1):
        result = post_span(args.base_url, span, args.timeout_sec)
        accepted = result["body"].get("accepted")
        print(
            f"[presale-demo] {index}/{len(spans)} "
            f"span_id={span['span_id']} service={span['service_name']} "
            f"name={span['name']} http={result['http_status']} accepted={accepted}"
        )

    print("[presale-demo] done")
    if args.wait_ai:
        detail, ai_wait_sec = wait_trace_ai_done(args.base_url,
                                                trace_key,
                                                args.analysis_timeout_sec,
                                                args.poll_interval_sec,
                                                args.timeout_sec)
        total_elapsed_sec = time.monotonic() - total_started
        analysis = detail.get("analysis") if isinstance(detail.get("analysis"), dict) else {}
        print(
            "[presale-demo] ai_done "
            f"trace_id={trace_key} "
            f"ai_status={detail.get('ai_status')} "
            f"risk_level={detail.get('risk_level')} "
            f"ai_wait_sec={ai_wait_sec:.2f} "
            f"total_elapsed_sec={total_elapsed_sec:.2f}"
        )
        if analysis:
            print(f"[presale-demo] summary={analysis.get('summary', '')}")
            print(f"[presale-demo] root_cause={analysis.get('root_cause', '')}")
            print(f"[presale-demo] solution={analysis.get('solution', '')}")
        elif detail.get("ai_error"):
            print(f"[presale-demo] ai_error={detail.get('ai_error')}")
    else:
        print("[presale-demo] wait_ai=false, skip polling AI status.")
    print(f"[presale-demo] Open Trace Explorer and search trace_id={trace_key}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
