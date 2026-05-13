#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import socket
import sys
import time
from itertools import product
from pathlib import Path
from typing import Any


ROOT_DIR = Path(__file__).resolve().parent
SUITE_D_DIR = ROOT_DIR / "server" / "tests" / "benchmark" / "suite_d"
if str(SUITE_D_DIR) not in sys.path:
    # 脚本放在项目根目录，但复用 Suite D 单 case runner。
    # 这样搜索脚本只负责参数组合和排序，不重新实现后端起停、wrk 解析和 SQLite drain 口径。
    sys.path.insert(0, str(SUITE_D_DIR))

import run_suite_d_case  # noqa: E402


JsonDict = dict[str, Any]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Search local 4-core Suite D demo parameters under mock AI mode."
    )
    parser.add_argument("--run-root", default="/tmp/logsentinel-suite-d-param-search")
    parser.add_argument("--summary-json", default="")
    parser.add_argument("--server-bin", default=str(ROOT_DIR / "server" / "build" / "LogSentinel"))
    parser.add_argument("--wrk-bin", default="wrk")
    parser.add_argument("--wrk-script", default=str(SUITE_D_DIR / "trace_model_suite_d.lua"))
    parser.add_argument("--server-cpuset", default="1-3")
    parser.add_argument("--wrk-cpuset", default="0")
    parser.add_argument("--port-start", type=int, default=18280)
    parser.add_argument("--port-search-limit", type=int, default=160)
    parser.add_argument("--preset", choices=("quick", "balanced", "wide", "finalists"), default="quick")
    parser.add_argument("--max-cases", type=int, default=0, help="0 means run all candidates in the preset.")
    parser.add_argument("--top-n", type=int, default=8)
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--duration", default="8s")
    parser.add_argument("--warmup-duration", default="2s")
    parser.add_argument("--spans-per-trace", type=int, default=8)
    parser.add_argument("--trace-ai-provider", default="mock")
    parser.add_argument("--keep-sqlite-db", action="store_true", help="Keep per-case SQLite DB files.")
    parser.add_argument("--cooldown-sec", type=float, default=0.0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--fail-fast", action="store_true")
    args = parser.parse_args()

    if args.max_cases < 0:
        parser.error("--max-cases must be >= 0")
    if args.top_n <= 0:
        parser.error("--top-n must be > 0")
    if args.repeats <= 0:
        parser.error("--repeats must be > 0")
    if args.cooldown_sec < 0:
        parser.error("--cooldown-sec must be >= 0")
    return args


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
    # 每个 case 都会单独起停一个 LogSentinel。
    # 这里主动跳过旧进程占用端口和本轮已用端口，避免搜索跑到一半因为端口冲突中断。
    for port in range(start_port, start_port + search_limit):
        if port in used_ports:
            continue
        if is_port_free(port):
            used_ports.add(port)
            return port
    raise RuntimeError(f"no free port found from {start_port} to {start_port + search_limit - 1}")


def build_quick_candidates() -> list[JsonDict]:
    # quick 档围绕当前 demo 默认参数做小范围扰动，适合验收前快速找更稳的现场参数。
    # 这里不追求暴力全局最优，目标是找出明显更好的 flush / sweep / dispatch / connections 组合。
    return [
        {"name": "baseline", "connections": 30, "server_io_threads": 1, "worker_threads": 8, "dispatch_worker_threads": 1, "trace_max_dispatch_per_tick": 64, "trace_sweep_interval_ms": 200, "trace_primary_flush_span_threshold": 512, "trace_primary_flush_interval_ms": 5, "trace_active_session_limit": 512, "trace_buffered_span_limit": 4096},
        {"name": "sweep100", "connections": 30, "server_io_threads": 1, "worker_threads": 8, "dispatch_worker_threads": 1, "trace_max_dispatch_per_tick": 64, "trace_sweep_interval_ms": 100, "trace_primary_flush_span_threshold": 512, "trace_primary_flush_interval_ms": 5, "trace_active_session_limit": 512, "trace_buffered_span_limit": 4096},
        {"name": "dispatch2", "connections": 30, "server_io_threads": 1, "worker_threads": 8, "dispatch_worker_threads": 2, "trace_max_dispatch_per_tick": 128, "trace_sweep_interval_ms": 100, "trace_primary_flush_span_threshold": 512, "trace_primary_flush_interval_ms": 5, "trace_active_session_limit": 512, "trace_buffered_span_limit": 4096},
        {"name": "worker12_dispatch2", "connections": 30, "server_io_threads": 1, "worker_threads": 12, "dispatch_worker_threads": 2, "trace_max_dispatch_per_tick": 128, "trace_sweep_interval_ms": 100, "trace_primary_flush_span_threshold": 512, "trace_primary_flush_interval_ms": 5, "trace_active_session_limit": 512, "trace_buffered_span_limit": 4096},
        {"name": "flush1024", "connections": 30, "server_io_threads": 1, "worker_threads": 8, "dispatch_worker_threads": 1, "trace_max_dispatch_per_tick": 64, "trace_sweep_interval_ms": 200, "trace_primary_flush_span_threshold": 1024, "trace_primary_flush_interval_ms": 5, "trace_active_session_limit": 512, "trace_buffered_span_limit": 4096},
        {"name": "flush1024_interval10", "connections": 30, "server_io_threads": 1, "worker_threads": 8, "dispatch_worker_threads": 1, "trace_max_dispatch_per_tick": 64, "trace_sweep_interval_ms": 200, "trace_primary_flush_span_threshold": 1024, "trace_primary_flush_interval_ms": 10, "trace_active_session_limit": 512, "trace_buffered_span_limit": 4096},
        {"name": "limit1024_buffer8192", "connections": 30, "server_io_threads": 1, "worker_threads": 8, "dispatch_worker_threads": 1, "trace_max_dispatch_per_tick": 128, "trace_sweep_interval_ms": 100, "trace_primary_flush_span_threshold": 512, "trace_primary_flush_interval_ms": 5, "trace_active_session_limit": 1024, "trace_buffered_span_limit": 8192},
        {"name": "conn45", "connections": 45, "server_io_threads": 1, "worker_threads": 8, "dispatch_worker_threads": 1, "trace_max_dispatch_per_tick": 64, "trace_sweep_interval_ms": 200, "trace_primary_flush_span_threshold": 512, "trace_primary_flush_interval_ms": 5, "trace_active_session_limit": 512, "trace_buffered_span_limit": 4096},
        {"name": "conn45_dispatch2", "connections": 45, "server_io_threads": 1, "worker_threads": 12, "dispatch_worker_threads": 2, "trace_max_dispatch_per_tick": 128, "trace_sweep_interval_ms": 100, "trace_primary_flush_span_threshold": 512, "trace_primary_flush_interval_ms": 5, "trace_active_session_limit": 1024, "trace_buffered_span_limit": 8192},
    ]


def build_finalist_candidates() -> list[JsonDict]:
    # finalists 档来自 balanced 粗筛中 online trace/s、完成比例和 p95 都比较好的候选。
    # 它不是继续扩大网格，而是把少数高潜参数复跑多次，用均值和波动排除偶然性。
    return [
        {"name": "b017_traceps_low_p95", "connections": 30, "server_io_threads": 1, "worker_threads": 12, "dispatch_worker_threads": 1, "trace_max_dispatch_per_tick": 64, "trace_sweep_interval_ms": 100, "trace_primary_flush_span_threshold": 512, "trace_primary_flush_interval_ms": 5, "trace_active_session_limit": 1024, "trace_buffered_span_limit": 8192},
        {"name": "b031_balanced", "connections": 30, "server_io_threads": 1, "worker_threads": 12, "dispatch_worker_threads": 2, "trace_max_dispatch_per_tick": 128, "trace_sweep_interval_ms": 200, "trace_primary_flush_span_threshold": 512, "trace_primary_flush_interval_ms": 5, "trace_active_session_limit": 1024, "trace_buffered_span_limit": 8192},
        {"name": "b033_conn45_traceps", "connections": 45, "server_io_threads": 1, "worker_threads": 8, "dispatch_worker_threads": 1, "trace_max_dispatch_per_tick": 64, "trace_sweep_interval_ms": 100, "trace_primary_flush_span_threshold": 512, "trace_primary_flush_interval_ms": 5, "trace_active_session_limit": 1024, "trace_buffered_span_limit": 8192},
        {"name": "b056_best_ratio", "connections": 45, "server_io_threads": 1, "worker_threads": 12, "dispatch_worker_threads": 1, "trace_max_dispatch_per_tick": 128, "trace_sweep_interval_ms": 200, "trace_primary_flush_span_threshold": 1024, "trace_primary_flush_interval_ms": 5, "trace_active_session_limit": 1024, "trace_buffered_span_limit": 8192},
        {"name": "b063_max_traceps", "connections": 45, "server_io_threads": 1, "worker_threads": 12, "dispatch_worker_threads": 2, "trace_max_dispatch_per_tick": 128, "trace_sweep_interval_ms": 200, "trace_primary_flush_span_threshold": 512, "trace_primary_flush_interval_ms": 5, "trace_active_session_limit": 1024, "trace_buffered_span_limit": 8192},
    ]


def build_grid_candidates(preset: str) -> list[JsonDict]:
    if preset == "quick":
        return build_quick_candidates()
    if preset == "finalists":
        return build_finalist_candidates()

    if preset == "balanced":
        dimensions = {
            "connections": [30, 45],
            "worker_threads": [8, 12],
            "dispatch_worker_threads": [1, 2],
            "trace_max_dispatch_per_tick": [64, 128],
            "trace_sweep_interval_ms": [100, 200],
            "trace_primary_flush_span_threshold": [512, 1024],
            "trace_primary_flush_interval_ms": [5],
        }
    else:
        dimensions = {
            "connections": [30, 45, 60],
            "worker_threads": [8, 12, 16],
            "dispatch_worker_threads": [1, 2],
            "trace_max_dispatch_per_tick": [64, 128, 256],
            "trace_sweep_interval_ms": [50, 100, 200],
            "trace_primary_flush_span_threshold": [256, 512, 1024],
            "trace_primary_flush_interval_ms": [5, 10],
        }

    keys = list(dimensions)
    candidates: list[JsonDict] = []
    for index, values in enumerate(product(*(dimensions[key] for key in keys)), start=1):
        candidate = dict(zip(keys, values))
        active_limit = 1024 if candidate["connections"] >= 45 or candidate["worker_threads"] >= 12 else 512
        candidate.update(
            {
                "name": f"{preset}_{index:03d}",
                "server_io_threads": 1,
                "trace_active_session_limit": active_limit,
                "trace_buffered_span_limit": active_limit * 8,
            }
        )
        candidates.append(candidate)
    return candidates


def build_case_args(args: argparse.Namespace, actual_root: Path, candidate: JsonDict, case_index: int, port: int) -> argparse.Namespace:
    case_dir = actual_root / f"{case_index:03d}_{candidate['name']}"
    output_json = case_dir / "result.json"
    argv = [
        "--server-bin", args.server_bin,
        "--run-root", str(case_dir),
        "--output-json", str(output_json),
        "--port-base", str(port),
        "--server-cpuset", args.server_cpuset,
        "--wrk-cpuset", args.wrk_cpuset,
        "--wrk-bin", args.wrk_bin,
        "--wrk-script", args.wrk_script,
        "--wrk-threads", "1",
        "--connections", str(candidate["connections"]),
        "--duration", args.duration,
        "--warmup-duration", args.warmup_duration,
        "--spans-per-trace", str(args.spans_per_trace),
        "--server-io-threads", str(candidate["server_io_threads"]),
        "--worker-threads", str(candidate["worker_threads"]),
        "--dispatch-worker-threads", str(candidate["dispatch_worker_threads"]),
        "--worker-queue-size", "4096",
        "--trace-active-session-limit", str(candidate["trace_active_session_limit"]),
        "--trace-buffered-span-limit", str(candidate["trace_buffered_span_limit"]),
        "--trace-max-dispatch-per-tick", str(candidate["trace_max_dispatch_per_tick"]),
        "--trace-lifecycle-profile", "protected",
        "--trace-sealed-grace-window-ms", "100",
        "--trace-sweep-interval-ms", str(candidate["trace_sweep_interval_ms"]),
        "--trace-primary-flush-span-threshold", str(candidate["trace_primary_flush_span_threshold"]),
        "--trace-primary-flush-interval-ms", str(candidate["trace_primary_flush_interval_ms"]),
        "--trace-ai-provider", args.trace_ai_provider,
        "--disable-webhook",
    ]
    return run_suite_d_case.parse_args(argv)


def final_completion_ratio(result: JsonDict) -> float:
    offered = int(result.get("wrk_metrics", {}).get("offered_traces") or 0)
    final_count = int(result.get("sqlite_counts_final", {}).get("trace_summary") or 0)
    return (final_count / offered) if offered > 0 else 0.0


def rank_key(row: JsonDict) -> tuple[float, float, float, float, float, str]:
    # 现场性能演示优先看窗口内平均完成 trace/s。
    # final/online ratio 仍然参与排序，用来过滤“入口很猛但后台积压严重”的候选。
    return (
        -float(row.get("online_completed_traces_per_sec") or 0.0),
        -float(row.get("final_completion_ratio") or 0.0),
        -float(row.get("online_completion_ratio") or 0.0),
        float(row.get("latency_p95_ms") or 0.0),
        float(row.get("drain_tail_ms") or 0.0),
        str(row.get("case_id") or ""),
    )


def mean(values: list[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def population_stddev(values: list[float]) -> float:
    if len(values) <= 1:
        return 0.0
    avg = mean(values)
    return (sum((value - avg) ** 2 for value in values) / len(values)) ** 0.5


def aggregate_candidate_rows(rows: list[JsonDict]) -> list[JsonDict]:
    grouped: dict[str, list[JsonDict]] = {}
    for row in rows:
        if row.get("status") != "ok":
            continue
        params = row.get("params") if isinstance(row.get("params"), dict) else {}
        name = str(params.get("name") or row.get("case_id") or "")
        grouped.setdefault(name, []).append(row)

    aggregates: list[JsonDict] = []
    for name, group in grouped.items():
        first_params = group[0].get("params") if isinstance(group[0].get("params"), dict) else {}
        online_values = [float(item.get("online_completed_traces_per_sec") or 0.0) for item in group]
        final_values = [float(item.get("final_completion_ratio") or 0.0) for item in group]
        p95_values = [float(item.get("latency_p95_ms") or 0.0) for item in group]
        qps_values = [float(item.get("requests_per_sec") or 0.0) for item in group]
        aggregates.append(
            {
                "name": name,
                "runs": len(group),
                "params": first_params,
                "online_completed_traces_per_sec_avg": mean(online_values),
                "online_completed_traces_per_sec_min": min(online_values),
                "online_completed_traces_per_sec_max": max(online_values),
                "online_completed_traces_per_sec_stddev": population_stddev(online_values),
                "final_completion_ratio_avg": mean(final_values),
                "latency_p95_ms_avg": mean(p95_values),
                "requests_per_sec_avg": mean(qps_values),
                "case_ids": [str(item.get("case_id") or "") for item in group],
            }
        )
    return sorted(
        aggregates,
        key=lambda item: (
            -float(item["online_completed_traces_per_sec_avg"]),
            -float(item["final_completion_ratio_avg"]),
            float(item["latency_p95_ms_avg"]),
            float(item["online_completed_traces_per_sec_stddev"]),
            str(item["name"]),
        ),
    )


def compact_row(case_id: str, candidate: JsonDict, result: JsonDict, status: str, error: str = "") -> JsonDict:
    wrk = result.get("wrk_metrics", {}) if isinstance(result, dict) else {}
    row: JsonDict = {
        "case_id": case_id,
        "status": status,
        "error": error,
        "params": candidate,
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
                "drain_tail_ms": int(result.get("drain_tail_ms") or 0),
                "final_trace_summary": int(result.get("sqlite_counts_final", {}).get("trace_summary") or 0),
            }
        )
    return row


def cleanup_case_sqlite_db(row: JsonDict) -> None:
    sqlite_path = Path(str(row.get("sqlite_db") or ""))
    if not str(sqlite_path) or not sqlite_path.exists():
        return
    # 参数搜索只需要 result.json 指标和 server.log 诊断。
    # SQLite DB 通常最占空间，所以默认删掉，避免多轮搜索把磁盘打满。
    sqlite_path.unlink()
    row["sqlite_db_removed"] = True


def print_candidate_line(index: int, total: int, row: JsonDict) -> None:
    if row["status"] != "ok":
        print(f"[search {index}/{total}] {row['case_id']} FAILED error={row['error']}")
        return
    print(
        f"[search {index}/{total}] {row['case_id']} "
        f"final={row['final_completion_ratio']:.4f} "
        f"online_ratio={row['online_completion_ratio']:.4f} "
        f"online={row['online_completed_traces_per_sec']:.2f} "
        f"qps={row['requests_per_sec']:.2f} "
        f"p95={row['latency_p95_ms']:.2f}ms "
        f"drain={row['drain_tail_ms']}ms"
    )


def run_search(args: argparse.Namespace) -> JsonDict:
    candidates = build_grid_candidates(args.preset)
    if args.max_cases > 0:
        candidates = candidates[: args.max_cases]

    actual_root = Path(f"{args.run_root}-{format_run_timestamp()}")
    summary_json = Path(args.summary_json) if args.summary_json else actual_root / "summary.json"
    actual_root.mkdir(parents=True, exist_ok=True)

    if args.dry_run:
        summary = {"dry_run": True, "preset": args.preset, "candidate_count": len(candidates), "candidates": candidates}
        summary_json.parent.mkdir(parents=True, exist_ok=True)
        summary_json.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"[suite-d-param-search] dry_run candidates={len(candidates)} summary={summary_json}")
        return summary

    used_ports: set[int] = set()
    rows: list[JsonDict] = []
    scheduled: list[tuple[int, int, JsonDict]] = []
    for index, candidate in enumerate(candidates, start=1):
        for repeat in range(1, args.repeats + 1):
            scheduled.append((index, repeat, candidate))

    for run_index, (candidate_index, repeat, candidate) in enumerate(scheduled, start=1):
        case_id = f"{candidate_index:03d}_r{repeat:02d}_{candidate['name']}"
        port = find_free_port(args.port_start, args.port_search_limit, used_ports)
        case_args = build_case_args(args, actual_root, candidate, run_index, port)
        try:
            result = run_suite_d_case.run_suite_d_case(case_args)
            row = compact_row(case_id, candidate, result, "ok")
            if not args.keep_sqlite_db:
                cleanup_case_sqlite_db(row)
        except Exception as exc:  # noqa: BLE001
            # 搜索脚本不能因为单个候选失败就丢掉前面结果；除非显式 fail-fast。
            row = compact_row(case_id, candidate, {}, "failed", str(exc))
            if args.fail_fast:
                raise
        rows.append(row)
        print_candidate_line(run_index, len(scheduled), row)

        if args.cooldown_sec > 0:
            # case 之间冷却一小段时间，降低上一轮 SQLite / proxy / OS writeback 对下一轮的污染。
            time.sleep(args.cooldown_sec)

    ok_rows = [row for row in rows if row.get("status") == "ok"]
    top_rows = sorted(ok_rows, key=rank_key)[: args.top_n]
    aggregate_top = aggregate_candidate_rows(rows)[: args.top_n]
    summary = {
        "dry_run": False,
        "preset": args.preset,
        "requested_run_root": args.run_root,
        "actual_run_root": str(actual_root),
        "summary_json": str(summary_json),
        "candidate_count": len(candidates),
        "repeats": args.repeats,
        "scheduled_case_count": len(scheduled),
        "ok_count": len(ok_rows),
        "failed_count": len(rows) - len(ok_rows),
        "sqlite_db_policy": "keep" if args.keep_sqlite_db else "delete_after_case",
        "rows": rows,
        "top": top_rows,
        "aggregate_top": aggregate_top,
    }
    summary_json.parent.mkdir(parents=True, exist_ok=True)
    summary_json.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print()
    print(f"[suite-d-param-search] summary={summary_json}")
    print("[suite-d-param-search] top candidates")
    for rank, row in enumerate(top_rows, start=1):
        params = row["params"]
        print(
            f"#{rank} {row['case_id']} "
            f"final={row['final_completion_ratio']:.4f} "
            f"online_ratio={row['online_completion_ratio']:.4f} "
            f"online={row['online_completed_traces_per_sec']:.2f} "
            f"qps={row['requests_per_sec']:.2f} "
            f"p95={row['latency_p95_ms']:.2f}ms "
            f"conn={params['connections']} "
            f"worker={params['worker_threads']} "
            f"dispatch={params['dispatch_worker_threads']} "
            f"flush={params['trace_primary_flush_span_threshold']}/{params['trace_primary_flush_interval_ms']} "
            f"sweep={params['trace_sweep_interval_ms']} "
            f"max_dispatch={params['trace_max_dispatch_per_tick']}"
        )
    if args.repeats > 1:
        print("[suite-d-param-search] aggregate top")
        for rank, item in enumerate(aggregate_top, start=1):
            params = item["params"]
            print(
                f"@{rank} {item['name']} "
                f"runs={item['runs']} "
                f"online_avg={item['online_completed_traces_per_sec_avg']:.2f} "
                f"online_std={item['online_completed_traces_per_sec_stddev']:.2f} "
                f"final_avg={item['final_completion_ratio_avg']:.4f} "
                f"p95_avg={item['latency_p95_ms_avg']:.2f}ms "
                f"qps_avg={item['requests_per_sec_avg']:.2f} "
                f"conn={params['connections']} "
                f"worker={params['worker_threads']} "
                f"dispatch={params['dispatch_worker_threads']} "
                f"flush={params['trace_primary_flush_span_threshold']}/{params['trace_primary_flush_interval_ms']} "
                f"sweep={params['trace_sweep_interval_ms']} "
                f"max_dispatch={params['trace_max_dispatch_per_tick']}"
            )
    return summary


def main() -> int:
    run_search(parse_args())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
