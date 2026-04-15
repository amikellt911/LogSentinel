#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Callable, Dict, List, Optional

import evaluator
import sender


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Suite B 单次 case runner")
    parser.add_argument("--url", default="http://127.0.0.1:8080/logs/spans")
    parser.add_argument("--profile", default="mixed_realistic", choices=["clean_baseline", "mixed_realistic", "late_replay_stress"])
    parser.add_argument("--trace-lifecycle-profile", default="protected", choices=["protected", "minimal"])
    parser.add_argument("--seed", type=int, default=20260415)
    parser.add_argument("--trace-count", type=int, default=10)
    parser.add_argument("--spans-per-trace", type=int, default=8)
    parser.add_argument("--base-gap-ms", type=int, default=20)
    parser.add_argument("--trace-gap-ms", type=int, default=80)
    parser.add_argument("--tick-ms", type=int, default=500)
    parser.add_argument("--grace-ms", type=int, default=1000)
    parser.add_argument("--tombstone-window-ms", type=int, default=12500)
    parser.add_argument("--service-name", default="svc-suite-b")
    parser.add_argument("--manifest", default="server/tests/benchmark/results/suite_b/manifest.jsonl")
    parser.add_argument("--sqlite-db", required=True)
    parser.add_argument("--output-json", default="")
    parser.add_argument("--timeout-sec", type=float, default=1.0)
    parser.add_argument("--send-workers", type=int, default=1)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--poll-interval-sec", type=float, default=0.2)
    parser.add_argument("--stable-rounds", type=int, default=5)
    parser.add_argument("--confirm-sleep-sec", type=float, default=0.3)
    parser.add_argument("--max-wait-sec", type=float, default=30.0)
    return parser.parse_args(argv)


def build_sender_args(args: argparse.Namespace) -> argparse.Namespace:
    return argparse.Namespace(
        url=args.url,
        profile=args.profile,
        seed=args.seed,
        trace_count=args.trace_count,
        spans_per_trace=args.spans_per_trace,
        base_gap_ms=args.base_gap_ms,
        trace_gap_ms=args.trace_gap_ms,
        tick_ms=args.tick_ms,
        grace_ms=args.grace_ms,
        tombstone_window_ms=args.tombstone_window_ms,
        service_name=args.service_name,
        manifest=args.manifest,
        timeout_sec=args.timeout_sec,
        send_workers=args.send_workers,
        dry_run=args.dry_run,
    )


def run_suite_b_case(
    args: argparse.Namespace,
    sender_runner: Callable[[argparse.Namespace], int] = sender.run_sender,
    evaluator_runner: Callable[..., Dict[str, object]] = evaluator.evaluate_suite_b,
) -> Dict[str, object]:
    manifest_path = Path(args.manifest)
    sqlite_path = Path(args.sqlite_db)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)

    # 这层 runner 第一刀只负责“单次 case 闭环”：
    # 1) 用 sender 产出 manifest；
    # 2) 再用 evaluator 读取 SQLite 给出指标；
    # 它故意不负责起停后端，也不负责 3x2 矩阵批跑，避免 orchestration 和实验矩阵耦死。
    sender_runner(build_sender_args(args))

    evaluation = evaluator_runner(
        manifest_path=manifest_path,
        sqlite_path=sqlite_path,
        poll_interval_sec=args.poll_interval_sec,
        stable_rounds=args.stable_rounds,
        confirm_sleep_sec=args.confirm_sleep_sec,
        max_wait_sec=args.max_wait_sec,
    )

    # sender 和 evaluator 的参数原来分散在两条 CLI 上。
    # 这里重新收成一份统一 JSON，是为了后面做正式 benchmark 时，一条命令的输入和输出都能直接留档。
    result: Dict[str, object] = {
        "profile": args.profile,
        "trace_lifecycle_profile": args.trace_lifecycle_profile,
        "manifest": str(manifest_path),
        "sqlite_db": str(sqlite_path),
        "sender": {
            "url": args.url,
            "seed": args.seed,
            "trace_count": args.trace_count,
            "spans_per_trace": args.spans_per_trace,
            "base_gap_ms": args.base_gap_ms,
            "trace_gap_ms": args.trace_gap_ms,
            "tick_ms": args.tick_ms,
            "grace_ms": args.grace_ms,
            "tombstone_window_ms": args.tombstone_window_ms,
            "service_name": args.service_name,
            "timeout_sec": args.timeout_sec,
            "send_workers": args.send_workers,
            "dry_run": args.dry_run,
        },
        "evaluator": {
            "poll_interval_sec": args.poll_interval_sec,
            "stable_rounds": args.stable_rounds,
            "confirm_sleep_sec": args.confirm_sleep_sec,
            "max_wait_sec": args.max_wait_sec,
        },
    }
    result.update(evaluation)

    if args.output_json:
        output_path = Path(args.output_json)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(result, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")

    return result


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    result = run_suite_b_case(args)
    print(json.dumps(result, ensure_ascii=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
