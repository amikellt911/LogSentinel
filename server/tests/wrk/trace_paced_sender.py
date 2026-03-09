#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Dict, Tuple


@dataclass
class SenderStats:
    total_requests: int = 0
    success_requests: int = 0
    non_2xx_requests: int = 0
    transport_errors: int = 0
    total_traces: int = 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="按固定节奏发送 trace，用于业务态 flamegraph")
    parser.add_argument("--url", default="http://127.0.0.1:8080/logs/spans")
    parser.add_argument("--mode", default="end", choices=["end"])
    parser.add_argument("--spans-per-trace", type=int, default=8)
    parser.add_argument("--batch-traces", type=int, default=5)
    parser.add_argument("--batch-sleep-ms", type=int, default=100)
    parser.add_argument("--duration-sec", type=int, default=20)
    parser.add_argument("--request-timeout-ms", type=int, default=1000)
    parser.add_argument("--service-name", default="svc-paced")
    return parser.parse_args()


def build_headers() -> Dict[str, str]:
    return {
        "Content-Type": "application/json",
        "Connection": "keep-alive",
    }


def build_span_payload(
    trace_key: int,
    span_id: int,
    spans_per_trace: int,
    service_name: str,
    logical_now_ms: int,
) -> Dict[str, object]:
    payload: Dict[str, object] = {
        "trace_key": trace_key,
        "span_id": span_id,
        "start_time_ms": logical_now_ms,
        "name": f"paced-span-{span_id}",
        "service_name": service_name,
        "trace_end": span_id >= spans_per_trace,
        "attributes": {
            "bench_mode": "paced-end",
            "sender": "trace_paced_sender.py",
        },
    }
    if span_id > 1:
        payload["parent_span_id"] = span_id - 1
    return payload


def post_json(
    opener: urllib.request.OpenerDirector,
    url: str,
    headers: Dict[str, str],
    payload: Dict[str, object],
    timeout_sec: float,
) -> int:
    body = json.dumps(payload, ensure_ascii=True).encode("utf-8")
    request = urllib.request.Request(url=url, data=body, headers=headers, method="POST")
    try:
        with opener.open(request, timeout=timeout_sec) as response:
            return int(response.getcode())
    except urllib.error.HTTPError as exc:
        return int(exc.code)


def send_trace_batch(
    opener: urllib.request.OpenerDirector,
    url: str,
    headers: Dict[str, str],
    args: argparse.Namespace,
    next_trace_key: int,
    logical_now_ms: int,
    stats: SenderStats,
) -> Tuple[int, int]:
    timeout_sec = args.request_timeout_ms / 1000.0

    for _ in range(args.batch_traces):
        trace_key = next_trace_key
        next_trace_key += 1
        stats.total_traces += 1

        for span_id in range(1, args.spans_per_trace + 1):
            logical_now_ms += 1
            payload = build_span_payload(
                trace_key=trace_key,
                span_id=span_id,
                spans_per_trace=args.spans_per_trace,
                service_name=args.service_name,
                logical_now_ms=logical_now_ms,
            )
            stats.total_requests += 1
            try:
                status_code = post_json(
                    opener=opener,
                    url=url,
                    headers=headers,
                    payload=payload,
                    timeout_sec=timeout_sec,
                )
            except Exception:
                stats.transport_errors += 1
                continue

            if 200 <= status_code < 300:
                stats.success_requests += 1
            else:
                stats.non_2xx_requests += 1

    return next_trace_key, logical_now_ms


def main() -> int:
    args = parse_args()
    opener = urllib.request.build_opener()
    headers = build_headers()
    stats = SenderStats()

    start_monotonic = time.monotonic()
    deadline = start_monotonic + args.duration_sec
    next_trace_key = 1
    logical_now_ms = int(time.time() * 1000)

    print(
        "paced_sender start: "
        f"url={args.url}, mode={args.mode}, spans_per_trace={args.spans_per_trace}, "
        f"batch_traces={args.batch_traces}, batch_sleep_ms={args.batch_sleep_ms}, "
        f"duration_sec={args.duration_sec}"
    )
    sys.stdout.flush()

    while time.monotonic() < deadline:
        next_trace_key, logical_now_ms = send_trace_batch(
            opener=opener,
            url=args.url,
            headers=headers,
            args=args,
            next_trace_key=next_trace_key,
            logical_now_ms=logical_now_ms,
            stats=stats,
        )

        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break

        time.sleep(min(args.batch_sleep_ms / 1000.0, remaining))

    elapsed = max(time.monotonic() - start_monotonic, 1e-9)
    print(
        "paced_sender done: "
        f"elapsed_sec={elapsed:.3f}, total_traces={stats.total_traces}, total_requests={stats.total_requests}, "
        f"success_requests={stats.success_requests}, non_2xx_requests={stats.non_2xx_requests}, "
        f"transport_errors={stats.transport_errors}, trace_per_sec={stats.total_traces / elapsed:.2f}, "
        f"req_per_sec={stats.total_requests / elapsed:.2f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
