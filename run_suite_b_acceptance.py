#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import socket
import sys
from pathlib import Path
from typing import List

CURRENT_DIR = Path(__file__).resolve().parent
if str(CURRENT_DIR) not in sys.path:
    sys.path.insert(0, str(CURRENT_DIR))

import run_suite_b_matrix


def parse_args(argv: List[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run a small Suite B protected/minimal acceptance matrix and print key metrics."
    )
    parser.add_argument(
        "--server-bin",
        default="./server/build/LogSentinel",
        help="LogSentinel binary path, default: ./server/build/LogSentinel",
    )
    parser.add_argument(
        "--run-root",
        default="/tmp/suite_b_acceptance",
        help="Result directory prefix; timestamp suffix is added by matrix runner.",
    )
    parser.add_argument(
        "--port-base",
        type=int,
        default=19580,
        help="Preferred first port. The wrapper will search for 6 free consecutive ports.",
    )
    parser.add_argument(
        "--port-search-limit",
        type=int,
        default=80,
        help="How many candidate starting ports to try, default: 80.",
    )
    parser.add_argument(
        "--server-cpuset",
        default="1-3",
        help="CPU set for backend process on a 4-core machine, default: 1-3.",
    )
    parser.add_argument("--server-io-threads", type=int, default=1)
    parser.add_argument("--worker-threads", type=int, default=8)
    parser.add_argument("--dispatch-worker-threads", type=int, default=1)
    parser.add_argument("--worker-queue-size", type=int, default=4096)
    parser.add_argument("--trace-count", type=int, default=10)
    parser.add_argument("--spans-per-trace", type=int, default=8)
    parser.add_argument("--send-workers", type=int, default=2)
    parser.add_argument("--seed", type=int, default=20260415)
    parser.add_argument(
        "--summary-json",
        default="",
        help="Optional explicit summary output path. Usually not needed.",
    )
    return parser.parse_args(argv)


def is_port_free(port: int, host: str = "127.0.0.1") -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.2)
        return sock.connect_ex((host, port)) != 0


def find_free_port_block(start_port: int, needed: int, search_limit: int) -> int:
    # Suite B matrix 一轮会连续占用 6 个端口。
    # 验收现场不应该因为某个旧进程占着 19580 就失败，所以这里主动找一段连续空闲端口。
    if needed <= 0:
        raise ValueError("needed must be > 0")
    if search_limit <= 0:
        raise ValueError("search_limit must be > 0")

    for candidate in range(start_port, start_port + search_limit):
        ports = range(candidate, candidate + needed)
        if all(is_port_free(port) for port in ports):
            return candidate
    raise RuntimeError(
        f"no free block of {needed} consecutive ports found from "
        f"{start_port} to {start_port + search_limit - 1}"
    )


def build_matrix_argv(args: argparse.Namespace, port_base: int) -> List[str]:
    # 这个 wrapper 固定的是“验收小矩阵”，不是论文正式 campaign。
    # 参数保持小规模，目的是现场快速复跑 protected/minimal 趋势，而不是替代正式多 seed 结果。
    argv = [
        "--server-bin",
        args.server_bin,
        "--run-root",
        args.run_root,
        "--port-base",
        str(port_base),
        "--server-cpuset",
        args.server_cpuset,
        "--server-io-threads",
        str(args.server_io_threads),
        "--worker-threads",
        str(args.worker_threads),
        "--dispatch-worker-threads",
        str(args.dispatch_worker_threads),
        "--worker-queue-size",
        str(args.worker_queue_size),
        "--trace-capacity",
        "12",
        "--trace-token-limit",
        "0",
        "--trace-sweep-interval-ms",
        "100",
        "--trace-idle-timeout-ms",
        "800",
        "--trace-max-dispatch-per-tick",
        "64",
        "--trace-buffered-span-limit",
        "4096",
        "--trace-active-session-limit",
        "512",
        "--disable-ai",
        "--disable-webhook",
        "--no-auto-start-proxy",
        "--trace-count",
        str(args.trace_count),
        "--spans-per-trace",
        str(args.spans_per_trace),
        "--send-workers",
        str(args.send_workers),
        "--seed",
        str(args.seed),
        "--sender-profiles",
        "clean_baseline,mixed_realistic,late_replay_stress",
        "--trace-lifecycle-profiles",
        "protected,minimal",
    ]
    if args.summary_json:
        argv.extend(["--output-summary", args.summary_json])
    return argv


def rate_value(case: dict, field: str) -> float:
    metric = case.get(field)
    if not isinstance(metric, dict):
        return 0.0
    value = metric.get("value")
    if isinstance(value, (int, float)):
        return float(value)
    return 0.0


def print_core_metrics(summary: dict) -> None:
    print("\n[suite-b-acceptance] core metrics")
    print(f"[suite-b-acceptance] summary_json={summary.get('artifacts', {}).get('summary_json')}")
    print(f"[suite-b-acceptance] actual_run_root={summary.get('actual_run_root')}")
    print("case_id                              complete  pollution duplicate unique_fail")
    print("------------------------------------ --------- --------- --------- -----------")
    for case in summary.get("cases", []):
        if not isinstance(case, dict):
            continue
        # 这里直接打印 evaluator 的三项主指标。
        # complete 越接近 1 越好；pollution/duplicate 越接近 0 越好；unique_fail 是重复写尝试诊断。
        print(
            f"{str(case.get('case_id', '')):<36} "
            f"{rate_value(case, 'trace_completeness_rate'):<9.4f} "
            f"{rate_value(case, 'trace_pollution_rate'):<9.4f} "
            f"{rate_value(case, 'duplicate_persistence_rate'):<9.4f} "
            f"{int(case.get('sqlite_unique_constraint_fail_count', 0)):<11d}"
        )

    deltas = summary.get("ingest_p95_latency_delta_by_profile")
    if isinstance(deltas, dict) and deltas:
        print("\n[suite-b-acceptance] protected - minimal ingest p95 delta")
        for profile, delta in deltas.items():
            if not isinstance(delta, dict):
                continue
            print(
                f"{profile}: protected={delta.get('protected_p95_ms')}ms "
                f"minimal={delta.get('minimal_p95_ms')}ms "
                f"delta={delta.get('absolute_ms')}ms"
            )


def main(argv: List[str] | None = None) -> int:
    args = parse_args(argv)
    port_base = find_free_port_block(
        start_port=args.port_base,
        needed=6,
        search_limit=args.port_search_limit,
    )
    if port_base != args.port_base:
        print(
            f"[suite-b-acceptance] preferred port {args.port_base} is busy; "
            f"use free block starting at {port_base}"
        )

    matrix_argv = build_matrix_argv(args, port_base)
    print("[suite-b-acceptance] running matrix:")
    print("python3 server/tests/benchmark/suite_b/run_suite_b_matrix.py " + " ".join(matrix_argv))

    matrix_args = run_suite_b_matrix.parse_args(matrix_argv)
    summary = run_suite_b_matrix.run_suite_b_matrix(matrix_args)
    print_core_metrics(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
