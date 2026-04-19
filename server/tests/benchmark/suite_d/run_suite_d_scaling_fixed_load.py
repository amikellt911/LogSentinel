#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional

CURRENT_DIR = Path(__file__).resolve().parent
COMMON_UTILS_DIR = CURRENT_DIR.parent / "common" / "utils"
for candidate_dir in (CURRENT_DIR, COMMON_UTILS_DIR):
    if str(candidate_dir) not in sys.path:
        sys.path.insert(0, str(candidate_dir))

import run_suite_d_case
from benchmark_metadata import attach_benchmark_metadata


JsonDict = Dict[str, Any]
DEFAULT_SCALING_ROOT = "server/tests/benchmark/results/suite_d/scaling_fixed_load"
DEFAULT_BACKEND_CORE_POINTS = [4, 8, 12, 16, 20, 24]
DEFAULT_WRK_CPUSET = "0-3"
DEFAULT_WRK_THREADS = 4
DEFAULT_CONNECTIONS = 90
DEFAULT_BACKEND_CORE_OFFSET = 4

# fixed-load 曲线只改变后端可用核数，sender/wrk 输入负载保持不变。
# 线程拓扑仍按 backend 核数分档，因为这里衡量的是“固定输入下后端部署规模变化后的完成能力”。
DEFAULT_BACKEND_TOPOLOGY_MAP: Dict[int, JsonDict] = {
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


def parse_optional_positive_int(value: Any, option_name: str) -> Optional[int]:
    # 环境变量和 CLI 都先按字符串进来；这里统一把空值视为“不覆写”。
    # 这样 wrapper 不需要拼复杂参数，也能用 SUITE_D_FORCE_* 在远端快速做拓扑探针。
    raw_value = "" if value is None else str(value).strip()
    if raw_value == "":
        return None
    try:
        parsed = int(raw_value)
    except ValueError as exc:
        raise ValueError(f"{option_name} must be a positive integer") from exc
    if parsed <= 0:
        raise ValueError(f"{option_name} must be a positive integer")
    return parsed


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


def forced_topology_from_args(args: argparse.Namespace) -> JsonDict:
    # 只返回用户明确指定的线程字段，summary 里可以直接看出本轮是否用了诊断拓扑。
    # 没指定的字段继续走 DEFAULT_BACKEND_TOPOLOGY_MAP，避免无意改变旧 fixed-load 口径。
    forced: JsonDict = {}
    if args.force_server_io_threads is not None:
        forced["server_io_threads"] = int(args.force_server_io_threads)
    if args.force_dispatch_worker_threads is not None:
        forced["dispatch_worker_threads"] = int(args.force_dispatch_worker_threads)
    if args.force_worker_threads is not None:
        forced["worker_threads"] = int(args.force_worker_threads)
    return forced


def resolve_backend_topology(args: argparse.Namespace, backend_cores: int) -> JsonDict:
    topology = dict(DEFAULT_BACKEND_TOPOLOGY_MAP[backend_cores])
    # 覆写只改变线程拓扑，不改变 backend_cores 对应的 CPU 资源窗口。
    # 这样可以专门验证“24 核资源 + 较保守线程数”是否缓解锁竞争和 flush 积压。
    topology.update(forced_topology_from_args(args))
    return topology


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Suite D fixed-load scaling runner: 固定 wrk/sender 输入，只扫后端核心数"
    )
    parser.add_argument("--scaling-root", default=DEFAULT_SCALING_ROOT)
    parser.add_argument("--output-summary", default="")
    parser.add_argument(
        "--backend-core-points",
        default=",".join(str(item) for item in DEFAULT_BACKEND_CORE_POINTS),
    )
    parser.add_argument("--port-base", type=int, default=18680)
    parser.add_argument("--port-stride", type=int, default=1)
    parser.add_argument("--server-bin", default="./server/build/LogSentinel")
    parser.add_argument("--server-command", default="")
    parser.add_argument("--wrk-bin", default="wrk")
    parser.add_argument("--wrk-script", default=run_suite_d_case.DEFAULT_WRK_SCRIPT)
    parser.add_argument("--wrk-cpuset", default=DEFAULT_WRK_CPUSET)
    parser.add_argument("--wrk-threads", type=int, default=DEFAULT_WRK_THREADS)
    parser.add_argument("--connections", type=int, default=DEFAULT_CONNECTIONS)
    parser.add_argument("--backend-core-offset", type=int, default=DEFAULT_BACKEND_CORE_OFFSET)
    parser.add_argument("--duration", default="15s")
    parser.add_argument("--warmup-duration", default="3s")
    parser.add_argument("--spans-per-trace", type=int, default=8)
    parser.add_argument("--trace-max-dispatch-per-tick", type=int, default=256)
    parser.add_argument("--trace-lifecycle-profile", default="protected")
    parser.add_argument("--trace-sealed-grace-window-ms", type=int, default=100)
    parser.add_argument("--trace-sweep-interval-ms", type=int, default=20)
    parser.add_argument("--trace-primary-flush-span-threshold", type=int, default=512)
    parser.add_argument("--trace-primary-flush-interval-ms", type=int, default=5)
    parser.add_argument(
        "--force-server-io-threads",
        default=os.environ.get("SUITE_D_FORCE_SERVER_IO_THREADS", ""),
        help="诊断用：只覆写 server I/O 线程数，不改变 backend cpuset",
    )
    parser.add_argument(
        "--force-dispatch-worker-threads",
        default=os.environ.get("SUITE_D_FORCE_DISPATCH_WORKER_THREADS", ""),
        help="诊断用：只覆写 trace dispatch 线程数，不改变 backend cpuset",
    )
    parser.add_argument(
        "--force-worker-threads",
        default=os.environ.get("SUITE_D_FORCE_WORKER_THREADS", ""),
        help="诊断用：只覆写 worker 线程数，不改变 backend cpuset",
    )
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
    if args.wrk_threads <= 0:
        parser.error("--wrk-threads must be > 0")
    if args.connections <= 0:
        parser.error("--connections must be > 0")
    if args.backend_core_offset < 0:
        parser.error("--backend-core-offset must be >= 0")
    if args.ai_mode != "off":
        parser.error("--ai-mode is currently frozen to off for Suite D fixed-load scaling")
    args.backend_core_points = parse_csv_ints(args.backend_core_points, "--backend-core-points")
    for point in args.backend_core_points:
        if point not in DEFAULT_BACKEND_TOPOLOGY_MAP:
            parser.error(f"unsupported backend core point: {point}")
    for attr_name, option_name in (
        ("force_server_io_threads", "--force-server-io-threads"),
        ("force_dispatch_worker_threads", "--force-dispatch-worker-threads"),
        ("force_worker_threads", "--force-worker-threads"),
    ):
        try:
            setattr(args, attr_name, parse_optional_positive_int(getattr(args, attr_name), option_name))
        except ValueError as exc:
            parser.error(str(exc))
    args.cli_argv = list(argv) if argv is not None else list(sys.argv[1:])
    return args


