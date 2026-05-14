#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import re
import sqlite3
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional

CURRENT_DIR = Path(__file__).resolve().parent
SUITE_A_DIR = CURRENT_DIR.parent / "suite_a"
COMMON_UTILS_DIR = CURRENT_DIR.parent / "common" / "utils"
for candidate_dir in (SUITE_A_DIR, COMMON_UTILS_DIR):
    if str(candidate_dir) not in sys.path:
        # Suite D 入口是脚本直跑，不是包内相对导入。
        # 这里显式补兄弟目录到 sys.path，避免“单测能 import，真实命令直跑却找不到模块”。
        sys.path.insert(0, str(candidate_dir))

import run_suite_a_case as suite_a_common
from benchmark_metadata import attach_benchmark_metadata, build_cpu_allocation
from trace_sqlite_polling import read_sqlite_counts, wait_until_sqlite_stable


JsonDict = Dict[str, Any]
DEFAULT_WRK_SCRIPT = str(CURRENT_DIR / "trace_model_suite_d.lua")
DEFAULT_TRACE_AI_PROVIDER = "mock"
REQUESTS_RE = re.compile(r"(?P<requests>\d+)\s+requests in\s+(?P<seconds>\d+(?:\.\d+)?)s")
REQUESTS_PER_SEC_RE = re.compile(r"Requests/sec:\s+(?P<qps>\d+(?:\.\d+)?)")
SUITE_D_SUMMARY_RE = re.compile(
    r"trace_model_suite_d metrics:\s+offered_traces=(?P<offered>\d+)\s+"
    r"spans_per_trace=(?P<spans>\d+)\s+"
    r"latency_p95_ms=(?P<p95>\d+(?:\.\d+)?)\s+"
    r"latency_p99_ms=(?P<p99>\d+(?:\.\d+)?)"
)

assert_port_available = suite_a_common.assert_port_available
launch_server_process = suite_a_common.launch_server_process
shell_join = suite_a_common.shell_join
stop_server_process = suite_a_common.stop_server_process
wait_for_port_ready = suite_a_common.wait_for_port_ready


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Suite D single-case runner: auto-start server, run wrk, poll SQLite, write result.json"
    )
    parser.add_argument("--server-command", default="")
    parser.add_argument("--server-bin", default="")
    parser.add_argument("--run-root", required=True)
    parser.add_argument("--output-json", default="")
    parser.add_argument("--sqlite-db", default="")
    parser.add_argument("--server-log", default="")
    parser.add_argument("--port-base", type=int, default=18080)
    parser.add_argument("--server-cpuset", default="")
    parser.add_argument("--wrk-cpuset", default="")
    parser.add_argument("--wrk-bin", default="wrk")
    parser.add_argument("--wrk-script", default=DEFAULT_WRK_SCRIPT)
    parser.add_argument("--wrk-threads", type=int, default=2)
    parser.add_argument("--connections", type=int, default=120)
    parser.add_argument("--duration", default="15s")
    parser.add_argument("--warmup-duration", default="3s")
    parser.add_argument("--spans-per-trace", type=int, default=8)
    parser.add_argument("--server-io-threads", type=int, default=1)
    parser.add_argument("--dispatch-worker-threads", type=int, default=1)
    parser.add_argument("--worker-threads", type=int, default=32)
    parser.add_argument("--worker-queue-size", type=int, default=4096)
    parser.add_argument("--trace-active-session-limit", type=int, default=512)
    parser.add_argument("--trace-buffered-span-limit", type=int, default=4096)
    parser.add_argument("--trace-max-dispatch-per-tick", type=int, default=64)
    parser.add_argument("--trace-lifecycle-profile", default="protected")
    parser.add_argument("--trace-sealed-grace-window-ms", type=int, default=100)
    parser.add_argument("--trace-sweep-interval-ms", type=int, default=200)
    parser.add_argument("--trace-primary-flush-span-threshold", type=int, default=512)
    parser.add_argument("--trace-primary-flush-interval-ms", type=int, default=5)
    parser.add_argument("--trace-ai-provider", default=DEFAULT_TRACE_AI_PROVIDER)
    parser.add_argument("--startup-timeout-sec", type=float, default=10.0)
    parser.add_argument("--stop-timeout-sec", type=float, default=5.0)
    parser.add_argument("--poll-interval-ms", type=int, default=50)
    parser.add_argument("--stable-rounds", type=int, default=3)
    parser.add_argument("--confirm-sleep-ms", type=int, default=100)
    parser.add_argument("--max-drain-wait-ms", type=int, default=30000)
    parser.add_argument("--disable-ai", action="store_true")
    parser.add_argument("--disable-webhook", action="store_true")
    parser.add_argument("--no-auto-start-proxy", action="store_true")
    parser.add_argument("--trace-ai-base-url", default="")
    parser.add_argument("--disable-buffered-trace-repo", action="store_true")
    args = parser.parse_args(argv)

    if not args.server_bin and not args.server_command:
        parser.error("--server-bin or --server-command is required")
    if args.server_io_threads <= 0:
        parser.error("--server-io-threads must be > 0")
    if args.dispatch_worker_threads <= 0:
        parser.error("--dispatch-worker-threads must be > 0")
    if args.worker_threads <= 0:
        parser.error("--worker-threads must be > 0")
    if args.worker_queue_size <= 0:
        parser.error("--worker-queue-size must be > 0")
    if args.trace_active_session_limit <= 0:
        parser.error("--trace-active-session-limit must be > 0")
    if args.trace_buffered_span_limit <= 0:
        parser.error("--trace-buffered-span-limit must be > 0")
    if args.connections <= 0:
        parser.error("--connections must be > 0")
    if args.wrk_threads <= 0:
        parser.error("--wrk-threads must be > 0")
    if args.spans_per_trace <= 0:
        parser.error("--spans-per-trace must be > 0")
    parse_duration_seconds(args.duration)
    parse_duration_seconds(args.warmup_duration)
    args.cli_argv = list(argv) if argv is not None else list(sys.argv[1:])
    return args


