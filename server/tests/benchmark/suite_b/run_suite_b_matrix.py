#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
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
    parser.add_argument("--server-command", required=True)
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
    return {"process": process, "log_file": log_file}


def wait_for_port_open(port: int, timeout_sec: float) -> None:
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.2)
            if sock.connect_ex(("127.0.0.1", port)) == 0:
                return
        time.sleep(0.1)
    raise RuntimeError(f"port {port} did not become ready before timeout")


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


def run_suite_b_matrix(
    args: argparse.Namespace,
    launch_server: Callable[[Dict[str, object], argparse.Namespace], Dict[str, object]] = launch_server_process,
    wait_for_port: Callable[[int, float], None] = wait_for_port_open,
    run_case: Callable[[argparse.Namespace], Dict[str, object]] = run_suite_b.run_suite_b_case,
    stop_server: Callable[[Dict[str, object], float], None] = stop_server_process,
) -> Dict[str, object]:
    cases = build_case_matrix(args)
    results: List[Dict[str, object]] = []

    for case in cases:
        # 每个 case 都单独起一个后端进程，并使用自己的 SQLite。
        # 原因很直接：Suite B 的主指标看的是“最终结果有没有被脏时序污染”，
        # 如果多个 case 共用一个 DB，前一轮落下来的 trace 会直接把后一轮 evaluator 口径弄脏。
        launch_values = dict(vars(args))
        launch_values["server_command"] = format_server_command(args.server_command, case)
        launch_args = argparse.Namespace(**launch_values)
        process_info = launch_server(case, launch_args)
        try:
            wait_for_port(int(case["port"]), args.startup_timeout_sec)
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
