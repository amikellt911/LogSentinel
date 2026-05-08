#!/usr/bin/env python3
"""
Trace attributes 详情接口黑盒验证。

这个脚本专门覆盖演示所需的证据链：
POST /logs/spans 上报 attributes -> Trace 聚合落库 -> GET /traces/{trace_id} 返回 span.attributes。
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

import requests


def parse_args() -> argparse.Namespace:
    server_dir = Path(__file__).resolve().parents[1]
    default_bin = server_dir / "build" / "LogSentinel"
    default_db = Path("/tmp") / f"logsentinel_trace_attributes_{int(time.time())}.db"

    parser = argparse.ArgumentParser(description="验证 Trace 详情接口会返回 Span attributes")
    parser.add_argument("--server-bin", default=str(default_bin), help="LogSentinel 可执行文件路径")
    parser.add_argument("--db", default=str(default_db), help="临时 SQLite 数据库路径")
    parser.add_argument("--port", type=int, default=18082, help="临时后端端口")
    parser.add_argument("--timeout", type=float, default=8.0, help="等待详情可查询的超时时间")
    return parser.parse_args()


def start_server(server_bin: Path, db_path: Path, port: int) -> subprocess.Popen:
    if not server_bin.exists():
        raise FileNotFoundError(f"未找到服务可执行文件: {server_bin}")

    cmd = [
        str(server_bin),
        "--db",
        str(db_path),
        "--port",
        str(port),
        "--disable-ai",
        "--disable-webhook",
    ]
    # 这里显式关闭 AI/Webhook，因为本脚本只验证 Trace 详情读侧 attributes，
    # 不应该让外部 provider、proxy 或告警通道影响结果稳定性。
    return subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )


def wait_server_ready(base_url: str, proc: subprocess.Popen, timeout: float) -> None:
    deadline = time.time() + timeout
    last_error: Exception | None = None
    while time.time() < deadline:
        if proc.poll() is not None:
            raise RuntimeError("服务进程在就绪前退出")
        try:
            resp = requests.get(f"{base_url}/__attributes_ready__", timeout=0.8)
            if resp.status_code:
                return
        except requests.RequestException as exc:
            last_error = exc
        time.sleep(0.2)
    raise RuntimeError(f"服务未在 {timeout}s 内就绪: {last_error}")


def post_span(base_url: str, payload: dict[str, Any]) -> None:
    resp = requests.post(f"{base_url}/logs/spans", json=payload, timeout=2.0)
    if not (200 <= resp.status_code < 300):
        raise RuntimeError(f"POST /logs/spans 失败: status={resp.status_code}, body={resp.text}")


def wait_trace_detail(base_url: str, trace_id: str, timeout: float) -> dict[str, Any]:
    deadline = time.time() + timeout
    last_body = ""
    while time.time() < deadline:
        resp = requests.get(f"{base_url}/traces/{trace_id}", timeout=2.0)
        last_body = resp.text
        if resp.status_code == 200:
            return resp.json()
        if resp.status_code not in (404, 503):
            raise RuntimeError(f"GET /traces/{trace_id} 异常: status={resp.status_code}, body={resp.text}")
        time.sleep(0.25)
    raise RuntimeError(f"Trace 详情未在 {timeout}s 内可查询，最后响应: {last_body}")


def assert_span_attributes(detail: dict[str, Any]) -> None:
    spans = detail.get("spans")
    if not isinstance(spans, list) or len(spans) != 2:
        raise AssertionError(f"spans 数量异常: {spans}")

    by_id = {str(span.get("span_id")): span for span in spans}
    root_attrs = by_id.get("910001", {}).get("attributes")
    child_attrs = by_id.get("910002", {}).get("attributes")

    # 这些断言对应预售演示的最小证据形状：
    # 一个 Span 证明当前规则快照，另一个 Span 证明下游消费了同一个 quote 并落到具体金额。
    if root_attrs != {
        "config_snapshot_version": "promo_rule_v18",
        "campaign_id": "PRESALE_0428",
    }:
        raise AssertionError(f"root attributes 不匹配: {root_attrs}")

    if not isinstance(child_attrs, dict):
        raise AssertionError(f"child attributes 不是对象: {child_attrs}")
    if child_attrs.get("quote_id") != "QUOTE8821":
        raise AssertionError(f"quote_id 未透出: {child_attrs}")
    if child_attrs.get("final_pay_amount") != "700":
        raise AssertionError(f"final_pay_amount 未透出: {child_attrs}")


def drain_output(proc: subprocess.Popen) -> str:
    if not proc.stdout:
        return ""
    try:
        return proc.stdout.read() or ""
    except Exception:
        return ""


def main() -> int:
    args = parse_args()
    db_path = Path(args.db)
    if db_path.exists():
        db_path.unlink()

    proc = start_server(Path(args.server_bin), db_path, args.port)
    base_url = f"http://127.0.0.1:{args.port}"
    trace_key = 91000
    trace_id = str(trace_key)

    try:
        wait_server_ready(base_url, proc, args.timeout)
        start_ms = int(time.time() * 1000)
        post_span(base_url, {
            "trace_key": trace_key,
            "span_id": 910001,
            "start_time_ms": start_ms,
            "end_time_ms": start_ms + 80,
            "name": "order-service settle_presale_final_payment",
            "service_name": "order-service",
            "status": "OK",
            "attributes": {
                "config_snapshot_version": "promo_rule_v18",
                "campaign_id": "PRESALE_0428",
            },
        })
        post_span(base_url, {
            "trace_key": trace_key,
            "span_id": 910002,
            "parent_span_id": 910001,
            "start_time_ms": start_ms + 20,
            "end_time_ms": start_ms + 60,
            "name": "order-service calculate_final_pay_amount",
            "service_name": "order-service",
            "status": "OK",
            "trace_end": True,
            "attributes": {
                "quote_id": "QUOTE8821",
                "final_pay_amount": "700",
            },
        })
        detail = wait_trace_detail(base_url, trace_id, args.timeout)
        assert_span_attributes(detail)
        print(f"[attributes-smoke] 通过: trace_id={trace_id} span.attributes 已从详情接口返回")
        return 0
    except Exception as exc:
        print(f"[attributes-smoke] 失败: {exc}", file=sys.stderr)
        return 1
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)
        if proc.returncode not in (0, -15, -9, None):
            print(drain_output(proc), file=sys.stderr)
        if db_path.exists():
            db_path.unlink()


if __name__ == "__main__":
    raise SystemExit(main())
