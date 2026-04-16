#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import shlex
import signal
import socket
import subprocess
import time
from pathlib import Path
from typing import Callable, Dict, List, Optional

import run_suite_b


def default_run_root() -> str:
    timestamp = time.strftime("%Y%m%d-%H%M%S")
    return f"server/tests/benchmark/results/suite_b/{timestamp}-matrix"


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Suite B 3x2 矩阵 runner")
    parser.add_argument("--server-command", default="")
    parser.add_argument("--server-bin", default="./server/build/LogSentinel")
    parser.add_argument("--server-cpuset", default="")
    parser.add_argument("--server-io-threads", type=int, default=1)
    parser.add_argument("--no-auto-start-proxy", action="store_true")
    # 这一组资源参数故意直接暴露在矩阵 runner 顶层。
    # 否则 4 核本机和 16 核云机每换一次资源配额，都得手写一整段 shell 模板，
    # 最后实验记录里只剩一坨字符串，根本没法稳定复现。
    parser.add_argument("--worker-threads", type=int, default=3)
    parser.add_argument("--dispatch-worker-threads", type=int, default=1)
    parser.add_argument("--worker-queue-size", type=int, default=2048)
    parser.add_argument("--trace-capacity", type=int, default=12)
    parser.add_argument("--trace-token-limit", type=int, default=0)
    # 这里把 Suite B 的默认 sweep tick 下调到 100ms。
    # 目的不是改后端产品语义，而是让 benchmark 场景里的时间轮量化误差更小，
    # 避免像 795ms/811ms 这种边界样本因为 tick 太粗而提前掉出“理论可吸收”窗口。
    parser.add_argument("--trace-sweep-interval-ms", type=int, default=100)
    parser.add_argument("--trace-idle-timeout-ms", type=int, default=800)
    parser.add_argument("--trace-max-dispatch-per-tick", type=int, default=64)
    parser.add_argument("--trace-buffered-span-limit", type=int, default=4096)
    parser.add_argument("--trace-active-session-limit", type=int, default=512)
    parser.add_argument("--disable-ai", action="store_true")
    parser.add_argument("--disable-webhook", action="store_true")
    parser.add_argument("--disable-buffered-trace-repo", action="store_true")
    parser.add_argument("--run-root", default=default_run_root())
    parser.add_argument("--output-summary", default="")
    parser.add_argument("--sender-profiles", default="clean_baseline,mixed_realistic,late_replay_stress")
    parser.add_argument("--trace-lifecycle-profiles", default="protected,minimal")
    parser.add_argument("--port-base", type=int, default=19080)
    parser.add_argument("--startup-timeout-sec", type=float, default=10.0)
    parser.add_argument("--stop-timeout-sec", type=float, default=5.0)
    parser.add_argument("--url-template", default="http://127.0.0.1:{port}/logs/spans")
    parser.add_argument("--seed", type=int, default=20260415)
    parser.add_argument("--trace-count", type=int, default=10)
    parser.add_argument("--spans-per-trace", type=int, default=8)
    parser.add_argument("--base-gap-ms", type=int, default=20)
    parser.add_argument("--trace-gap-ms", type=int, default=80)
    parser.add_argument("--tick-ms", type=int, default=500)
    parser.add_argument("--grace-ms", type=int, default=1000)
    parser.add_argument("--tombstone-window-ms", type=int, default=12500)
    parser.add_argument("--service-name", default="svc-suite-b")
    parser.add_argument("--timeout-sec", type=float, default=1.0)
    parser.add_argument("--send-workers", type=int, default=1)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--poll-interval-sec", type=float, default=0.2)
    parser.add_argument("--stable-rounds", type=int, default=5)
    parser.add_argument("--confirm-sleep-sec", type=float, default=0.3)
    parser.add_argument("--max-wait-sec", type=float, default=30.0)
    return parser.parse_args(argv)


