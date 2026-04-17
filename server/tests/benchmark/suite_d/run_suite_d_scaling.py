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
DEFAULT_SCALING_ROOT = "server/tests/benchmark/results/suite_d/scaling"
DEFAULT_TOTAL_CORE_POINTS = [4, 8, 12, 16, 20, 24]
# 这里冻结的是“总核数 -> sender/backend 分摊”。
# 也就是说主变量始终是总预算，sender 和 backend 只是这个预算下的部署拆分，不再额外做独立搜索。
DEFAULT_CORE_SPLIT = {
    4: {"sender_cores": 1, "backend_cores": 3},
    8: {"sender_cores": 2, "backend_cores": 6},
    12: {"sender_cores": 2, "backend_cores": 10},
    16: {"sender_cores": 3, "backend_cores": 13},
    20: {"sender_cores": 3, "backend_cores": 17},
    24: {"sender_cores": 4, "backend_cores": 20},
}
# 这里直接冻结成 T2 的比例映射结果。
# Task 4 的职责不是“再决定用哪套拓扑”，而是把已经约定好的基线拓扑落成可复跑的主曲线入口。
DEFAULT_TOPOLOGY_MAP = {
    4: {"server_io_threads": 1, "dispatch_worker_threads": 1, "worker_threads": 8},
    8: {"server_io_threads": 2, "dispatch_worker_threads": 1, "worker_threads": 12},
    12: {"server_io_threads": 3, "dispatch_worker_threads": 2, "worker_threads": 16},
    16: {"server_io_threads": 4, "dispatch_worker_threads": 3, "worker_threads": 24},
    20: {"server_io_threads": 5, "dispatch_worker_threads": 3, "worker_threads": 28},
    24: {"server_io_threads": 6, "dispatch_worker_threads": 4, "worker_threads": 32},
}


def format_run_timestamp(now_func: Callable[[], float] = time.time) -> str:
    now = now_func()
    seconds = int(now)
    millis = int(round((now - seconds) * 1000))
    if millis >= 1000:
        seconds += 1
        millis = 0
    return f"{time.strftime('%Y%m%d-%H%M%S', time.localtime(seconds))}-{millis:03d}ms"


def resolve_scaling_root(scaling_root_prefix: str, now_func: Callable[[], float] = time.time) -> tuple[str, str]:
    requested_scaling_root = str(Path(scaling_root_prefix))
    actual_scaling_root = f"{requested_scaling_root}-{format_run_timestamp(now_func)}"
    return requested_scaling_root, actual_scaling_root


def ensure_scaling_root_resolved(args: argparse.Namespace) -> None:
    if hasattr(args, "actual_scaling_root"):
        args.requested_scaling_root = getattr(args, "requested_scaling_root", args.scaling_root)
        return

    requested_scaling_root, actual_scaling_root = resolve_scaling_root(args.scaling_root)
    args.requested_scaling_root = requested_scaling_root
    args.actual_scaling_root = actual_scaling_root


def parse_csv_ints(value: str, option_name: str) -> List[int]:
    values: List[int] = []
    for item in value.split(","):
        item = item.strip()
        if item:
            values.append(int(item))
    if not values:
        raise ValueError(f"{option_name} must contain at least one integer")
    return values


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Suite D 主扩展曲线 runner；固定 AI-off，用 T2 比例映射跑 4/8/12/16/20/24 核"
    )
    parser.add_argument("--scaling-root", default=DEFAULT_SCALING_ROOT)
    parser.add_argument("--output-summary", default="")
    parser.add_argument(
        "--total-core-points",
        default=",".join(str(item) for item in DEFAULT_TOTAL_CORE_POINTS),
    )
    parser.add_argument("--port-base", type=int, default=18180)
    parser.add_argument("--port-stride", type=int, default=1)
    parser.add_argument("--server-bin", default="./server/build/LogSentinel")
    parser.add_argument("--server-command", default="")
    parser.add_argument("--wrk-bin", default="wrk")
    parser.add_argument("--wrk-script", default=run_suite_d_case.DEFAULT_WRK_SCRIPT)
    parser.add_argument("--duration", default="15s")
    parser.add_argument("--warmup-duration", default="3s")
    parser.add_argument("--spans-per-trace", type=int, default=8)
    parser.add_argument("--connections-per-sender-core", type=int, default=30)
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
    if args.connections_per_sender_core <= 0:
        parser.error("--connections-per-sender-core must be > 0")
    args.total_core_points = parse_csv_ints(args.total_core_points, "--total-core-points")
    if args.ai_mode != "off":
        parser.error("--ai-mode is currently frozen to off for Suite D main scaling")
    for point in args.total_core_points:
        if point not in DEFAULT_CORE_SPLIT or point not in DEFAULT_TOPOLOGY_MAP:
            parser.error(f"unsupported total core point: {point}")
    return args


