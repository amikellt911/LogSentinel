#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


ROOT_DIR = Path(__file__).resolve().parent
SUITE_D_DIR = ROOT_DIR / "server" / "tests" / "benchmark" / "suite_d"
if str(SUITE_D_DIR) not in sys.path:
    # 这里继续复用 Suite D 单 case runner，只把 proxy 生命周期单独拿出来控制。
    # 因为 C++ --auto-start-proxy 只能启动默认 128 worker 的 proxy，无法公平搜索 proxy 并发上限。
    sys.path.insert(0, str(SUITE_D_DIR))

import run_suite_d_case  # noqa: E402


JsonDict = dict[str, Any]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Search C++ worker_threads and Python proxy max_workers for Suite D AI-on demo."
    )
    parser.add_argument("--run-root", default="/tmp/logsentinel-suite-d-worker-ai-search")
    parser.add_argument("--summary-json", default="")
    parser.add_argument("--server-bin", default=str(ROOT_DIR / "server" / "build" / "LogSentinel"))
    parser.add_argument("--proxy-script", default=str(ROOT_DIR / "server" / "ai" / "proxy" / "main.py"))
    parser.add_argument("--python-bin", default=os.environ.get("DEV_PYTHON", "python3"))
    parser.add_argument("--wrk-bin", default="wrk")
    parser.add_argument("--wrk-script", default=str(SUITE_D_DIR / "trace_model_suite_d.lua"))
    parser.add_argument("--server-cpuset", default="1-3")
    parser.add_argument("--wrk-cpuset", default="0")
    parser.add_argument("--port-start", type=int, default=18480)
    parser.add_argument("--proxy-port-start", type=int, default=19480)
    parser.add_argument("--port-search-limit", type=int, default=240)
    parser.add_argument("--worker-points", default="12,16,24,32,48,64,96,128,192,256")
    parser.add_argument("--proxy-scale", type=float, default=1.0, help="proxy max-workers = ceil(worker_threads * proxy_scale)")
    parser.add_argument("--proxy-max-workers-cap", type=int, default=256)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--duration", default="8s")
    parser.add_argument("--warmup-duration", default="2s")
    parser.add_argument("--connections", type=int, default=30)
    parser.add_argument("--dispatch-worker-threads", type=int, default=2)
    parser.add_argument("--trace-max-dispatch-per-tick", type=int, default=128)
    parser.add_argument("--trace-sweep-interval-ms", type=int, default=200)
    parser.add_argument("--trace-primary-flush-span-threshold", type=int, default=512)
    parser.add_argument("--trace-primary-flush-interval-ms", type=int, default=5)
    parser.add_argument("--trace-active-session-limit", type=int, default=2048)
    parser.add_argument("--trace-buffered-span-limit", type=int, default=16384)
    parser.add_argument("--worker-queue-size", type=int, default=8192)
    parser.add_argument("--top-n", type=int, default=10)
    parser.add_argument("--keep-sqlite-db", action="store_true")
    parser.add_argument("--cooldown-sec", type=float, default=0.5)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--fail-fast", action="store_true")
    args = parser.parse_args()

    if args.repeats <= 0:
        parser.error("--repeats must be > 0")
    if args.proxy_scale <= 0:
        parser.error("--proxy-scale must be > 0")
    if args.proxy_max_workers_cap <= 0:
        parser.error("--proxy-max-workers-cap must be > 0")
    if args.top_n <= 0:
        parser.error("--top-n must be > 0")
    if args.cooldown_sec < 0:
        parser.error("--cooldown-sec must be >= 0")
    return args


def parse_csv_ints(value: str) -> list[int]:
    points: list[int] = []
    for item in value.split(","):
        item = item.strip()
        if not item:
            continue
        parsed = int(item)
        if parsed <= 0:
            raise ValueError("worker points must be positive")
        points.append(parsed)
    if not points:
        raise ValueError("worker points must not be empty")
    return points


def format_run_timestamp() -> str:
    now = time.time()
    seconds = int(now)
    millis = int(round((now - seconds) * 1000))
    if millis >= 1000:
        seconds += 1
        millis = 0
    return f"{time.strftime('%Y%m%d-%H%M%S', time.localtime(seconds))}-{millis:03d}ms"


