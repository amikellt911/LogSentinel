#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional

CURRENT_DIR = Path(__file__).resolve().parent
if str(CURRENT_DIR) not in sys.path:
    sys.path.insert(0, str(CURRENT_DIR))

import run_suite_d_case


JsonDict = Dict[str, Any]
DEFAULT_SEARCH_ROOT = "server/tests/benchmark/results/suite_d/topology_search"
# 这里故意只保留 3 个候选，不再扩成大矩阵。
# Suite D 这一轮要回答的是“统一部署策略下哪个拓扑更像样”，不是把 24 核机器重新做一次暴力调参。
DEFAULT_CANDIDATES: List[JsonDict] = [
    {"name": "t1", "server_io_threads": 4, "dispatch_worker_threads": 3, "worker_threads": 32},
    {"name": "t2", "server_io_threads": 6, "dispatch_worker_threads": 4, "worker_threads": 32},
    {"name": "t3", "server_io_threads": 6, "dispatch_worker_threads": 5, "worker_threads": 48},
]


def format_run_timestamp(now_func: Callable[[], float] = time.time) -> str:
    now = now_func()
    seconds = int(now)
    millis = int(round((now - seconds) * 1000))
    if millis >= 1000:
        seconds += 1
        millis = 0
    return f"{time.strftime('%Y%m%d-%H%M%S', time.localtime(seconds))}-{millis:03d}ms"


def resolve_search_root(search_root_prefix: str, now_func: Callable[[], float] = time.time) -> tuple[str, str]:
    requested_search_root = str(Path(search_root_prefix))
    actual_search_root = f"{requested_search_root}-{format_run_timestamp(now_func)}"
    return requested_search_root, actual_search_root


def ensure_search_root_resolved(args: argparse.Namespace) -> None:
    if hasattr(args, "actual_search_root"):
        args.requested_search_root = getattr(args, "requested_search_root", args.search_root)
        return

    requested_search_root, actual_search_root = resolve_search_root(args.search_root)
    args.requested_search_root = requested_search_root
    args.actual_search_root = actual_search_root


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Suite D 24核小拓扑搜索 runner；固定 sender=4/backend=20，只比较 T1/T2/T3"
    )
    parser.add_argument("--search-root", default=DEFAULT_SEARCH_ROOT)
    parser.add_argument("--output-summary", default="")
    parser.add_argument("--port-base", type=int, default=18180)
    parser.add_argument("--port-stride", type=int, default=1)
    parser.add_argument("--server-bin", default="./server/build/LogSentinel")
    parser.add_argument("--server-command", default="")
    parser.add_argument("--server-cpuset", default="4-23")
    parser.add_argument("--wrk-cpuset", default="0-3")
    parser.add_argument("--sender-cores", type=int, default=4)
    parser.add_argument("--backend-cores", type=int, default=20)
    parser.add_argument("--wrk-bin", default="wrk")
    parser.add_argument("--wrk-script", default=run_suite_d_case.DEFAULT_WRK_SCRIPT)
    parser.add_argument("--wrk-threads", type=int, default=2)
    parser.add_argument("--connections", type=int, default=120)
    parser.add_argument("--duration", default="15s")
    parser.add_argument("--warmup-duration", default="3s")
    parser.add_argument("--spans-per-trace", type=int, default=8)
    parser.add_argument("--worker-queue-size", type=int, default=8192)
    parser.add_argument("--trace-active-session-limit", type=int, default=2048)
    parser.add_argument("--trace-buffered-span-limit", type=int, default=16384)
    parser.add_argument("--trace-max-dispatch-per-tick", type=int, default=64)
    parser.add_argument("--trace-lifecycle-profile", default="protected")
    parser.add_argument("--trace-sealed-grace-window-ms", type=int, default=100)
    parser.add_argument("--trace-sweep-interval-ms", type=int, default=200)
    parser.add_argument("--trace-primary-flush-span-threshold", type=int, default=512)
    parser.add_argument("--trace-primary-flush-interval-ms", type=int, default=5)
    parser.add_argument("--startup-timeout-sec", type=float, default=10.0)
    parser.add_argument("--stop-timeout-sec", type=float, default=5.0)
    parser.add_argument("--poll-interval-ms", type=int, default=50)
    parser.add_argument("--stable-rounds", type=int, default=3)
    parser.add_argument("--confirm-sleep-ms", type=int, default=100)
    parser.add_argument("--max-drain-wait-ms", type=int, default=30000)
    parser.add_argument("--ai-mode", choices=("off", "on"), default="off")
    args = parser.parse_args(argv)

    if not args.server_bin and not args.server_command:
        parser.error("--server-bin or --server-command is required")
    if args.port_stride <= 0:
        parser.error("--port-stride must be > 0")
    if args.sender_cores != 4:
        parser.error("--sender-cores is currently frozen to 4 for 24-core topology search")
    if args.backend_cores != 20:
        parser.error("--backend-cores is currently frozen to 20 for 24-core topology search")
    if args.ai_mode != "off":
        parser.error("--ai-mode is currently frozen to off for topology search")
    args.candidates = [dict(item) for item in DEFAULT_CANDIDATES]
    return args


