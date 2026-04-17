#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import shlex
import signal
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Dict, Optional

COMMON_UTILS_DIR = Path(__file__).resolve().parents[1] / "common" / "utils"
if str(COMMON_UTILS_DIR) not in sys.path:
    # 这个脚本平时就是按 `python3 server/tests/benchmark/suite_a/run_suite_a_case.py` 直接执行。
    # 直接跑脚本时，Python 默认只把当前脚本目录塞进 sys.path，不会自动看到兄弟目录里的共享 helper。
    # 这里手动补路径，是为了让 Suite A 先稳定复用 common helper，而不是被导入路径问题卡死。
    sys.path.insert(0, str(COMMON_UTILS_DIR))

from trace_sqlite_polling import read_sqlite_counts, wait_until_sqlite_stable
from benchmark_metadata import attach_benchmark_metadata, build_cpu_allocation


JsonDict = Dict[str, Any]


@dataclass
class SenderStats:
    total_requests: int = 0
    success_requests: int = 0
    non_2xx_requests: int = 0
    transport_errors: int = 0


def parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Suite A fixed clean sender + SQLite evaluator")
    # 如果提供 server-bin/run-root，这个脚本会自己起后端并自动派生 url/sqlite-db/output-json/server-log。
    # 如果不提供，就走“只发流量 + 只读 SQLite”的手动模式。
    parser.add_argument("--url", default="")
    parser.add_argument("--sqlite-db", default="")
    parser.add_argument("--server-command", default="")
    parser.add_argument("--server-bin", default="")
    parser.add_argument("--run-root", default="")
    parser.add_argument("--port-base", type=int, default=18080)
    parser.add_argument("--server-log", default="")
    parser.add_argument("--server-cpuset", default="")
    parser.add_argument("--server-io-threads", type=int, default=1)
    parser.add_argument("--worker-threads", type=int, default=32)
    parser.add_argument("--dispatch-worker-threads", type=int, default=1)
    # 这几项不是 sender/evaluator 自己消费的逻辑，而是 auto-start 模式下继续往后端透传的 benchmark 开关。
    # Stage 1 搜索脚本会靠它们批量切 lifecycle/sweep/flush/AI on-off。
    parser.add_argument("--disable-ai", action="store_true")
    parser.add_argument("--disable-webhook", action="store_true")
    parser.add_argument("--disable-buffered-trace-repo", action="store_true")
    parser.add_argument("--trace-lifecycle-profile", default="")
    parser.add_argument("--trace-sealed-grace-window-ms", type=int, default=0)
    parser.add_argument("--trace-sweep-interval-ms", type=int, default=0)
    parser.add_argument("--trace-primary-flush-span-threshold", type=int, default=0)
    parser.add_argument("--trace-primary-flush-interval-ms", type=int, default=0)
    parser.add_argument("--startup-timeout-sec", type=float, default=10.0)
    parser.add_argument("--stop-timeout-sec", type=float, default=5.0)
    # 这三项固定 sender 的流量形状：
    # trace 总数、每条 trace 的 span 数，以及相邻 trace 的起始间隔。
    parser.add_argument("--trace-count", type=int, required=True)
    parser.add_argument("--spans-per-trace", type=int, default=8)
    parser.add_argument("--inter-trace-gap-ms", type=int, required=True)
    # send_workers 只控制发送端并发，不等于后端 worker 线程数。
    parser.add_argument("--send-workers", type=int, default=1)
    parser.add_argument("--request-timeout-ms", type=int, default=1000)
    parser.add_argument("--service-name", default="svc-suite-a")
    # 这几项只服务 SQLite 主数据排空等待，不参与发送速率控制。
    parser.add_argument("--poll-interval-ms", type=int, default=200)
    parser.add_argument("--stable-rounds", type=int, default=5)
    parser.add_argument("--confirm-sleep-ms", type=int, default=300)
    parser.add_argument("--max-drain-wait-ms", type=int, default=30000)
    parser.add_argument("--output-json", default="")
    args = parser.parse_args(argv)

    # 两种模式二选一：
    # 1) 手动模式：用户自己起后端，显式传 url/sqlite-db；
    # 2) auto-start 模式：传 server-bin 或 server-command，由脚本自动派生路径并起停后端。
    autostart_enabled = bool(args.server_bin or args.server_command)
    if autostart_enabled:
        if not args.run_root:
            parser.error("--run-root is required when --server-bin/--server-command is used")
    else:
        if not args.url or not args.sqlite_db:
            parser.error("--url and --sqlite-db are required when auto-start is not used")

    args.cli_argv = list(argv) if argv is not None else list(sys.argv[1:])
    return args