def is_port_free(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.2)
        return sock.connect_ex(("127.0.0.1", port)) != 0


def find_free_port(start_port: int, search_limit: int, used_ports: set[int]) -> int:
    # server 端口和 proxy 端口都由本脚本分配。
    # 这样多轮搜索不会撞上旧进程，也不会误用正在跑的 8001 默认 proxy。
    for port in range(start_port, start_port + search_limit):
        if port in used_ports:
            continue
        if is_port_free(port):
            used_ports.add(port)
            return port
    raise RuntimeError(f"no free port found from {start_port} to {start_port + search_limit - 1}")


def wait_for_port_ready(port: int, proc: subprocess.Popen[str], log_path: Path, timeout_sec: float = 10.0) -> None:
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        if is_port_free(port) is False:
            return
        if proc.poll() is not None:
            tail = log_path.read_text(encoding="utf-8", errors="replace")[-4000:] if log_path.exists() else ""
            raise RuntimeError(f"proxy exited before ready, code={proc.returncode}, log_tail={tail}")
        time.sleep(0.1)
    tail = log_path.read_text(encoding="utf-8", errors="replace")[-4000:] if log_path.exists() else ""
    raise RuntimeError(f"proxy did not listen on port {port}, log_tail={tail}")


def terminate_process(proc: subprocess.Popen[str], name: str) -> None:
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        # proxy 搜索脚本不能留下残留进程，否则下一轮端口探测会被污染。
        # 先 TERM 后 KILL，避免正常退出路径来不及刷日志。
        proc.kill()
        proc.wait(timeout=5)


def start_proxy(args: argparse.Namespace, case_dir: Path, proxy_port: int, proxy_max_workers: int) -> tuple[subprocess.Popen[str], Path]:
    proxy_log = case_dir / "proxy.log"
    proxy_log.parent.mkdir(parents=True, exist_ok=True)
    log_file = proxy_log.open("w", encoding="utf-8")
    command = [
        args.python_bin,
        args.proxy_script,
        "--host", "127.0.0.1",
        "--port", str(proxy_port),
        "--max-workers", str(proxy_max_workers),
    ]
    # 每个 worker 点单独拉起 proxy，且 max-workers 显式写进命令。
    # 这样测到的瓶颈才是 C++ worker/proxy 并发共同作用，不会被默认 128 limiter 偷偷截断。
    proc = subprocess.Popen(command, cwd=str(ROOT_DIR), stdout=log_file, stderr=subprocess.STDOUT, text=True)
    log_file.close()
    wait_for_port_ready(proxy_port, proc, proxy_log)
    return proc, proxy_log


def proxy_workers_for(worker_threads: int, args: argparse.Namespace) -> int:
    scaled = int(worker_threads * args.proxy_scale)
    if worker_threads * args.proxy_scale > scaled:
        scaled += 1
    return max(1, min(args.proxy_max_workers_cap, scaled))


def build_case_args(
    args: argparse.Namespace,
    actual_root: Path,
    case_index: int,
    case_name: str,
    server_port: int,
    proxy_port: int,
    worker_threads: int,
) -> argparse.Namespace:
    case_dir = actual_root / f"{case_index:03d}_{case_name}"
    output_json = case_dir / "result.json"
    argv = [
        "--server-bin", args.server_bin,
        "--run-root", str(case_dir),
        "--output-json", str(output_json),
        "--port-base", str(server_port),
        "--server-cpuset", args.server_cpuset,
        "--wrk-cpuset", args.wrk_cpuset,
        "--wrk-bin", args.wrk_bin,
        "--wrk-script", args.wrk_script,
        "--wrk-threads", "1",
        "--connections", str(args.connections),
        "--duration", args.duration,
        "--warmup-duration", args.warmup_duration,
        "--spans-per-trace", "8",
        "--server-io-threads", "1",
        "--worker-threads", str(worker_threads),
        "--dispatch-worker-threads", str(args.dispatch_worker_threads),
        "--worker-queue-size", str(args.worker_queue_size),
        "--trace-active-session-limit", str(args.trace_active_session_limit),
        "--trace-buffered-span-limit", str(args.trace_buffered_span_limit),
        "--trace-max-dispatch-per-tick", str(args.trace_max_dispatch_per_tick),
        "--trace-lifecycle-profile", "protected",
        "--trace-sealed-grace-window-ms", "100",
        "--trace-sweep-interval-ms", str(args.trace_sweep_interval_ms),
        "--trace-primary-flush-span-threshold", str(args.trace_primary_flush_span_threshold),
        "--trace-primary-flush-interval-ms", str(args.trace_primary_flush_interval_ms),
        "--trace-ai-provider", "mock",
        "--no-auto-start-proxy",
        "--trace-ai-base-url", f"http://127.0.0.1:{proxy_port}",
        "--disable-webhook",
    ]
    return run_suite_d_case.parse_args(argv)