def build_case_args(
    args: argparse.Namespace,
    candidate: JsonDict,
    candidate_index: int,
) -> argparse.Namespace:
    run_root = Path(args.actual_search_root) / str(candidate["name"]) / "run_01"
    run_root.mkdir(parents=True, exist_ok=True)
    output_json = run_root / "result.json"

    argv = [
        "--run-root",
        str(run_root),
        "--output-json",
        str(output_json),
        "--port-base",
        str(args.port_base + candidate_index * args.port_stride),
        "--server-cpuset",
        args.server_cpuset,
        "--wrk-cpuset",
        args.wrk_cpuset,
        "--wrk-bin",
        args.wrk_bin,
        "--wrk-script",
        args.wrk_script,
        "--wrk-threads",
        str(args.wrk_threads),
        "--connections",
        str(args.connections),
        "--duration",
        args.duration,
        "--warmup-duration",
        args.warmup_duration,
        "--spans-per-trace",
        str(args.spans_per_trace),
        "--server-io-threads",
        str(candidate["server_io_threads"]),
        "--dispatch-worker-threads",
        str(candidate["dispatch_worker_threads"]),
        "--worker-threads",
        str(candidate["worker_threads"]),
        "--worker-queue-size",
        str(args.worker_queue_size),
        "--trace-active-session-limit",
        str(args.trace_active_session_limit),
        "--trace-buffered-span-limit",
        str(args.trace_buffered_span_limit),
        "--trace-max-dispatch-per-tick",
        str(args.trace_max_dispatch_per_tick),
        "--trace-lifecycle-profile",
        args.trace_lifecycle_profile,
        "--trace-sealed-grace-window-ms",
        str(args.trace_sealed_grace_window_ms),
        "--trace-sweep-interval-ms",
        str(args.trace_sweep_interval_ms),
        "--trace-primary-flush-span-threshold",
        str(args.trace_primary_flush_span_threshold),
        "--trace-primary-flush-interval-ms",
        str(args.trace_primary_flush_interval_ms),
        "--poll-interval-ms",
        str(args.poll_interval_ms),
        "--stable-rounds",
        str(args.stable_rounds),
        "--confirm-sleep-ms",
        str(args.confirm_sleep_ms),
        "--max-drain-wait-ms",
        str(args.max_drain_wait_ms),
        "--startup-timeout-sec",
        str(args.startup_timeout_sec),
        "--stop-timeout-sec",
        str(args.stop_timeout_sec),
        "--disable-ai",
        "--disable-webhook",
        "--no-auto-start-proxy",
    ]
    if args.server_command:
        argv.extend(["--server-command", args.server_command])
    else:
        argv.extend(["--server-bin", args.server_bin])
    return run_suite_d_case.parse_args(argv)


def run_case_once(case_args: argparse.Namespace) -> JsonDict:
    return run_suite_d_case.run_suite_d_case(case_args)


def candidate_sort_key(candidate_result: JsonDict) -> tuple[float, float, int, str]:
    # 这里先看窗口内真正完成了多少 trace。
    # 如果主指标都输了，只靠尾巴短一点没有意义；所以先比 online，再比 ratio，最后才用 drain 当 tie-break。
    return (
        -float(candidate_result["online_completed_traces_per_sec"]),
        -float(candidate_result["online_completion_ratio"]),
        int(candidate_result["drain_tail_ms"]),
        str(candidate_result["candidate_name"]),
    )


def write_summary(output_path: str, summary: JsonDict) -> None:
    output_file = Path(output_path)
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(json.dumps(summary, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")


def run_topology_search(
    args: argparse.Namespace,
    case_runner: Callable[[argparse.Namespace], JsonDict] = run_case_once,
    line_writer: Callable[[str], None] = print,
) -> JsonDict:
    ensure_search_root_resolved(args)
    Path(args.actual_search_root).mkdir(parents=True, exist_ok=True)

    candidate_results: List[JsonDict] = []
    for index, candidate in enumerate(args.candidates):
        case_args = build_case_args(args, candidate, index)
        case_result = dict(case_runner(case_args))
        case_result["candidate_name"] = str(candidate["name"])
        case_result["server_io_threads"] = int(candidate["server_io_threads"])
        case_result["dispatch_worker_threads"] = int(candidate["dispatch_worker_threads"])
        case_result["worker_threads"] = int(candidate["worker_threads"])
        candidate_results.append(case_result)

        # winner_so_far 只在“已经跑过的 candidate 子集”里选。
        # 这样终端输出能边跑边给出当前领先者，不需要等到最后一轮才知道是否值得继续盯日志。
        current_winner = sorted(candidate_results, key=candidate_sort_key)[0]
        line_writer(
            f"[candidate {index + 1}/{len(args.candidates)}] "
            f"name={candidate['name']} "
            f"online={float(case_result['online_completed_traces_per_sec']):.2f} "
            f"ratio={float(case_result['online_completion_ratio']):.4f} "
            f"drain={int(case_result['drain_tail_ms'])} "
            f"qps={float(case_result['wrk_metrics']['requests_per_sec']):.2f} "
            f"winner_so_far={current_winner['candidate_name']}"
        )

    ranked_candidates = sorted(candidate_results, key=candidate_sort_key)
    summary: JsonDict = {
        "requested_search_root": args.requested_search_root,
        "actual_search_root": args.actual_search_root,
        "sender_cores": args.sender_cores,
        "backend_cores": args.backend_cores,
        "wrk_cpuset": args.wrk_cpuset,
        "server_cpuset": args.server_cpuset,
        "ai_mode": args.ai_mode,
        "candidates": candidate_results,
        "top_candidates": ranked_candidates,
    }
    output_summary = args.output_summary or str(Path(args.actual_search_root) / "summary.json")
    write_summary(output_summary, summary)
    return summary


def main() -> None:
    args = parse_args()
    run_topology_search(args)


if __name__ == "__main__":
    main()