def format_run_timestamp(now_func: Callable[[], float] = time.time) -> str:
    now = now_func()
    seconds = int(now)
    millis = int(round((now - seconds) * 1000))
    if millis >= 1000:
        seconds += 1
        millis = 0
    return f"{time.strftime('%Y%m%d-%H%M%S', time.localtime(seconds))}-{millis:03d}ms"


def resolve_run_artifacts(
    run_root: str,
    output_json: str,
    sqlite_db: str,
    server_log: str,
    now_func: Callable[[], float] = time.time,
) -> tuple[str, str, JsonDict]:
    requested_run_root = str(Path(run_root))
    actual_run_root = f"{requested_run_root}-{format_run_timestamp(now_func)}"
    actual_root = Path(actual_run_root)
    artifacts = {
        "output_json": output_json or str(actual_root / "result.json"),
        "sqlite_db": sqlite_db or str(actual_root / "suite_a.db"),
        "server_log": server_log or str(actual_root / "server.log"),
    }
    return requested_run_root, actual_run_root, artifacts


def shell_join(parts: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in parts)


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

    parts: list[str] = []
    if args.server_cpuset:
        parts.extend(["taskset", "-c", args.server_cpuset])

    server_bin_tokens = shlex.split(args.server_bin)
    if not server_bin_tokens:
        raise ValueError("--server-bin must not be empty when auto-start is enabled")
    parts.extend(server_bin_tokens)

    # 当前 baseline 的默认启动口径直接写死在这里：
    # AI 开且 provider=mock，webhook 关，避免每次探测都手工拼一大段命令。
    parts.extend(
        [
            "--db",
            str(artifacts["sqlite_db"]),
            "--port",
            str(artifacts["port"]),
            "--trace-ai-provider",
            "mock",
            "--auto-start-proxy",
            "--disable-webhook",
            "--server-io-threads",
            str(args.server_io_threads),
            "--worker-threads",
            str(args.worker_threads),
            "--dispatch-worker-threads",
            str(args.dispatch_worker_threads),
        ]
    )
    parts.extend(build_server_passthrough_args(args))
    return shell_join(parts)


def build_server_passthrough_args(args: argparse.Namespace) -> list[str]:
    parts: list[str] = []
    if getattr(args, "disable_ai", False):
        parts.append("--disable-ai")
    if getattr(args, "disable_webhook", False):
        parts.append("--disable-webhook")
    if getattr(args, "disable_buffered_trace_repo", False):
        parts.append("--disable-buffered-trace-repo")
    if getattr(args, "trace_lifecycle_profile", ""):
        parts.extend(["--trace-lifecycle-profile", args.trace_lifecycle_profile])
    if getattr(args, "trace_sealed_grace_window_ms", 0) > 0:
        parts.extend(
            [
                "--trace-sealed-grace-window-ms",
                str(args.trace_sealed_grace_window_ms),
            ]
        )
    if getattr(args, "trace_sweep_interval_ms", 0) > 0:
        parts.extend(["--trace-sweep-interval-ms", str(args.trace_sweep_interval_ms)])
    if getattr(args, "trace_primary_flush_span_threshold", 0) > 0:
        parts.extend(
            [
                "--trace-primary-flush-span-threshold",
                str(args.trace_primary_flush_span_threshold),
            ]
        )
    if getattr(args, "trace_primary_flush_interval_ms", 0) > 0:
        parts.extend(
            [
                "--trace-primary-flush-interval-ms",
                str(args.trace_primary_flush_interval_ms),
            ]
        )
    return parts


def launch_server_process(command: str, log_path: Path) -> JsonDict:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_file = log_path.open("w", encoding="utf-8")
    process = subprocess.Popen(
        command,
        shell=True,
        stdout=log_file,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    return {
        "process": process,
        "log_file": log_file,
        "log_path": str(log_path),
    }


def assert_port_available(port: int, host: str = "127.0.0.1") -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.2)
        if sock.connect_ex((host, port)) == 0:
            raise RuntimeError(f"port {port} is already occupied before starting Suite A server")


