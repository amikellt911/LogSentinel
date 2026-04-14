#!/usr/bin/env python3

"""
最小 Trace 生命周期语义探针。

这不是正式 benchmark 脚本，只负责发一条固定时间线：
1. 先发一个自带 trace_end 的根 span；
2. 再等待一小段时间；
3. 再补一条晚到 span；
4. 最后轮询 `/traces/{trace_id}`，打印聚合后的 `span_count`。

它的用途很单纯：
- 手工验证 `protected / minimal` 的真实差异；
- 给后面的 Suite B 正式压测脚本先打一个最小样板；
- 排查“到底是 sender 脏了，还是后端生命周期语义脏了”。
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from typing import Dict, Optional, Tuple


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="发送最小 Trace 生命周期测试场景")
    parser.add_argument("--base-url", default="http://127.0.0.1:8080", help="LogSentinel 基础地址")
    parser.add_argument("--trace-key", type=int, default=0, help="显式指定 trace_key；为 0 时自动用当前毫秒时间")
    parser.add_argument("--service-name", default="svc-trace-lifecycle-smoke", help="构造 span 时使用的 service_name")
    parser.add_argument("--root-name", default="lifecycle-root-end", help="根 span 名称")
    parser.add_argument("--late-name", default="lifecycle-late-span", help="晚到 span 名称")
    parser.add_argument("--trace-end-field", default="trace_end", help="结束字段名，默认 trace_end")
    parser.add_argument("--late-span-delay-ms", type=int, default=50, help="根 span 发出后，晚到 span 的延迟")
    parser.add_argument("--detail-timeout-sec", type=float, default=6.0, help="等待 `/traces/{trace_id}` 可查询的超时")
    parser.add_argument("--poll-interval-ms", type=int, default=100, help="轮询 detail 的间隔")
    parser.add_argument("--request-timeout-ms", type=int, default=1000, help="单次 HTTP 请求超时")
    parser.add_argument("--expect-span-count", type=int, default=-1, help="如果 >=0，就校验最终 span_count")
    parser.add_argument("--dry-run", action="store_true", help="只打印将要发送的 payload，不真正发请求")
    return parser.parse_args()


def build_headers() -> Dict[str, str]:
    return {
        "Content-Type": "application/json",
        "Connection": "keep-alive",
    }


def build_payloads(args: argparse.Namespace, trace_key: int) -> Tuple[dict, dict]:
    logical_now_ms = int(time.time() * 1000)
    root = {
        "trace_key": trace_key,
        "span_id": trace_key + 1,
        "start_time_ms": logical_now_ms,
        "end_time_ms": logical_now_ms + 10,
        "name": args.root_name,
        "service_name": args.service_name,
        "status": "OK",
        # 这里故意把结束标记打在根 span 上。
        # 既然我们要测的是“命中结束条件后，晚到 span 还能不能再并进来”，
        # 那最短时间线就是先让根节点直接触发收口，再补一条子 span。
        args.trace_end_field: True,
    }
    late_span = {
        "trace_key": trace_key,
        "span_id": trace_key + 2,
        "parent_span_id": trace_key + 1,
        "start_time_ms": logical_now_ms + args.late_span_delay_ms + 20,
        "end_time_ms": logical_now_ms + args.late_span_delay_ms + 40,
        "name": args.late_name,
        "service_name": args.service_name,
        "status": "ERROR",
    }
    return root, late_span


def post_json(
    opener: urllib.request.OpenerDirector,
    url: str,
    headers: Dict[str, str],
    payload: dict,
    timeout_sec: float,
) -> Tuple[int, str]:
    body = json.dumps(payload, ensure_ascii=True).encode("utf-8")
    request = urllib.request.Request(url=url, data=body, headers=headers, method="POST")
    try:
        with opener.open(request, timeout=timeout_sec) as response:
            return int(response.getcode()), response.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        return int(exc.code), exc.read().decode("utf-8", errors="replace")


def get_json(
    opener: urllib.request.OpenerDirector,
    url: str,
    timeout_sec: float,
) -> Tuple[int, Optional[dict], str]:
    request = urllib.request.Request(url=url, method="GET")
    try:
        with opener.open(request, timeout=timeout_sec) as response:
            text = response.read().decode("utf-8", errors="replace")
            return int(response.getcode()), json.loads(text), text
    except urllib.error.HTTPError as exc:
        text = exc.read().decode("utf-8", errors="replace")
        parsed = None
        try:
            parsed = json.loads(text)
        except json.JSONDecodeError:
            parsed = None
        return int(exc.code), parsed, text


def wait_trace_detail(
    opener: urllib.request.OpenerDirector,
    base_url: str,
    trace_id: str,
    timeout_sec: float,
    poll_interval_ms: int,
    request_timeout_ms: int,
) -> dict:
    # 这里轮询 detail，而不是直接读 SQLite。
    # 这样脚本看到的是完整黑盒效果：HTTP 接入、聚合、落库、查询接口都必须真的通。
    deadline = time.time() + timeout_sec
    detail_url = f"{base_url.rstrip('/')}/traces/{trace_id}"
    timeout_inner_sec = request_timeout_ms / 1000.0
    last_status = 0
    last_body = ""

    while time.time() < deadline:
        status_code, payload, raw_text = get_json(opener, detail_url, timeout_inner_sec)
        last_status = status_code
        last_body = raw_text
        if status_code == 200 and isinstance(payload, dict):
            return payload
        time.sleep(poll_interval_ms / 1000.0)

    raise RuntimeError(
        "trace detail 未在超时内可查询："
        f"trace_id={trace_id}, last_status={last_status}, last_body={last_body}"
    )


def main() -> int:
    args = parse_args()

    if args.late_span_delay_ms < 0:
        raise ValueError("--late-span-delay-ms 必须 >= 0")
    if args.detail_timeout_sec <= 0:
        raise ValueError("--detail-timeout-sec 必须 > 0")
    if args.poll_interval_ms <= 0:
        raise ValueError("--poll-interval-ms 必须 > 0")
    if args.request_timeout_ms <= 0:
        raise ValueError("--request-timeout-ms 必须 > 0")

    trace_key = args.trace_key if args.trace_key > 0 else int(time.time() * 1000)
    trace_id = str(trace_key)
    root, late_span = build_payloads(args, trace_key)

    print(
        "[trace-lifecycle-smoke] scenario="
        f"trace_end_on_root_then_late_span trace_id={trace_id} "
        f"late_span_delay_ms={args.late_span_delay_ms} base_url={args.base_url}"
    )
    print("[trace-lifecycle-smoke] root_payload=" + json.dumps(root, ensure_ascii=False))
    print("[trace-lifecycle-smoke] late_payload=" + json.dumps(late_span, ensure_ascii=False))

    if args.dry_run:
        print("[trace-lifecycle-smoke] dry-run only, no request sent")
        return 0

    opener = urllib.request.build_opener()
    headers = build_headers()
    post_url = f"{args.base_url.rstrip('/')}/logs/spans"
    timeout_sec = args.request_timeout_ms / 1000.0

    root_status, root_body = post_json(opener, post_url, headers, root, timeout_sec)
    print(f"[trace-lifecycle-smoke] root_post status={root_status} body={root_body}")
    if root_status != 202:
        raise RuntimeError(f"根 span 发送失败: status={root_status}, body={root_body}")

    if args.late_span_delay_ms > 0:
        time.sleep(args.late_span_delay_ms / 1000.0)

    late_status, late_body = post_json(opener, post_url, headers, late_span, timeout_sec)
    print(f"[trace-lifecycle-smoke] late_post status={late_status} body={late_body}")
    if late_status != 202:
        raise RuntimeError(f"晚到 span 发送失败: status={late_status}, body={late_body}")

    detail = wait_trace_detail(
        opener=opener,
        base_url=args.base_url,
        trace_id=trace_id,
        timeout_sec=args.detail_timeout_sec,
        poll_interval_ms=args.poll_interval_ms,
        request_timeout_ms=args.request_timeout_ms,
    )

    span_count = int(detail.get("span_count", 0))
    ai_status = str(detail.get("ai_status", ""))
    returned_spans = detail.get("spans", [])
    print(
        "[trace-lifecycle-smoke] detail="
        f"trace_id={trace_id} span_count={span_count} ai_status={ai_status} "
        f"returned_span_items={len(returned_spans) if isinstance(returned_spans, list) else -1}"
    )
    print("[trace-lifecycle-smoke] detail_json=" + json.dumps(detail, ensure_ascii=False))

    if args.expect_span_count >= 0 and span_count != args.expect_span_count:
        raise RuntimeError(
            f"span_count 不符合预期: expected={args.expect_span_count}, actual={span_count}"
        )

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("[trace-lifecycle-smoke] interrupted", file=sys.stderr)
        raise SystemExit(130)
