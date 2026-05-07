#!/usr/bin/env python3
"""
DeepSeek Trace Provider 最小手工联通脚本。

这个脚本只验证一件事：当前仓库里的 DeepSeekProvider 能不能用真实 API Key
直连 DeepSeek v4 flash，并返回 LogSentinel Trace AI 需要的四字段 JSON。

它不经过 C++ 后端，也不要求 Python proxy 已经启动。
原因是联通性排查要先把“厂商接口/模型名/API Key/JSON mode”单独验掉，
再去看后端 Settings 冷启动路由和 Trace 聚合主链。
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import sys
from typing import Any, Dict


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    # 脚本通常从仓库根目录执行，但也允许用户在任意目录直接跑这个文件。
    # 这里把 server/ 加进 import 路径，确保能复用生产 DeepSeekProvider，而不是另写一套 HTTP 逻辑。
    sys.path.insert(0, str(PROJECT_ROOT))

from ai.proxy.providers.deepseek import DeepSeekProvider


def build_demo_trace_text() -> str:
    """
    构造一段稳定的小 trace。
    这里沿用支付链路超时场景，因为它足够短，同时能让模型明确输出 root_cause 和 solution。
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
    DeepSeek JSON mode 只保证 json_object，不保证业务 schema。
    所以 prompt 里必须明确四个字段和 risk_level 枚举，再让 provider 做本地 Pydantic 校验。
    """
    return (
        "You are a distributed tracing analysis expert for LogSentinel.\n"
        "Return ONLY one JSON object with exactly these fields:\n"
        "summary, risk_level, root_cause, solution.\n"
        "risk_level must be one of: critical, error, warning, info, safe, unknown.\n"
        "Do not wrap the JSON in markdown.\n"
        "Use Chinese for summary, root_cause, and solution.\n\n"
        "<trace_context>\n"
        f"{trace_text}"
        "\n</trace_context>\n"
    )


def print_json(label: str, payload: Dict[str, Any]) -> None:
    print(label)
    print(json.dumps(payload, indent=2, ensure_ascii=False))


def run_probe(args: argparse.Namespace) -> int:
    """
    直接复用生产 DeepSeekProvider。
    这样这条手工探针测到的就是线上会走的 request payload、错误归一、usage 归一和 schema 校验路径。
    """
    api_key = args.api_key or os.getenv("DEEPSEEK_API_KEY", "")
    if not api_key:
        print("[deepseek-probe] 缺少 API Key：请传 --api-key 或设置 DEEPSEEK_API_KEY。")
        return 2

    trace_text = build_demo_trace_text()
    prompt = build_demo_prompt(trace_text)
    provider = DeepSeekProvider(
        api_key="",
        model_name=args.model,
        base_url=args.base_url,
        timeout_seconds=args.timeout_sec,
    )

    print(f"[deepseek-probe] base_url={args.base_url.rstrip('/')}")
    print(f"[deepseek-probe] model={args.model}")
    print(f"[deepseek-probe] timeout_sec={args.timeout_sec}")

    result = provider.analyze_trace(
        trace_text=trace_text,
        prompt=prompt,
        api_key=api_key,
        model=args.model,
        timeout_ms=int(args.timeout_sec * 1000),
    )

    print_json("[deepseek-probe] provider result:", result)

    if not isinstance(result, dict):
        print("[deepseek-probe] 失败：provider 返回值不是 dict。")
        return 1
    if result.get("ok") is not True:
        print("[deepseek-probe] 失败：DeepSeek 调用未成功，见 error_status/error_message。")
        return 1

    analysis = result.get("analysis")
    usage = result.get("usage")
    if not isinstance(analysis, dict):
        print("[deepseek-probe] 失败：成功响应缺少 analysis 对象。")
        return 1

    required_fields = {"summary", "risk_level", "root_cause", "solution"}
    missing = sorted(required_fields.difference(analysis.keys()))
    if missing:
        print(f"[deepseek-probe] 失败：analysis 缺字段 {missing}。")
        return 1

    print("[deepseek-probe] 联通成功：DeepSeek v4 flash 返回了可落库的 Trace AI JSON。")
    if usage:
        print_json("[deepseek-probe] usage:", usage)
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="DeepSeek Trace Provider 手工联通脚本")
    parser.add_argument(
        "--api-key",
        default="",
        help="DeepSeek API Key；也可以用环境变量 DEEPSEEK_API_KEY",
    )
    parser.add_argument(
        "--model",
        default="deepseek-v4-flash",
        help="DeepSeek 模型名，默认 deepseek-v4-flash",
    )
    parser.add_argument(
        "--base-url",
        default="https://api.deepseek.com",
        help="DeepSeek API Base URL",
    )
    parser.add_argument(
        "--timeout-sec",
        type=float,
        default=30.0,
        help="本地 provider 调用超时秒数",
    )
    return parser.parse_args()


def main() -> int:
    return run_probe(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