def wait_for_port_ready(port: int, timeout_sec: float, host: str = "127.0.0.1") -> None:
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.2)
            if sock.connect_ex((host, port)) == 0:
                return
        time.sleep(0.1)
    raise RuntimeError(f"server on port {port} did not become ready before timeout")


def stop_server_process(process_info: JsonDict, timeout_sec: float) -> None:
    process = process_info["process"]
    log_file = process_info["log_file"]
    try:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=timeout_sec)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=timeout_sec)
    finally:
        log_file.close()


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
) -> JsonDict:
    logical_now_ms = int(time.time() * 1000)
    payload: JsonDict = {
        "trace_key": trace_key,
        "span_id": span_id,
        # /logs/spans 入口会在 HTTP Handler 层校验 start_time_ms。
        # 这里如果漏掉，请求会在进 TraceSessionManager 之前就被 400 拒掉，看起来像“后端完全没处理”。
        "start_time_ms": logical_now_ms,
        "name": f"suite-a-span-{span_id}",
        "service_name": service_name,
        # Suite A 只做 clean 流量，所以最后一个 span 直接作为 trace_end。
        "trace_end": span_id >= spans_per_trace,
        "attributes": {
            "bench_suite": "suite_a",
            "sender": "run_suite_a_case.py",
            "traffic_profile": "clean_fixed_sender",
        },
    }
    if span_id > 1:
        payload["parent_span_id"] = span_id - 1
    return payload


def post_json(
    opener: urllib.request.OpenerDirector,
    url: str,
    headers: Dict[str, str],
    payload: JsonDict,
    timeout_sec: float,
) -> int:
    body = json.dumps(payload, ensure_ascii=True).encode("utf-8")
    request = urllib.request.Request(url=url, data=body, headers=headers, method="POST")
    try:
        with opener.open(request, timeout=timeout_sec) as response:
            return int(response.getcode())
    except urllib.error.HTTPError as exc:
        return int(exc.code)


def send_clean_traces(args: argparse.Namespace) -> JsonDict:
    opener = urllib.request.build_opener()
    headers = build_headers()
    timeout_sec = args.request_timeout_ms / 1000.0
    stats = SenderStats()
    stats_lock = threading.Lock()
    start_monotonic = time.monotonic()

    def send_one_trace(trace_key: int) -> None:
        # 这里按 trace 粒度调度，而不是把 span 扔成全局乱序队列。
        # Suite A 的目标不是制造脏时序，而是给 compare_target/baseline 施加相同 clean 主链压力。
        scheduled_start = start_monotonic + ((trace_key - 1) * args.inter_trace_gap_ms / 1000.0)
        remaining = scheduled_start - time.monotonic()
        if remaining > 0:
            time.sleep(remaining)

        local_total = 0
        local_success = 0
        local_non_2xx = 0
        local_transport_errors = 0

        for span_id in range(1, args.spans_per_trace + 1):
            payload = build_span_payload(
                trace_key=trace_key,
                span_id=span_id,
                spans_per_trace=args.spans_per_trace,
                service_name=args.service_name,
            )
            local_total += 1
            try:
                status_code = post_json(
                    opener=opener,
                    url=args.url,
                    headers=headers,
                    payload=payload,
                    timeout_sec=timeout_sec,
                )
            except Exception:
                local_transport_errors += 1
                continue

            if 200 <= status_code < 300:
                local_success += 1
            else:
                local_non_2xx += 1

        with stats_lock:
            stats.total_requests += local_total
            stats.success_requests += local_success
            stats.non_2xx_requests += local_non_2xx
            stats.transport_errors += local_transport_errors

    with ThreadPoolExecutor(max_workers=args.send_workers) as executor:
        futures = [executor.submit(send_one_trace, trace_key) for trace_key in range(1, args.trace_count + 1)]
        for future in futures:
            future.result()

    # t_stop 只认“最后一条 trace 的最后一个 span 发送阶段结束”。
    # 它不等 SQLite，不等 AI，不等后端排空；后面的 drain_tail_ms 就是专门量这段尾巴的。
    t_stop_ms = int(time.time() * 1000)
    return {
        "trace_count": args.trace_count,
        "spans_per_trace": args.spans_per_trace,
        "inter_trace_gap_ms": args.inter_trace_gap_ms,
        "t_stop_ms": t_stop_ms,
        "sender_stats": {
            "total_requests": stats.total_requests,
            "success_requests": stats.success_requests,
            "non_2xx_requests": stats.non_2xx_requests,
            "transport_errors": stats.transport_errors,
        },
    }


