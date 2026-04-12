#!/usr/bin/env python3
"""
GLM 最小手工联调脚本。

这个脚本故意只做两件事：
1. probe-proxy：直接调用 Python proxy 的 /analyze/trace/glm，验证 key/model/json/usage。
2. send-spans：往后端发送一条完整 trace，方便前端或数据库观察真实 Trace 主链效果。

它不负责自动改 Settings、更不负责自动重启后端。
因为 ai_provider/model/api_key 当前是冷启动语义，真正的“后端端到端走 glm”仍然需要你先配置并重启后端。
"""

from __future__ import annotations

import argparse
import json
import time
from typing import Any, Dict, List

import requests


def build_demo_trace_text() -> str:
    """
    这里直接构造一段可读的 trace 文本，用于直打 proxy。
    既然 proxy 路由真正吃的是“最终渲染好的 prompt”，那这里没必要伪装成 span JSON，
    只要能稳定给模型提供端到端故障上下文即可。
    """
    return (
        "Trace Summary:\n"
        "- gateway-service POST /api/v1/checkout\n"
        "- payment-service process-payment\n"
        "- bank-gateway authorize-payment\n\n"
        "Observed anomalies:\n"
        "- bank-gateway span status=ERROR\n"
        "- error.type=upstream-timeout\n"
        "- payment-service retried once but still failed\n"
        "- gateway root span completed with downstream payment failure\n"
    )


def build_demo_prompt(trace_text: str) -> str:
    """
    这里沿用当前 Trace 主链的四字段结构。
    既然 GLM 只保证 json_object，不保证服务端 schema 强约束，
    那 prompt 里就必须把字段和输出约束讲死。
    """
    return (
        "You are a distributed tracing analysis expert for LogSentinel.\n"
        "Return ONLY one JSON object with exactly these fields:\n"
        "summary, risk_level, root_cause, solution.\n"
        "risk_level must be one of: critical, error, warning, info, safe, unknown.\n"
        "Do not wrap the JSON in markdown.\n\n"
        "<trace_context>\n"
        f"{trace_text}"
        "\n</trace_context>\n"
    )


def call_glm_proxy(proxy_base_url: str,
                   api_key: str,
                   model: str,
                   timeout_sec: float,
                   provider_timeout_sec: float) -> int:
    """
    这个动作只验证 Python proxy -> GLM 的链路。
    它不经过 C++ 后端，所以可以先把“模型调用本身通不通”单独验掉。
    这里把“本地等待多久”和“provider 允许上游等多久”拆开，
    因为我们要验证的是 proxy 能不能在上游超时后，及时把结构化 TIMEOUT 回给调用方，
    而不是让本地 requests 和 provider 上游同时卡死在一个时刻。
    """
    trace_text = build_demo_trace_text()
    prompt = build_demo_prompt(trace_text)
    url = f"{proxy_base_url.rstrip('/')}/analyze/trace/glm"
    payload = {
        "trace_text": trace_text,
        "prompt": prompt,
        "api_key": api_key,
        "model": model,
        "timeout_ms": int(provider_timeout_sec * 1000),
    }

    print(f"[glm-probe] proxy_url={url}")
    print(f"[glm-probe] model={model}")
    print(f"[glm-probe] provider_timeout_sec={provider_timeout_sec}")
    print(f"[glm-probe] local_timeout_sec={timeout_sec}")

    response = requests.post(url, json=payload, timeout=timeout_sec)
    print(f"[glm-probe] http_status={response.status_code}")

    try:
        data = response.json()
    except ValueError:
        print("[glm-probe] 返回不是 JSON：")
        print(response.text)
        return 1

    print(json.dumps(data, indent=2, ensure_ascii=False))

    # 这里明确区分两层失败：
    # 1. HTTP 非 2xx：说明 proxy 路由自己炸了。
    # 2. ok=false：说明 provider 调用了，但模型/鉴权/格式等业务层失败了。
    if response.status_code >= 400:
        return 1
    if isinstance(data, dict) and data.get("ok") is False:
        return 1
    return 0


def post_span(base_url: str, payload: Dict[str, Any], timeout_sec: float) -> None:
    response = requests.post(
        f"{base_url.rstrip('/')}/logs/spans",
        json=payload,
        timeout=timeout_sec,
    )
    response.raise_for_status()
    print(response.text)