def read_trace_analysis_count(sqlite_path: Path) -> int:
    sqlite_uri = f"file:{sqlite_path}?mode=ro"
    # trace_summary 代表主链路已经完成聚合并可查询，trace_analysis 才代表 AI 分析结果已经落库。
    # Suite D AI-on 搜索必须把这两个口径拆开，否则 worker 线程变多时只会看到主链吞吐，漏掉 AI 后链路是否追上。
    conn = sqlite3.connect(sqlite_uri, uri=True, timeout=5.0)
    try:
        conn.execute("PRAGMA busy_timeout = 5000")
        return int(conn.execute("SELECT COUNT(*) FROM trace_analysis").fetchone()[0])
    finally:
        conn.close()


def parse_duration_seconds(value: str) -> float:
    match = re.fullmatch(r"(?P<number>\d+(?:\.\d+)?)(?P<unit>[smh])", value.strip())
    if match is None:
        raise ValueError(f"unsupported duration: {value}")

    number = float(match.group("number"))
    unit = match.group("unit")
    if unit == "s":
        return number
    if unit == "m":
        return number * 60.0
    if unit == "h":
        return number * 3600.0
    raise ValueError(f"unsupported duration unit: {value}")


def resolve_run_artifacts(
    run_root: str,
    output_json: str,
    sqlite_db: str,
    server_log: str,
    now_func: Callable[[], float] = time.time,
) -> tuple[str, str, JsonDict]:
    requested_run_root = str(Path(run_root))
    actual_run_root = f"{requested_run_root}-{suite_a_common.format_run_timestamp(now_func)}"
    actual_root = Path(actual_run_root)
    artifacts = {
        "output_json": output_json or str(actual_root / "result.json"),
        "sqlite_db": sqlite_db or str(actual_root / "suite_d.db"),
        "server_log": server_log or str(actual_root / "server.log"),
    }
    return requested_run_root, actual_run_root, artifacts