def enrich_case_result_with_metadata(runtime_args: argparse.Namespace, result: JsonDict) -> JsonDict:
    commands: JsonDict = {
        "argv": list(getattr(runtime_args, "cli_argv", [])),
        "target_url": getattr(runtime_args, "url", ""),
        "sender_command": "internal_python_sender",
    }
    if getattr(runtime_args, "resolved_server_command", ""):
        commands["server_command"] = runtime_args.resolved_server_command

    artifacts: JsonDict = {
        "result_json": getattr(runtime_args, "output_json", ""),
        "sqlite_db": getattr(runtime_args, "sqlite_db", ""),
        "server_log": getattr(runtime_args, "server_log", ""),
    }
    if getattr(runtime_args, "requested_run_root", ""):
        artifacts["requested_run_root"] = runtime_args.requested_run_root
    if getattr(runtime_args, "actual_run_root", ""):
        artifacts["actual_run_root"] = runtime_args.actual_run_root

    # 单 case 结果和 scan/compare/search summary 都会被单独拷走复盘。
    # 所以这里不能假设“外层 summary 里反正还有一份机器信息”，
    # 必须让每个 result.json 自己就能说明这轮到底用了多少核、哪些线程参数和哪些后端开关。
    return attach_benchmark_metadata(
        payload=result,
        suite_name="suite_a",
        entry_script=__file__,
        workload={
            "trace_count": getattr(runtime_args, "trace_count", 0),
            "spans_per_trace": getattr(runtime_args, "spans_per_trace", 0),
            "inter_trace_gap_ms": getattr(runtime_args, "inter_trace_gap_ms", 0),
            "send_workers": getattr(runtime_args, "send_workers", 0),
            "request_timeout_ms": getattr(runtime_args, "request_timeout_ms", 0),
            "poll_interval_ms": getattr(runtime_args, "poll_interval_ms", 0),
            "stable_rounds": getattr(runtime_args, "stable_rounds", 0),
            "confirm_sleep_ms": getattr(runtime_args, "confirm_sleep_ms", 0),
            "max_drain_wait_ms": getattr(runtime_args, "max_drain_wait_ms", 0),
            "service_name": getattr(runtime_args, "service_name", ""),
        },
        cpu_allocation=build_cpu_allocation(
            server_cpuset=getattr(runtime_args, "server_cpuset", ""),
        ),
        thread_topology={
            "server_io_threads": getattr(runtime_args, "server_io_threads", None),
            "worker_threads": getattr(runtime_args, "worker_threads", None),
            "dispatch_worker_threads": getattr(runtime_args, "dispatch_worker_threads", None),
        },
        effective_flags={
            "disable_ai": bool(getattr(runtime_args, "disable_ai", False)),
            "disable_webhook": bool(getattr(runtime_args, "disable_webhook", False)),
            "disable_buffered_trace_repo": bool(getattr(runtime_args, "disable_buffered_trace_repo", False)),
            "trace_lifecycle_profile": getattr(runtime_args, "trace_lifecycle_profile", ""),
            "trace_sealed_grace_window_ms": getattr(runtime_args, "trace_sealed_grace_window_ms", 0),
            "trace_sweep_interval_ms": getattr(runtime_args, "trace_sweep_interval_ms", 0),
            "trace_primary_flush_span_threshold": getattr(runtime_args, "trace_primary_flush_span_threshold", 0),
            "trace_primary_flush_interval_ms": getattr(runtime_args, "trace_primary_flush_interval_ms", 0),
        },
        commands=commands,
        artifacts=artifacts,
    )