def build_case_args(
    args: argparse.Namespace,
    backend_cores: int,
    point_index: int,
) -> argparse.Namespace:
    topology = resolve_backend_topology(args, backend_cores)
    # sender/wrk 始终固定在同一段 CPU 上；后端从 backend_core_offset 开始扩张。
    # 这样每个点面对同一份外部输入压力，drain 和完成率才有公平比较意义。
    server_cpuset = build_cpuset(args.backend_core_offset, backend_cores)
    worker_queue_size = derive_worker_queue_size(int(topology["worker_threads"]))
    trace_active_session_limit = derive_trace_active_session_limit(backend_cores)
    trace_buffered_span_limit = derive_trace_buffered_span_limit(trace_active_session_limit)

    run_root = Path(args.actual_scaling_root) / f"backend_{backend_cores:02d}" / "run_01"
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


def run_fixed_load_scaling(
    args: argparse.Namespace,
    case_runner: Callable[[argparse.Namespace], JsonDict] = run_case_once,
    line_writer: Callable[[str], None] = print,
) -> JsonDict:
    ensure_scaling_root_resolved(args)
    Path(args.actual_scaling_root).mkdir(parents=True, exist_ok=True)

    by_backend_cores: List[JsonDict] = []
    for index, backend_cores in enumerate(args.backend_core_points):
        topology = resolve_backend_topology(args, backend_cores)
        case_args = build_case_args(args, backend_cores, index)
        case_result = dict(case_runner(case_args))
        # 每行同时保留固定输入负载和后端拓扑。
        # 后续写论文时可以直接说明“wrk 不变，server_cpuset 递增”，不用再从命令行反推。
        point_summary: JsonDict = {
            "backend_cores": backend_cores,
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
        by_backend_cores.append(point_summary)
        line_writer(
            f"[point {index + 1}/{len(args.backend_core_points)}] "
            f"backend={backend_cores} "
            f"wrk={case_args.wrk_cpuset} "
            f"server={case_args.server_cpuset} "
            f"online={point_summary['online_completed_traces_per_sec']:.2f} "
            f"ratio={point_summary['online_completion_ratio']:.4f} "
            f"drain={point_summary['drain_tail_ms']} "
            f"qps={float(point_summary['wrk_metrics']['requests_per_sec']):.2f}"
        )

    best_point = max(
        by_backend_cores,
        key=lambda item: (
            float(item["online_completed_traces_per_sec"]),
            float(item["online_completion_ratio"]),
            -int(item["drain_tail_ms"]),
            int(item["backend_cores"]),
        ),
    )
    summary: JsonDict = {
        "requested_scaling_root": args.requested_scaling_root,
        "actual_scaling_root": args.actual_scaling_root,
        "backend_core_points": list(args.backend_core_points),
        "fixed_load": {
            "wrk_cpuset": args.wrk_cpuset,
            "wrk_threads": int(args.wrk_threads),
            "connections": int(args.connections),
            "backend_core_offset": int(args.backend_core_offset),
            "forced_topology": forced_topology_from_args(args),
        },
        "by_backend_cores": by_backend_cores,
        "overall": {
            "best_backend_cores": int(best_point["backend_cores"]),
            "best_online_completed_traces_per_sec": float(best_point["online_completed_traces_per_sec"]),
        },
    }
    output_summary = args.output_summary or str(Path(args.actual_scaling_root) / "summary.json")
    summary = attach_benchmark_metadata(
        payload=summary,
        suite_name="suite_d",
        entry_script=__file__,
        workload={
            "backend_core_points": list(args.backend_core_points),
            "wrk_cpuset": args.wrk_cpuset,
            "wrk_threads": int(args.wrk_threads),
            "connections": int(args.connections),
            "duration": args.duration,
            "warmup_duration": args.warmup_duration,
            "spans_per_trace": args.spans_per_trace,
        },
        cpu_allocation={
            "wrk_cpuset": args.wrk_cpuset,
            "backend_core_offset": int(args.backend_core_offset),
        },
        thread_topology={
            "backend_topology_map": DEFAULT_BACKEND_TOPOLOGY_MAP,
            "forced_topology": forced_topology_from_args(args),
            "trace_max_dispatch_per_tick": args.trace_max_dispatch_per_tick,
        },
        effective_flags={
            "trace_lifecycle_profile": args.trace_lifecycle_profile,
            "trace_sealed_grace_window_ms": args.trace_sealed_grace_window_ms,
            "trace_sweep_interval_ms": args.trace_sweep_interval_ms,
            "trace_primary_flush_span_threshold": args.trace_primary_flush_span_threshold,
            "trace_primary_flush_interval_ms": args.trace_primary_flush_interval_ms,
            "ai_mode": args.ai_mode,
        },
        commands={
            "argv": list(getattr(args, "cli_argv", [])),
            "server_command_template": args.server_command,
            "server_bin": args.server_bin,
            "wrk_bin": args.wrk_bin,
            "wrk_script": args.wrk_script,
        },
        artifacts={
            "summary_json": output_summary,
            "requested_scaling_root": args.requested_scaling_root,
            "actual_scaling_root": args.actual_scaling_root,
        },
    )
    write_summary(output_summary, summary)
    return summary


def main() -> None:
    args = parse_args()
    run_fixed_load_scaling(args)


if __name__ == "__main__":
    main()