def parse_csv_list(value: str) -> List[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def build_case_matrix(args: argparse.Namespace) -> List[Dict[str, object]]:
    run_root = Path(args.run_root)
    sender_profiles = parse_csv_list(args.sender_profiles)
    lifecycle_profiles = parse_csv_list(args.trace_lifecycle_profiles)
    cases: List[Dict[str, object]] = []

    port = args.port_base
    for lifecycle_profile in lifecycle_profiles:
        for sender_profile in sender_profiles:
            case_id = f"{lifecycle_profile}__{sender_profile}"
            case_dir = run_root / lifecycle_profile / sender_profile
            case_dir.mkdir(parents=True, exist_ok=True)

            cases.append(
                {
                    "case_id": case_id,
                    "trace_lifecycle_profile": lifecycle_profile,
                    "sender_profile": sender_profile,
                    "run_dir": str(case_dir),
                    "manifest": str(case_dir / "manifest.jsonl"),
                    "sqlite_db": str(case_dir / "suite_b.db"),
                    "output_json": str(case_dir / "result.json"),
                    "server_log": str(case_dir / "server.log"),
                    "port": port,
                }
            )
            port += 1

    return cases


def format_server_command(template: str, case: Dict[str, object]) -> str:
    return template.format(
        sqlite_db=case["sqlite_db"],
        trace_lifecycle_profile=case["trace_lifecycle_profile"],
        port=case["port"],
        log_path=case["server_log"],
        case_id=case["case_id"],
        run_dir=case["run_dir"],
    )


def shell_join(parts: List[str]) -> str:
    return " ".join(shlex.quote(part) for part in parts)


def build_default_server_command(args: argparse.Namespace, case: Dict[str, object]) -> str:
    parts: List[str] = []
    # 这里先只绑后端进程，不碰 sender 主进程。
    # 原因不是 sender 不重要，而是 sender 当前就在 matrix runner 这个 Python 进程里执行；
    # 如果要分别给 sender 绑核，就得把 sender 再拆成独立子进程，这一刀先不扩工程量。
    if args.server_cpuset:
        parts.extend(["taskset", "-c", args.server_cpuset])

    server_bin_tokens = shlex.split(args.server_bin)
    if not server_bin_tokens:
        raise ValueError("--server-bin must not be empty")
    parts.extend(server_bin_tokens)

    # 默认命令显式把 benchmark 关心的后端参数全部带上。
    # 这样实验语义不会偷偷依赖 main.cpp 的默认值，也不会因为别处改了默认配置导致历史命令失真。
    parts.extend(
        [
            "--db",
            str(case["sqlite_db"]),
            "--port",
            str(case["port"]),
            "--trace-lifecycle-profile",
            str(case["trace_lifecycle_profile"]),
            "--server-io-threads",
            str(args.server_io_threads),
            "--worker-threads",
            str(args.worker_threads),
            "--dispatch-worker-threads",
            str(args.dispatch_worker_threads),
            "--worker-queue-size",
            str(args.worker_queue_size),
            "--trace-capacity",
            str(args.trace_capacity),
            "--trace-token-limit",
            str(args.trace_token_limit),
            "--trace-sweep-interval-ms",
            str(args.trace_sweep_interval_ms),
            "--trace-idle-timeout-ms",
            str(args.trace_idle_timeout_ms),
            "--trace-max-dispatch-per-tick",
            str(args.trace_max_dispatch_per_tick),
            "--trace-buffered-span-limit",
            str(args.trace_buffered_span_limit),
            "--trace-active-session-limit",
            str(args.trace_active_session_limit),
        ]
    )

    if args.no_auto_start_proxy:
        parts.append("--no-auto-start-proxy")
    if args.disable_ai:
        parts.append("--disable-ai")
    if args.disable_webhook:
        parts.append("--disable-webhook")
    if args.disable_buffered_trace_repo:
        parts.append("--disable-buffered-trace-repo")
    return shell_join(parts)


def resolve_server_command(args: argparse.Namespace, case: Dict[str, object]) -> str:
    # 兼容旧的模板入口，避免已经写好的外部脚本全部失效；
    # 但如果用户没传模板，就走新的默认命令构造，直接吃 CLI 资源参数。
    if args.server_command:
        return format_server_command(args.server_command, case)
    return build_default_server_command(args, case)


def launch_server_process(case: Dict[str, object], args: argparse.Namespace) -> Dict[str, object]:
    log_path = Path(case["server_log"])
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_file = log_path.open("w", encoding="utf-8")
    process = subprocess.Popen(
        args.server_command,
        shell=True,
        stdout=log_file,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    return {"process": process, "log_file": log_file, "log_path": str(log_path)}


def assert_port_available(
    port: int,
    host: str = "127.0.0.1",
    probe_connect: Optional[Callable[[str, int], int]] = None,
) -> None:
    if probe_connect is None:
        def default_probe(target_host: str, target_port: int) -> int:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
                sock.settimeout(0.2)
                return sock.connect_ex((target_host, target_port))
        probe_connect = default_probe
    if probe_connect(host, port) == 0:
        raise RuntimeError(
            f"port {port} is already occupied before launching suite_b case; "
            "this would let an old process fake the readiness check"
        )


def _tail_log_excerpt(log_path: str, line_limit: int = 20) -> str:
    try:
        lines = Path(log_path).read_text(encoding="utf-8").splitlines()
    except OSError:
        return ""
    if not lines:
        return ""
    excerpt = "\n".join(lines[-line_limit:])
    return f"\nRecent server log:\n{excerpt}"


def assert_process_alive(process_info: Dict[str, object]) -> None:
    process = process_info["process"]
    return_code = process.poll()
    if return_code is None:
        return
    log_excerpt = _tail_log_excerpt(str(process_info.get("log_path", "")))
    raise RuntimeError(f"server process exited before ready check finished with code {return_code}{log_excerpt}")


def wait_for_port_open(port: int, timeout_sec: float) -> None:
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.2)
            if sock.connect_ex(("127.0.0.1", port)) == 0:
                return
        time.sleep(0.1)
    raise RuntimeError(f"port {port} did not become ready before timeout")


def wait_for_server_ready(
    process_info: Dict[str, object],
    port: int,
    timeout_sec: float,
    wait_for_port: Callable[[int, float], None] = wait_for_port_open,
    check_process_alive: Callable[[Dict[str, object]], None] = assert_process_alive,
) -> None:
    # 这里只看“端口有人监听”是不够的。
    # 如果旧进程本来就占着这个端口，connect_ex 也会返回成功，
    # 但这次新起的 case 进程可能其实已经因为 bind 失败退出了。
    check_process_alive(process_info)
    wait_for_port(port, timeout_sec)
    check_process_alive(process_info)


def stop_server_process(process_info: Dict[str, object], timeout_sec: float) -> None:
    process = process_info["process"]
    log_file = process_info["log_file"]
    try:
        if process.poll() is None:
            # 这里不能只 terminate shell 进程本身。
            # 因为 server-command 允许用 shell 模板，真正的 LogSentinel 往往是 shell 的子进程；
            # 如果不按进程组一起停，矩阵跑完后很容易留下孤儿后端，占住端口污染下一组实验。
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
            try:
                process.wait(timeout=timeout_sec)
            except subprocess.TimeoutExpired:
                os.killpg(os.getpgid(process.pid), signal.SIGKILL)
                process.wait(timeout=timeout_sec)
    finally:
        log_file.close()


def build_case_args(args: argparse.Namespace, case: Dict[str, object]) -> argparse.Namespace:
    return argparse.Namespace(
        url=args.url_template.format(port=case["port"]),
        profile=case["sender_profile"],
        trace_lifecycle_profile=case["trace_lifecycle_profile"],
        seed=args.seed,
        trace_count=args.trace_count,
        spans_per_trace=args.spans_per_trace,
        base_gap_ms=args.base_gap_ms,
        trace_gap_ms=args.trace_gap_ms,
        tick_ms=args.tick_ms,
        grace_ms=args.grace_ms,
        tombstone_window_ms=args.tombstone_window_ms,
        service_name=args.service_name,
        manifest=case["manifest"],
        sqlite_db=case["sqlite_db"],
        output_json=case["output_json"],
        timeout_sec=args.timeout_sec,
        send_workers=args.send_workers,
        dry_run=args.dry_run,
        poll_interval_sec=args.poll_interval_sec,
        stable_rounds=args.stable_rounds,
        confirm_sleep_sec=args.confirm_sleep_sec,
        max_wait_sec=args.max_wait_sec,
    )


def extract_case_p95_ms(case: Dict[str, object]) -> Optional[float]:
    latency_stats = case.get("ingest_latency_ms")
    if not isinstance(latency_stats, dict):
        return None
    p95 = latency_stats.get("p95")
    if not isinstance(p95, (int, float)):
        return None
    return float(p95)


def build_ingest_p95_latency_delta_by_profile(cases: List[Dict[str, object]]) -> Dict[str, Dict[str, object]]:
    grouped: Dict[str, Dict[str, float]] = {}
    for case in cases:
        profile = str(case.get("profile", ""))
        lifecycle = str(case.get("trace_lifecycle_profile", ""))
        p95 = extract_case_p95_ms(case)
        if not profile or lifecycle not in {"protected", "minimal"} or p95 is None:
            continue
        grouped.setdefault(profile, {})[lifecycle] = p95

    deltas: Dict[str, Dict[str, object]] = {}
    for profile, values in grouped.items():
        if "protected" not in values or "minimal" not in values:
            continue
        protected_p95 = values["protected"]
        minimal_p95 = values["minimal"]
        absolute_ms = protected_p95 - minimal_p95
        # 这里的相对增量只在 minimal p95 非 0 时计算。
        # dry-run 或极小 fake case 可能出现 0ms，强行除会把护栏指标变成无意义的无穷大。
        relative = None if minimal_p95 == 0 else absolute_ms / minimal_p95
        deltas[profile] = {
            "protected_p95_ms": protected_p95,
            "minimal_p95_ms": minimal_p95,
            "absolute_ms": absolute_ms,
            "relative": relative,
        }
    return deltas


def run_suite_b_matrix(
    args: argparse.Namespace,
    assert_port_available: Callable[[int], None] = assert_port_available,
    launch_server: Callable[[Dict[str, object], argparse.Namespace], Dict[str, object]] = launch_server_process,
    wait_for_port: Callable[[int, float], None] = wait_for_port_open,
    check_process_alive: Callable[[Dict[str, object]], None] = assert_process_alive,
    run_case: Callable[[argparse.Namespace], Dict[str, object]] = run_suite_b.run_suite_b_case,
    stop_server: Callable[[Dict[str, object], float], None] = stop_server_process,
) -> Dict[str, object]:
    cases = build_case_matrix(args)
    results: List[Dict[str, object]] = []

    for case in cases:
        # 每个 case 都单独起一个后端进程，并使用自己的 SQLite。
        # 原因很直接：Suite B 的主指标看的是“最终结果有没有被脏时序污染”，
        # 如果多个 case 共用一个 DB，前一轮落下来的 trace 会直接把后一轮 evaluator 口径弄脏。
        assert_port_available(int(case["port"]))
        launch_values = dict(vars(args))
        launch_values["server_command"] = resolve_server_command(args, case)
        launch_args = argparse.Namespace(**launch_values)
        process_info = launch_server(case, launch_args)
        try:
            wait_for_server_ready(
                process_info,
                int(case["port"]),
                args.startup_timeout_sec,
                wait_for_port=wait_for_port,
                check_process_alive=check_process_alive,
            )
            case_result = run_case(build_case_args(args, case))
            case_result["case_id"] = case["case_id"]
            case_result["server_log"] = case["server_log"]
            results.append(case_result)
        finally:
            stop_server(process_info, args.stop_timeout_sec)

    summary = {
        "total_cases": len(results),
        "sender_profiles": parse_csv_list(args.sender_profiles),
        "trace_lifecycle_profiles": parse_csv_list(args.trace_lifecycle_profiles),
        "ingest_p95_latency_delta_by_profile": build_ingest_p95_latency_delta_by_profile(results),
        "cases": results,
    }

    output_summary = args.output_summary or str(Path(args.run_root) / "summary.json")
    output_path = Path(output_summary)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(summary, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
    return summary


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    summary = run_suite_b_matrix(args)
    print(json.dumps(summary, ensure_ascii=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