def run_suite_a_case(
    args: argparse.Namespace,
    sender_runner: Callable[[argparse.Namespace], JsonDict] = send_clean_traces,
    sqlite_counter: Callable[[Path], JsonDict] = read_sqlite_counts,
    wait_for_stable_runner: Callable[..., JsonDict] = wait_until_sqlite_stable,
) -> JsonDict:
    process_info: Optional[JsonDict] = None
    runtime_args = args
    try:
        if getattr(args, "server_bin", "") or getattr(args, "server_command", ""):
            requested_run_root, actual_run_root, artifacts = resolve_run_artifacts(
                run_root=args.run_root,
                output_json=args.output_json,
                sqlite_db=args.sqlite_db,
                server_log=args.server_log,
            )
            Path(actual_run_root).mkdir(parents=True, exist_ok=True)
            # 这里把路径真正落回运行参数，后面 sender/evaluator 就不用再区分“请求路径”和“真实路径”。
            runtime_args = argparse.Namespace(**vars(args))
            runtime_args.requested_run_root = requested_run_root
            runtime_args.actual_run_root = actual_run_root
            runtime_args.output_json = artifacts["output_json"]
            runtime_args.sqlite_db = artifacts["sqlite_db"]
            runtime_args.server_log = artifacts["server_log"]
            runtime_args.url = runtime_args.url or f"http://127.0.0.1:{args.port_base}/logs/spans"
            artifacts["port"] = args.port_base
            artifacts["run_root"] = actual_run_root
            command = resolve_server_command(args, artifacts)
            runtime_args.server_command = command
            # 这里额外落一份“最终实际执行命令”，专门服务 benchmark 复盘。
            # compare_target 一旦跑脏，单看模板字符串不够，必须知道 sqlite/port/CLI 是否真的替换进去了。
            runtime_args.resolved_server_command = command

            assert_port_available(args.port_base)
            process_info = launch_server_process(command, Path(runtime_args.server_log))
            wait_for_port_ready(args.port_base, args.startup_timeout_sec)

        sqlite_path = Path(runtime_args.sqlite_db)
        sender_result = sender_runner(runtime_args)

        stop_counts = sqlite_counter(sqlite_path)
        t_stop_ms = int(sender_result["t_stop_ms"])
        # drain 等待只认主数据表，不认 AI / webhook / analysis 之类的后置动作。
        stable_result = wait_for_stable_runner(
            sqlite_path=sqlite_path,
            sqlite_counter=sqlite_counter,
            poll_interval_ms=runtime_args.poll_interval_ms,
            stable_rounds=runtime_args.stable_rounds,
            confirm_sleep_ms=runtime_args.confirm_sleep_ms,
            max_wait_ms=runtime_args.max_drain_wait_ms,
            start_ms=int(time.monotonic() * 1000),
            # 这组实验发送的是确定总量的 clean trace，所以 drain 完成口径也必须锁成确定值：
            # 只要 trace_summary 还没追到 sender 发出的 trace_count，就不能把“计数暂时稳定”当成最终完成。
            # 真正的结束条件只有两种：达到目标 trace 数，或者超时。
            expected_trace_count=int(sender_result["trace_count"]),
        )

        result = {
            "trace_count": int(sender_result["trace_count"]),
            "spans_per_trace": int(sender_result["spans_per_trace"]),
            "inter_trace_gap_ms": int(sender_result.get("inter_trace_gap_ms", runtime_args.inter_trace_gap_ms)),
            "t_stop_ms": t_stop_ms,
            "visible_trace_count_at_stop": int(stop_counts["trace_summary"]),
            "visible_completion_rate_at_stop": (
                int(stop_counts["trace_summary"]) / int(sender_result["trace_count"])
                if int(sender_result["trace_count"]) > 0
                else 0.0
            ),
            "drain_tail_ms": int(stable_result["drain_tail_ms"]),
            "drain_timeout": bool(stable_result["drain_timeout"]),
            "sqlite_counts_at_stop": stop_counts,
            "sqlite_counts_final": stable_result["final_counts"],
            "sender_stats": sender_result["sender_stats"],
        }
        if hasattr(runtime_args, "requested_run_root"):
            result["requested_run_root"] = runtime_args.requested_run_root
            result["actual_run_root"] = runtime_args.actual_run_root
            result["sqlite_db"] = runtime_args.sqlite_db
            result["server_log"] = runtime_args.server_log
            result["url"] = runtime_args.url
            # 只在 auto-start 模式输出这个字段，避免手动模式伪造一条并不存在的命令。
            result["resolved_server_command"] = runtime_args.resolved_server_command

        result = enrich_case_result_with_metadata(runtime_args, result)
        if runtime_args.output_json:
            Path(runtime_args.output_json).write_text(
                json.dumps(result, ensure_ascii=True, indent=2) + "\n",
                encoding="utf-8",
            )
        return result
    finally:
        if process_info is not None:
            stop_server_process(process_info, args.stop_timeout_sec)


def main(argv: Optional[list[str]] = None) -> int:
    args = parse_args(argv)
    result = run_suite_a_case(args)
    print(json.dumps(result, ensure_ascii=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