def final_completion_ratio(result: JsonDict) -> float:
    offered = int(result.get("wrk_metrics", {}).get("offered_traces") or 0)
    final_count = int(result.get("sqlite_counts_final", {}).get("trace_summary") or 0)
    return (final_count / offered) if offered > 0 else 0.0


def final_ai_completion_ratio(result: JsonDict) -> float:
    final_trace_count = int(result.get("sqlite_counts_final", {}).get("trace_summary") or 0)
    final_analysis_count = int(result.get("final_trace_analysis_count") or 0)
    return (final_analysis_count / final_trace_count) if final_trace_count > 0 else 0.0


def compact_row(case_id: str,
                worker_threads: int,
                proxy_max_workers: int,
                result: JsonDict,
                status: str,
                error: str = "") -> JsonDict:
    wrk = result.get("wrk_metrics", {}) if isinstance(result, dict) else {}
    row: JsonDict = {
        "case_id": case_id,
        "status": status,
        "error": error,
        "worker_threads": worker_threads,
        "proxy_max_workers": proxy_max_workers,
    }
    if status == "ok":
        row.update(
            {
                "result_json": result.get("output_json"),
                "sqlite_db": result.get("sqlite_db"),
                "server_log": result.get("server_log"),
                "requests_per_sec": float(wrk.get("requests_per_sec") or 0.0),
                "offered_traces": int(wrk.get("offered_traces") or 0),
                "latency_p95_ms": float(wrk.get("latency_p95_ms") or 0.0),
                "latency_p99_ms": float(wrk.get("latency_p99_ms") or 0.0),
                "online_completed_traces_per_sec": float(result.get("online_completed_traces_per_sec") or 0.0),
                "online_completion_ratio": float(result.get("online_completion_ratio") or 0.0),
                "final_completion_ratio": final_completion_ratio(result),
                "final_trace_analysis_count": int(result.get("final_trace_analysis_count") or 0),
                "online_ai_completed_traces_per_sec": float(result.get("online_ai_completed_traces_per_sec") or 0.0),
                "final_ai_completion_ratio": float(
                    result.get("final_ai_completion_ratio")
                    if result.get("final_ai_completion_ratio") is not None
                    else final_ai_completion_ratio(result)
                ),
                "final_ai_offered_ratio": float(result.get("final_ai_offered_ratio") or 0.0),
                "drain_tail_ms": int(result.get("drain_tail_ms") or 0),
                "final_trace_summary": int(result.get("sqlite_counts_final", {}).get("trace_summary") or 0),
            }
        )
    return row


def cleanup_case_sqlite_db(row: JsonDict) -> None:
    sqlite_path = Path(str(row.get("sqlite_db") or ""))
    if not str(sqlite_path) or not sqlite_path.exists():
        return
    # worker 搜索会跑很多点，SQLite DB 对最终排序没有继续价值。
    # 默认删掉 DB，只保留 result/server/proxy 日志，避免把 /tmp 写满。
    sqlite_path.unlink()
    row["sqlite_db_removed"] = True