def build_server_passthrough_args(args: argparse.Namespace) -> List[str]:
    parts = [
        "--server-io-threads",
        str(args.server_io_threads),
        "--worker-threads",
        str(args.worker_threads),
        "--dispatch-worker-threads",
        str(args.dispatch_worker_threads),
        "--worker-queue-size",
        str(args.worker_queue_size),
        "--trace-active-session-limit",
        str(args.trace_active_session_limit),
        "--trace-buffered-span-limit",
        str(args.trace_buffered_span_limit),
        "--trace-max-dispatch-per-tick",
        str(args.trace_max_dispatch_per_tick),
        "--trace-lifecycle-profile",
        str(args.trace_lifecycle_profile),
        "--trace-sealed-grace-window-ms",
        str(args.trace_sealed_grace_window_ms),
        "--trace-sweep-interval-ms",
        str(args.trace_sweep_interval_ms),
        "--trace-primary-flush-span-threshold",
        str(args.trace_primary_flush_span_threshold),
        "--trace-primary-flush-interval-ms",
        str(args.trace_primary_flush_interval_ms),
    ]

    if args.disable_ai:
        parts.append("--disable-ai")
    else:
        parts.extend(["--trace-ai-provider", str(args.trace_ai_provider)])
        if args.trace_ai_base_url:
            # worker/proxy 搜索会手动拉起独立 AI proxy，再把后端指到这个端口。
            # 这里必须透传 base_url，否则 --no-auto-start-proxy 只能关闭 sidecar，却无法绑定本轮 proxy。
            parts.extend(["--trace-ai-base-url", str(args.trace_ai_base_url)])
        if not args.no_auto_start_proxy:
            parts.append("--auto-start-proxy")

    if args.disable_webhook:
        parts.append("--disable-webhook")
    if args.no_auto_start_proxy:
        parts.append("--no-auto-start-proxy")
    if args.disable_buffered_trace_repo:
        parts.append("--disable-buffered-trace-repo")
    return parts


def resolve_server_command(args: argparse.Namespace, artifacts: JsonDict) -> str:
    if args.server_command:
        base_command = args.server_command.format(
            sqlite_db=artifacts["sqlite_db"],
            port=artifacts["port"],
            log_path=artifacts["server_log"],
            run_dir=artifacts.get("run_root", ""),
        )
        extra_parts = build_server_passthrough_args(args)
        if not extra_parts:
            return base_command
        return f"{base_command} {shell_join(extra_parts)}"

    parts: List[str] = []
    if args.server_cpuset:
        parts.extend(["taskset", "-c", args.server_cpuset])
    parts.extend(
        [
            args.server_bin,
            "--db",
            str(artifacts["sqlite_db"]),
            "--port",
            str(artifacts["port"]),
        ]
    )
    parts.extend(build_server_passthrough_args(args))
    return shell_join(parts)


def build_wrk_env(args: argparse.Namespace) -> Dict[str, str]:
    env = os.environ.copy()
    env["TRACE_WRK_MODE"] = "end"
    env["TRACE_WRK_THREADS"] = str(getattr(args, "wrk_threads", 2))
    return env


def build_wrk_command(args: argparse.Namespace, url: str, duration: str) -> List[str]:
    command: List[str] = []
    wrk_cpuset = getattr(args, "wrk_cpuset", "")
    if wrk_cpuset:
        command.extend(["taskset", "-c", wrk_cpuset])
    command.extend(
        [
            getattr(args, "wrk_bin", "wrk"),
            f"-t{getattr(args, 'wrk_threads', 2)}",
            f"-c{getattr(args, 'connections', 120)}",
            f"-d{duration}",
            "-s",
            str(getattr(args, "wrk_script", DEFAULT_WRK_SCRIPT)),
            url,
            "--",
            "end",
            str(getattr(args, "spans_per_trace", 8)),
            "4",
            "2048",
        ]
    )
    return command


def run_wrk_command(command: List[str], env: Dict[str, str]) -> str:
    completed = subprocess.run(command, env=env, check=True, capture_output=True, text=True)
    return f"{completed.stdout}{completed.stderr}"