def build_demo_spans(trace_key: int, now_ms: int) -> List[Dict[str, Any]]:
    """
    这条 demo trace 故意保留“支付 -> 银行超时”这条链，
    因为它既能让前端调用链看起来比较像真实问题，也方便 GLM/Gemini 给出明确结论。
    """
    root_span_id = trace_key * 10 + 1
    payment_span_id = trace_key * 10 + 2
    bank_span_id = trace_key * 10 + 3

    root_start_ms = now_ms - 4000
    payment_start_ms = now_ms - 3500
    bank_start_ms = now_ms - 3000

    return [
        {
            "trace_key": trace_key,
            "span_id": bank_span_id,
            "parent_span_id": payment_span_id,
            "start_time_ms": bank_start_ms,
            "end_time_ms": bank_start_ms + 1600,
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
            "span_id": payment_span_id,
            "parent_span_id": root_span_id,
            "start_time_ms": payment_start_ms,
            "end_time_ms": payment_start_ms + 2200,
            "name": "payment-service process-payment",
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
            "span_id": root_span_id,
            "start_time_ms": root_start_ms,
            "end_time_ms": root_start_ms + 2600,
            "name": "gateway-service POST /api/v1/checkout",
            "service_name": "gateway-service",
            "status": "OK",
            "kind": "SERVER",
            "trace_end": True,
            "attributes": {
                "http.method": "POST",
                "http.route": "/api/v1/checkout",
                "tenant": "glm-demo",
            },
        },
    ]


def send_demo_trace(server_base_url: str, timeout_sec: float, seed_ms: int | None) -> int:
    """
    这个动作只负责往后端送一条完整 trace。
    它不保证后端一定已经切到 glm；如果你想测真正的 glm 端到端，
    需要先在 Settings 或启动参数里把 provider/model/api_key 配好并重启后端。
    """
    now_ms = int(time.time() * 1000)
    effective_seed_ms = seed_ms if seed_ms is not None else now_ms
    trace_key = effective_seed_ms % 1_000_000_000

    print(f"[send-spans] server_base_url={server_base_url}")
    print(f"[send-spans] trace_key={trace_key}")

    for payload in build_demo_spans(trace_key, now_ms):
        post_span(server_base_url, payload, timeout_sec)

    print("[send-spans] 已发送完成。")
    print(f"[send-spans] 前端可直接搜索 trace_id={trace_key}")
    print(
        f"[send-spans] 如需确认后端确实走 glm，请先把 ai_provider/model/api_key 配到 glm 并重启后端。"
    )
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="GLM 最小手工联调脚本")
    subparsers = parser.add_subparsers(dest="command", required=True)

    proxy_parser = subparsers.add_parser(
        "probe-proxy",
        help="直打 Python proxy 的 /analyze/trace/glm，验证 key/model/json/usage",
    )
    proxy_parser.add_argument("--proxy-base-url", default="http://127.0.0.1:8001", help="AI proxy 地址")
    proxy_parser.add_argument("--api-key", required=True, help="GLM API Key")
    proxy_parser.add_argument("--model", default="glm-5.1", help="GLM 模型名")
    proxy_parser.add_argument("--timeout-sec", type=float, default=35.0, help="本地等待 proxy 的 HTTP 超时（秒）")
    proxy_parser.add_argument("--provider-timeout-sec", type=float, default=30.0, help="传给 provider 的上游超时预算（秒）")

    span_parser = subparsers.add_parser(
        "send-spans",
        help="往后端发送一条 demo trace，方便前端/数据库观察 Trace 主链",
    )
    span_parser.add_argument("--server-base-url", default="http://127.0.0.1:8080", help="后端地址")
    span_parser.add_argument("--timeout-sec", type=float, default=3.0, help="HTTP 超时（秒）")
    span_parser.add_argument("--seed-ms", type=int, default=None, help="可选，手动指定 trace_key 种子")

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.command == "probe-proxy":
        return call_glm_proxy(
            proxy_base_url=args.proxy_base_url,
            api_key=args.api_key,
            model=args.model,
            timeout_sec=args.timeout_sec,
            provider_timeout_sec=args.provider_timeout_sec,
        )
    if args.command == "send-spans":
        return send_demo_trace(
            server_base_url=args.server_base_url,
            timeout_sec=args.timeout_sec,
            seed_ms=args.seed_ms,
        )
    raise RuntimeError(f"unknown command: {args.command}")


if __name__ == "__main__":
    raise SystemExit(main())