def mean(values: list[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def stddev(values: list[float]) -> float:
    if len(values) <= 1:
        return 0.0
    avg = mean(values)
    return (sum((value - avg) ** 2 for value in values) / len(values)) ** 0.5


def aggregate_rows(rows: list[JsonDict]) -> list[JsonDict]:
    grouped: dict[int, list[JsonDict]] = {}
    for row in rows:
        if row.get("status") != "ok":
            continue
        grouped.setdefault(int(row["worker_threads"]), []).append(row)

    aggregates: list[JsonDict] = []
    for worker_threads, group in grouped.items():
        online = [float(row.get("online_completed_traces_per_sec") or 0.0) for row in group]
        ai_online = [float(row.get("online_ai_completed_traces_per_sec") or 0.0) for row in group]
        ai_ratio = [float(row.get("final_ai_completion_ratio") or 0.0) for row in group]
        final_ratio = [float(row.get("final_completion_ratio") or 0.0) for row in group]
        p95 = [float(row.get("latency_p95_ms") or 0.0) for row in group]
        qps = [float(row.get("requests_per_sec") or 0.0) for row in group]
        aggregates.append(
            {
                "worker_threads": worker_threads,
                "proxy_max_workers": int(group[0]["proxy_max_workers"]),
                "runs": len(group),
                # AI-on 搜索的第一目标是看 trace_analysis 能不能追上。
                # trace_summary 只说明主链可见，不能替代 AI 分析完成量，否则 worker/proxy 搜索会被主链吞吐误导。
                "online_ai_completed_traces_per_sec_avg": mean(ai_online),
                "online_ai_completed_traces_per_sec_stddev": stddev(ai_online),
                "final_ai_completion_ratio_avg": mean(ai_ratio),
                "online_completed_traces_per_sec_avg": mean(online),
                "online_completed_traces_per_sec_stddev": stddev(online),
                "final_completion_ratio_avg": mean(final_ratio),
                "latency_p95_ms_avg": mean(p95),
                "requests_per_sec_avg": mean(qps),
                "case_ids": [str(row["case_id"]) for row in group],
            }
        )
    return sorted(
        aggregates,
        key=lambda row: (
            -float(row["online_ai_completed_traces_per_sec_avg"]),
            -float(row["final_ai_completion_ratio_avg"]),
            -float(row["online_completed_traces_per_sec_avg"]),
            float(row["latency_p95_ms_avg"]),
            float(row["online_completed_traces_per_sec_stddev"]),
            int(row["worker_threads"]),
        ),
    )


def print_row(index: int, total: int, row: JsonDict) -> None:
    if row["status"] != "ok":
        print(f"[worker-ai {index}/{total}] {row['case_id']} FAILED error={row['error']}")
        return
    print(
        f"[worker-ai {index}/{total}] {row['case_id']} "
        f"worker={row['worker_threads']} proxy={row['proxy_max_workers']} "
        f"online={row['online_completed_traces_per_sec']:.2f} "
        f"ai_online={row['online_ai_completed_traces_per_sec']:.2f} "
        f"final={row['final_completion_ratio']:.4f} "
        f"ai_final={row['final_ai_completion_ratio']:.4f} "
        f"qps={row['requests_per_sec']:.2f} "
        f"p95={row['latency_p95_ms']:.2f}ms"
    )


def run_search(args: argparse.Namespace) -> JsonDict:
    worker_points = parse_csv_ints(args.worker_points)
    actual_root = Path(f"{args.run_root}-{format_run_timestamp()}")
    summary_json = Path(args.summary_json) if args.summary_json else actual_root / "summary.json"
    actual_root.mkdir(parents=True, exist_ok=True)

    scheduled = [(worker, repeat) for worker in worker_points for repeat in range(1, args.repeats + 1)]
    if args.dry_run:
        preview = [
            {"worker_threads": worker, "proxy_max_workers": proxy_workers_for(worker, args)}
            for worker in worker_points
        ]
        # dry-run 也把核心参数写完整，避免正式跑之前还要读源码确认搜索空间。
        summary = {
            "dry_run": True,
            "requested_run_root": args.run_root,
            "actual_run_root": str(actual_root),
            "summary_json": str(summary_json),
            "worker_points": preview,
            "scheduled_case_count": len(scheduled),
            "repeats": args.repeats,
            "proxy_scale": args.proxy_scale,
            "proxy_max_workers_cap": args.proxy_max_workers_cap,
            "duration": args.duration,
            "warmup_duration": args.warmup_duration,
            "connections": args.connections,
            "sqlite_db_policy": "keep" if args.keep_sqlite_db else "delete_after_case",
        }
        summary_json.parent.mkdir(parents=True, exist_ok=True)
        summary_json.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"[suite-d-worker-ai] dry_run cases={len(scheduled)} summary={summary_json}")
        return summary

    used_server_ports: set[int] = set()
    used_proxy_ports: set[int] = set()
    rows: list[JsonDict] = []
    for index, (worker_threads, repeat) in enumerate(scheduled, start=1):
        proxy_max_workers = proxy_workers_for(worker_threads, args)
        case_name = f"w{worker_threads}_p{proxy_max_workers}_r{repeat:02d}"
        case_dir = actual_root / f"{index:03d}_{case_name}"
        server_port = find_free_port(args.port_start, args.port_search_limit, used_server_ports)
        proxy_port = find_free_port(args.proxy_port_start, args.port_search_limit, used_proxy_ports)
        proxy_proc: subprocess.Popen[str] | None = None
        try:
            proxy_proc, proxy_log = start_proxy(args, case_dir, proxy_port, proxy_max_workers)
            case_args = build_case_args(args, actual_root, index, case_name, server_port, proxy_port, worker_threads)
            result = run_suite_d_case.run_suite_d_case(case_args)
            row = compact_row(case_name, worker_threads, proxy_max_workers, result, "ok")
            row["proxy_log"] = str(proxy_log)
            if not args.keep_sqlite_db:
                cleanup_case_sqlite_db(row)
        except Exception as exc:  # noqa: BLE001
            row = compact_row(case_name, worker_threads, proxy_max_workers, {}, "failed", str(exc))
            # 失败时也保留 proxy/server 端口和日志路径，方便直接定位是 proxy 没起来、后端没连上，还是 wrk 压测失败。
            row["server_port"] = server_port
            row["proxy_port"] = proxy_port
            row["proxy_log"] = str(case_dir / "proxy.log")
            if args.fail_fast:
                raise
        finally:
            if proxy_proc is not None:
                terminate_process(proxy_proc, "ai-proxy")

        rows.append(row)
        print_row(index, len(scheduled), row)
        if args.cooldown_sec > 0:
            time.sleep(args.cooldown_sec)

    aggregate_top = aggregate_rows(rows)
    summary = {
        "dry_run": False,
        "requested_run_root": args.run_root,
        "actual_run_root": str(actual_root),
        "summary_json": str(summary_json),
        "worker_points": worker_points,
        "repeats": args.repeats,
        "proxy_scale": args.proxy_scale,
        "proxy_max_workers_cap": args.proxy_max_workers_cap,
        "fixed_case_params": {
            "duration": args.duration,
            "warmup_duration": args.warmup_duration,
            "connections": args.connections,
            "dispatch_worker_threads": args.dispatch_worker_threads,
            "trace_max_dispatch_per_tick": args.trace_max_dispatch_per_tick,
            "trace_sweep_interval_ms": args.trace_sweep_interval_ms,
            "trace_primary_flush_span_threshold": args.trace_primary_flush_span_threshold,
            "trace_primary_flush_interval_ms": args.trace_primary_flush_interval_ms,
            "trace_active_session_limit": args.trace_active_session_limit,
            "trace_buffered_span_limit": args.trace_buffered_span_limit,
            "worker_queue_size": args.worker_queue_size,
        },
        "sqlite_db_policy": "keep" if args.keep_sqlite_db else "delete_after_case",
        "rows": rows,
        "aggregate_top": aggregate_top[: args.top_n],
    }
    summary_json.parent.mkdir(parents=True, exist_ok=True)
    summary_json.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print()
    print(f"[suite-d-worker-ai] summary={summary_json}")
    print("[suite-d-worker-ai] aggregate top")
    for rank, item in enumerate(aggregate_top[: args.top_n], start=1):
        print(
            f"#{rank} worker={item['worker_threads']} proxy={item['proxy_max_workers']} "
            f"runs={item['runs']} "
            f"ai_online_avg={item['online_ai_completed_traces_per_sec_avg']:.2f} "
            f"ai_final_avg={item['final_ai_completion_ratio_avg']:.4f} "
            f"online_avg={item['online_completed_traces_per_sec_avg']:.2f} "
            f"online_std={item['online_completed_traces_per_sec_stddev']:.2f} "
            f"final_avg={item['final_completion_ratio_avg']:.4f} "
            f"p95_avg={item['latency_p95_ms_avg']:.2f}ms "
            f"qps_avg={item['requests_per_sec_avg']:.2f}"
        )
    return summary


def main() -> int:
    run_search(parse_args())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