def build_cpuset(start_core: int, core_count: int) -> str:
    if core_count <= 0:
        raise ValueError("core_count must be > 0")
    end_core = start_core + core_count - 1
    if start_core == end_core:
        return str(start_core)
    return f"{start_core}-{end_core}"


def derive_worker_queue_size(worker_threads: int) -> int:
    return max(4096, worker_threads * 256)


def derive_trace_active_session_limit(backend_cores: int) -> int:
    if backend_cores <= 6:
        return 512
    if backend_cores <= 10:
        return 1024
    if backend_cores <= 17:
        return 1536
    return 2048


def derive_trace_buffered_span_limit(active_session_limit: int) -> int:
    return active_session_limit * 8


def build_case_args(
    args: argparse.Namespace,
    total_cores: int,
    point_index: int,
) -> argparse.Namespace:
    split = DEFAULT_CORE_SPLIT[total_cores]
    topology = DEFAULT_TOPOLOGY_MAP[total_cores]
    sender_cores = int(split["sender_cores"])
    backend_cores = int(split["backend_cores"])
    wrk_cpuset = build_cpuset(0, sender_cores)
    server_cpuset = build_cpuset(sender_cores, backend_cores)
    worker_queue_size = derive_worker_queue_size(int(topology["worker_threads"]))
    trace_active_session_limit = derive_trace_active_session_limit(backend_cores)
    trace_buffered_span_limit = derive_trace_buffered_span_limit(trace_active_session_limit)

    # 每个总核数点位都落到独立目录。
    # 这样后面回看主曲线时，能直接按 cores_xx 找到该点位的 sqlite/server log/result.json 资产。
    run_root = Path(args.actual_scaling_root) / f"cores_{total_cores:02d}" / "run_01"
    run_root.mkdir(parents=True, exist_ok=True)
    output_json = run_root / "result.json"

    argv = [
        "--run-root",
        str(run_root),
        "--output-json",
        str(output_json),
        "--port-base",
        str(args.port_base + point_index * args.port_stride),
        "--server-cpuset",
        server_cpuset,
        "--wrk-cpuset",
        wrk_cpuset,
        "--wrk-bin",
        args.wrk_bin,
        "--wrk-script",
        args.wrk_script,
        "--wrk-threads",
        str(sender_cores),
        "--connections",
        str(sender_cores * args.connections_per_sender_core),
        "--duration",
        args.duration,
        "--warmup-duration",
        args.warmup_duration,
        "--spans-per-trace",
        str(args.spans_per_trace),
        "--server-io-threads",
        str(topology["server_io_threads"]),
        "--dispatch-worker-threads",
        str(topology["dispatch_worker_threads"]),
        "--worker-threads",
        str(topology["worker_threads"]),
        "--worker-queue-size",
        str(worker_queue_size),
        "--trace-active-session-limit",
        str(trace_active_session_limit),
        "--trace-buffered-span-limit",
        str(trace_buffered_span_limit),
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


def write_summary(output_path: str, summary: JsonDict) -> None:
    output_file = Path(output_path)
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(json.dumps(summary, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")


def run_scaling(
    args: argparse.Namespace,
    case_runner: Callable[[argparse.Namespace], JsonDict] = run_case_once,
    line_writer: Callable[[str], None] = print,
) -> JsonDict:
    ensure_scaling_root_resolved(args)
    Path(args.actual_scaling_root).mkdir(parents=True, exist_ok=True)

    by_total_cores: List[JsonDict] = []
    for index, total_cores in enumerate(args.total_core_points):
        split = DEFAULT_CORE_SPLIT[total_cores]
        topology = DEFAULT_TOPOLOGY_MAP[total_cores]
        case_args = build_case_args(args, total_cores, index)
        case_result = dict(case_runner(case_args))
        # point_summary 是主曲线最终要落盘的标准行。
        # 这里把部署拓扑、窗口内完成量和 drain 尾巴放在同一层，后面画图或写表就不用再回头拼多份 JSON。
        point_summary: JsonDict = {
            "total_cores": total_cores,
            "sender_cores": int(split["sender_cores"]),
            "backend_cores": int(split["backend_cores"]),
            "server_cpuset": case_args.server_cpuset,
            "wrk_cpuset": case_args.wrk_cpuset,
            "server_io_threads": int(topology["server_io_threads"]),
            "dispatch_worker_threads": int(topology["dispatch_worker_threads"]),
            "worker_threads": int(topology["worker_threads"]),
            "worker_queue_size": int(case_args.worker_queue_size),
            "trace_active_session_limit": int(case_args.trace_active_session_limit),
            "trace_buffered_span_limit": int(case_args.trace_buffered_span_limit),
            "wrk_threads": int(case_args.wrk_threads),
            "connections": int(case_args.connections),
            "requested_run_root": case_result["requested_run_root"],
            "actual_run_root": case_result["actual_run_root"],
            "online_completed_traces_per_sec": float(case_result["online_completed_traces_per_sec"]),
            "online_completion_ratio": float(case_result["online_completion_ratio"]),
            "drain_tail_ms": int(case_result["drain_tail_ms"]),
            "drain_timeout": bool(case_result["drain_timeout"]),
            "wrk_metrics": case_result["wrk_metrics"],
        }
        by_total_cores.append(point_summary)
        line_writer(
            f"[point {index + 1}/{len(args.total_core_points)}] "
            f"total={total_cores} "
            f"sender={split['sender_cores']} "
            f"backend={split['backend_cores']} "
            f"online={point_summary['online_completed_traces_per_sec']:.2f} "
            f"ratio={point_summary['online_completion_ratio']:.4f} "
            f"drain={point_summary['drain_tail_ms']} "
            f"qps={float(point_summary['wrk_metrics']['requests_per_sec']):.2f}"
        )

    # 排名先看窗口内完成速率，再看窗口内完成比例，最后才看 drain 尾巴。
    # 这样不会把“尾巴更短但窗口内掉更多 trace”的点位错误排到前面。
    best_point = max(
        by_total_cores,
        key=lambda item: (
            float(item["online_completed_traces_per_sec"]),
            float(item["online_completion_ratio"]),
            -int(item["drain_tail_ms"]),
            int(item["total_cores"]),
        ),
    )
    summary: JsonDict = {
        "requested_scaling_root": args.requested_scaling_root,
        "actual_scaling_root": args.actual_scaling_root,
        "total_core_points": list(args.total_core_points),
        "by_total_cores": by_total_cores,
        "overall": {
            "best_total_cores": int(best_point["total_cores"]),
            "best_online_completed_traces_per_sec": float(best_point["online_completed_traces_per_sec"]),
        },
    }
    output_summary = args.output_summary or str(Path(args.actual_scaling_root) / "summary.json")
    write_summary(output_summary, summary)
    return summary


def main() -> None:
    args = parse_args()
    run_scaling(args)


if __name__ == "__main__":
    main()