def parse_wrk_metrics(output_text: str) -> JsonDict:
    requests_match = REQUESTS_RE.search(output_text)
    qps_match = REQUESTS_PER_SEC_RE.search(output_text)
    summary_match = SUITE_D_SUMMARY_RE.search(output_text)
    if requests_match is None or qps_match is None or summary_match is None:
        raise ValueError("failed to parse wrk output for Suite D metrics")

    return {
        "requests": int(requests_match.group("requests")),
        "duration_seconds": float(requests_match.group("seconds")),
        "requests_per_sec": float(qps_match.group("qps")),
        "offered_traces": int(summary_match.group("offered")),
        "spans_per_trace": int(summary_match.group("spans")),
        "latency_p95_ms": float(summary_match.group("p95")),
        "latency_p99_ms": float(summary_match.group("p99")),
    }


def write_result_json(output_path: str, result: JsonDict) -> None:
    output_file = Path(output_path)
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(json.dumps(result, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")


def enrich_case_result_with_metadata(runtime_args: argparse.Namespace, result: JsonDict) -> JsonDict:
    return attach_benchmark_metadata(
        payload=result,
        suite_name="suite_d",
        entry_script=__file__,
        workload={
            "connections": runtime_args.connections,
            "wrk_threads": runtime_args.wrk_threads,
            "duration": runtime_args.duration,
            "warmup_duration": runtime_args.warmup_duration,
            "spans_per_trace": runtime_args.spans_per_trace,
        },
        cpu_allocation=build_cpu_allocation(
            server_cpuset=getattr(runtime_args, "server_cpuset", ""),
            wrk_cpuset=getattr(runtime_args, "wrk_cpuset", ""),
        ),
        thread_topology={
            "server_io_threads": runtime_args.server_io_threads,
            "dispatch_worker_threads": runtime_args.dispatch_worker_threads,
            "worker_threads": runtime_args.worker_threads,
            "worker_queue_size": runtime_args.worker_queue_size,
            "trace_active_session_limit": runtime_args.trace_active_session_limit,
            "trace_buffered_span_limit": runtime_args.trace_buffered_span_limit,
            "trace_max_dispatch_per_tick": runtime_args.trace_max_dispatch_per_tick,
        },
        effective_flags={
            "trace_lifecycle_profile": runtime_args.trace_lifecycle_profile,
            "trace_sealed_grace_window_ms": runtime_args.trace_sealed_grace_window_ms,
            "trace_sweep_interval_ms": runtime_args.trace_sweep_interval_ms,
            "trace_primary_flush_span_threshold": runtime_args.trace_primary_flush_span_threshold,
            "trace_primary_flush_interval_ms": runtime_args.trace_primary_flush_interval_ms,
            "disable_ai": runtime_args.disable_ai,
            "disable_webhook": runtime_args.disable_webhook,
            "disable_buffered_trace_repo": runtime_args.disable_buffered_trace_repo,
            "no_auto_start_proxy": runtime_args.no_auto_start_proxy,
            "trace_ai_provider": runtime_args.trace_ai_provider,
            "trace_ai_base_url": runtime_args.trace_ai_base_url,
        },
        commands={
            "argv": list(getattr(runtime_args, "cli_argv", [])),
            "server_command": runtime_args.resolved_server_command,
            "wrk_command_warmup": build_wrk_command(runtime_args, runtime_args.url, runtime_args.warmup_duration),
            "wrk_command_measurement": build_wrk_command(runtime_args, runtime_args.url, runtime_args.duration),
        },
        artifacts={
            "result_json": runtime_args.output_json,
            "requested_run_root": runtime_args.requested_run_root,
            "actual_run_root": runtime_args.actual_run_root,
            "sqlite_db": runtime_args.sqlite_db,
            "server_log": runtime_args.server_log,
        },
    )


def run_suite_d_case(
    args: argparse.Namespace,
    wrk_runner: Callable[[List[str], Dict[str, str]], str] = run_wrk_command,
    sqlite_counter: Callable[[Path], JsonDict] = read_sqlite_counts,
    analysis_counter: Callable[[Path], int] = read_trace_analysis_count,
    wait_for_stable_runner: Callable[..., JsonDict] = wait_until_sqlite_stable,
) -> JsonDict:
    process_info: Optional[JsonDict] = None
    requested_run_root, actual_run_root, artifacts = resolve_run_artifacts(
        run_root=args.run_root,
        output_json=args.output_json,
        sqlite_db=args.sqlite_db,
        server_log=args.server_log,
    )
    Path(actual_run_root).mkdir(parents=True, exist_ok=True)

    runtime_args = argparse.Namespace(**vars(args))
    runtime_args.requested_run_root = requested_run_root
    runtime_args.actual_run_root = actual_run_root
    runtime_args.output_json = artifacts["output_json"]
    runtime_args.sqlite_db = artifacts["sqlite_db"]
    runtime_args.server_log = artifacts["server_log"]
    runtime_args.url = f"http://127.0.0.1:{args.port_base}/logs/spans"
    artifacts["port"] = args.port_base
    artifacts["run_root"] = actual_run_root
    command = resolve_server_command(args, artifacts)
    runtime_args.server_command = command
    runtime_args.resolved_server_command = command

    try:
        assert_port_available(args.port_base)
        process_info = launch_server_process(command, Path(runtime_args.server_log))
        wait_for_port_ready(args.port_base, args.startup_timeout_sec)

        wrk_env = build_wrk_env(runtime_args)
        if parse_duration_seconds(runtime_args.warmup_duration) > 0:
            wrk_runner(build_wrk_command(runtime_args, runtime_args.url, runtime_args.warmup_duration), wrk_env)

        measurement_output = wrk_runner(
            build_wrk_command(runtime_args, runtime_args.url, runtime_args.duration),
            wrk_env,
        )
        # t_stop_ms 用墙钟写进结果 JSON，方便和 server.log、云机时间线对齐。
        # drain_start_ms 必须用 monotonic 记录耗时起点，因为 wait_until_sqlite_stable 内部也用 monotonic；
        # 如果把 epoch ms 混进去，drain_tail_ms 会被负数压成 0，导致 Suite D 的尾巴指标假好看。
        t_stop_ms = int(time.time() * 1000)
        drain_start_ms = int(time.monotonic() * 1000)
        wrk_metrics = parse_wrk_metrics(measurement_output)

        sqlite_path = Path(runtime_args.sqlite_db)
        stop_counts = sqlite_counter(sqlite_path)
        # stop_counts 只代表“wrk 停止这一刻 SQLite 已经看见多少 trace”，也就是在线窗口内完成量。
        # 这里不能直接把它当 final，因为 buffered repo / dispatch / shutdown drain 还可能继续把尾巴补进去。
        # Suite D 当前又存在 non-2xx/503，所以 offered_traces 不是“最终一定要追满的真值”。
        # 真正的 final 必须先停服，让进程把退出路径上的 drain 做完，再按“SQLite 是否稳定”收最终计数。
        stop_server_process(process_info, args.stop_timeout_sec)
        process_info = None
        stable = wait_for_stable_runner(
            sqlite_path,
            poll_interval_ms=runtime_args.poll_interval_ms,
            stable_rounds=runtime_args.stable_rounds,
            confirm_sleep_ms=runtime_args.confirm_sleep_ms,
            max_wait_ms=runtime_args.max_drain_wait_ms,
            start_ms=drain_start_ms,
        )
        final_trace_analysis_count = analysis_counter(sqlite_path)

        measurement_seconds = parse_duration_seconds(runtime_args.duration)
        visible_trace_count = int(stop_counts["trace_summary"])
        # analysis_count 是 AI 后链路真正落库的数量。
        # 它通常会明显小于 trace_summary，因为主摘要先落库，AI worker 再慢慢调用 proxy 并写 trace_analysis。
        final_trace_count = int(stable["final_counts"].get("trace_summary") or 0)
        offered_traces = int(wrk_metrics["offered_traces"])
        result: JsonDict = {
            "requested_run_root": requested_run_root,
            "actual_run_root": actual_run_root,
            "output_json": runtime_args.output_json,
            "sqlite_db": runtime_args.sqlite_db,
            "server_log": runtime_args.server_log,
            "server_cpuset": runtime_args.server_cpuset,
            "wrk_cpuset": runtime_args.wrk_cpuset,
            "server_io_threads": runtime_args.server_io_threads,
            "dispatch_worker_threads": runtime_args.dispatch_worker_threads,
            "worker_threads": runtime_args.worker_threads,
            "worker_queue_size": runtime_args.worker_queue_size,
            "trace_active_session_limit": runtime_args.trace_active_session_limit,
            "trace_buffered_span_limit": runtime_args.trace_buffered_span_limit,
            "trace_max_dispatch_per_tick": runtime_args.trace_max_dispatch_per_tick,
            "trace_lifecycle_profile": runtime_args.trace_lifecycle_profile,
            "trace_sealed_grace_window_ms": runtime_args.trace_sealed_grace_window_ms,
            "trace_sweep_interval_ms": runtime_args.trace_sweep_interval_ms,
            "trace_primary_flush_span_threshold": runtime_args.trace_primary_flush_span_threshold,
            "trace_primary_flush_interval_ms": runtime_args.trace_primary_flush_interval_ms,
            "trace_ai_provider": runtime_args.trace_ai_provider,
            "trace_ai_base_url": runtime_args.trace_ai_base_url,
            "connections": runtime_args.connections,
            "wrk_threads": runtime_args.wrk_threads,
            "warmup_duration": runtime_args.warmup_duration,
            "duration": runtime_args.duration,
            "spans_per_trace": runtime_args.spans_per_trace,
            "resolved_server_command": runtime_args.resolved_server_command,
            "t_stop_ms": t_stop_ms,
            "wrk_metrics": wrk_metrics,
            "sqlite_counts_at_stop": stop_counts,
            "sqlite_counts_final": stable["final_counts"],
            "online_completed_traces_per_sec": visible_trace_count / measurement_seconds,
            "online_completion_ratio": (visible_trace_count / offered_traces) if offered_traces > 0 else 0.0,
            "final_trace_analysis_count": final_trace_analysis_count,
            "online_ai_completed_traces_per_sec": final_trace_analysis_count / measurement_seconds,
            "final_ai_completion_ratio": (
                final_trace_analysis_count / final_trace_count
            ) if final_trace_count > 0 else 0.0,
            "final_ai_offered_ratio": (
                final_trace_analysis_count / offered_traces
            ) if offered_traces > 0 else 0.0,
            "drain_tail_ms": int(stable["drain_tail_ms"]),
            "drain_timeout": bool(stable["drain_timeout"]),
        }
        result = enrich_case_result_with_metadata(runtime_args, result)
        write_result_json(runtime_args.output_json, result)
        return result
    finally:
        if process_info is not None:
            stop_server_process(process_info, args.stop_timeout_sec)


def main() -> None:
    args = parse_args()
    result = run_suite_d_case(args)
    offered_traces = int(result["wrk_metrics"]["offered_traces"])
    stop_traces = int(result["sqlite_counts_at_stop"]["trace_summary"])
    final_traces = int(result["sqlite_counts_final"]["trace_summary"])
    final_analysis = int(result.get("final_trace_analysis_count") or 0)
    # stdout 只打印能解释主曲线的关键字段。
    # 完整机器信息、命令和 SQLite 路径仍然在 result.json 里，避免云机终端被大 JSON 刷屏。
    print(
        "[suite_d_case] "
        f"online={result['online_completed_traces_per_sec']:.2f} "
        f"ratio={result['online_completion_ratio']:.4f} "
        f"drain={result['drain_tail_ms']} "
        f"qps={result['wrk_metrics']['requests_per_sec']:.2f} "
        f"stop={stop_traces}/{offered_traces} "
        f"final={final_traces}/{offered_traces} "
        f"analysis={final_analysis}/{final_traces}"
    )


if __name__ == "__main__":
    main()
