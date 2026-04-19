#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any, Dict, Iterable, List, Tuple


JsonDict = Dict[str, Any]
DEFAULT_SUMMARY = Path("/tmp/suite_d_scaling_fixed90_24backend_summary.json")
RUNTIME_PATTERNS = (
    "[TraceRuntimeStats]",
    "[BufferedTraceRuntimeStats]",
    "SavePrimaryBatch",
    "UNIQUE",
    "database is locked",
    "queue",
    "overload",
    "critical",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Diagnose Suite D fixed-load results without hand-written remote Python snippets."
    )
    parser.add_argument(
        "--summary",
        default=str(DEFAULT_SUMMARY),
        help="fixed-load summary JSON path; default: /tmp/suite_d_scaling_fixed90_24backend_summary.json",
    )
    parser.add_argument(
        "--tail-lines",
        type=int,
        default=80,
        help="number of matching runtime-stat/log lines to print per backend point",
    )
    return parser.parse_args()


def load_json(path: Path) -> JsonDict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def fmt(value: Any, digits: int = 2) -> str:
    if value is None:
        return "NA"
    try:
        return f"{float(value):.{digits}f}"
    except (TypeError, ValueError):
        return str(value)


def parse_key_values(line: str) -> JsonDict:
    result: JsonDict = {}
    # server runtime stats 是 `key=value, key=value` 这种日志。
    # 这里用宽松正则抽字段，后面只挑关键字段展示，不依赖字段顺序。
    for key, value in re.findall(r"([A-Za-z0-9_]+)=([^,\s]+)", line):
        result[key] = value
    return result


def tail_matching_lines(path: Path, patterns: Tuple[str, ...], limit: int) -> List[str]:
    if not path.exists():
        return [f"<missing log: {path}>"]
    matched: List[str] = []
    with path.open("r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if any(pattern in line for pattern in patterns):
                matched.append(line.rstrip())
    return matched[-limit:]


def extract_last_stats(log_lines: Iterable[str]) -> Tuple[JsonDict, JsonDict]:
    trace_stats: JsonDict = {}
    buffered_stats: JsonDict = {}
    for line in log_lines:
        if "[TraceRuntimeStats]" in line:
            trace_stats = parse_key_values(line)
        if "[BufferedTraceRuntimeStats]" in line:
            buffered_stats = parse_key_values(line)
    return trace_stats, buffered_stats


def resolve_result_path(point: JsonDict) -> Path:
    candidates = []
    if point.get("requested_run_root"):
        candidates.append(Path(point["requested_run_root"]) / "result.json")
    if point.get("actual_run_root"):
        candidates.append(Path(point["actual_run_root"]) / "result.json")

    # Suite D single-case runner 当前把 result.json 写在 requested_run_root，
    # 但 SQLite/server.log 会落在 actual_run_root 时间戳目录。
    # 这里必须两边都尝试，避免诊断脚本把“结果 JSON 缺失”和“日志目录带时间戳”混成一个路径。
    for path in candidates:
        if path.exists():
            return path

    joined = ", ".join(str(path) for path in candidates) or "<no candidate paths>"
    raise FileNotFoundError(f"missing result.json for backend={point.get('backend_cores')}: {joined}")


def print_point(point: JsonDict, tail_lines: int) -> None:
    result_path = resolve_result_path(point)
    result = load_json(result_path)
    server_log = Path(result["server_log"])
    runtime_lines = tail_matching_lines(server_log, RUNTIME_PATTERNS, tail_lines)
    trace_stats, buffered_stats = extract_last_stats(runtime_lines)

    # 第一段是可直接放表格的 fixed-load 主指标。
    # 这些字段来自 result.json，不从滚屏输出里反推，避免人工复制时丢行。
    print()
    print(f"===== backend_{point['backend_cores']:02d} =====")
    print(
        "config "
        f"server_cpuset={result.get('server_cpuset')} "
        f"wrk_cpuset={result.get('wrk_cpuset')} "
        f"io={result.get('server_io_threads')} "
        f"dispatch={result.get('dispatch_worker_threads')} "
        f"workers={result.get('worker_threads')} "
        f"connections={result.get('connections')} "
        f"wrk_threads={result.get('wrk_threads')}"
    )
    print(
        "throughput "
        f"qps={fmt(result.get('wrk_metrics', {}).get('requests_per_sec'))} "
        f"offered={result.get('wrk_metrics', {}).get('offered_traces')} "
        f"stop={result.get('sqlite_counts_at_stop', {}).get('trace_summary')} "
        f"final={result.get('sqlite_counts_final', {}).get('trace_summary')} "
        f"online={fmt(result.get('online_completed_traces_per_sec'))} "
        f"ratio={fmt(result.get('online_completion_ratio'), 4)} "
        f"drain_ms={result.get('drain_tail_ms')}"
    )

    # 第二段只摘最能定位瓶颈的 runtime stats。
    # 如果高核点 completed 不涨但 flush 平均耗时/flush 次数/dispatch backlog 明显变差，就能直接指向后链路。
    if trace_stats:
        print(
            "trace_stats "
            f"dispatch_count={trace_stats.get('dispatch_count', 'NA')} "
            f"worker_done_count={trace_stats.get('worker_done_count', 'NA')} "
            f"worker_submit_fail_count={trace_stats.get('worker_submit_fail_count', 'NA')} "
            f"dispatch_queue_pending={trace_stats.get('dispatch_queue_pending', 'NA')} "
            f"worker_pending_tasks={trace_stats.get('worker_pending_tasks', 'NA')}"
        )
    else:
        print("trace_stats <missing>")

    if buffered_stats:
        print(
            "buffered_stats "
            f"primary_flush_calls={buffered_stats.get('primary_flush_calls', 'NA')} "
            f"primary_flush_fail_count={buffered_stats.get('primary_flush_fail_count', 'NA')} "
            f"primary_flush_avg_ms={buffered_stats.get('primary_flush_avg_ms', 'NA')} "
            f"primary_flushed_summary_count={buffered_stats.get('primary_flushed_summary_count', 'NA')} "
            f"primary_flushed_span_count={buffered_stats.get('primary_flushed_span_count', 'NA')}"
        )
    else:
        print("buffered_stats <missing>")

    print("--- matching log tail ---")
    if runtime_lines:
        for line in runtime_lines:
            print(line)
    else:
        print("<no matching runtime-stat lines>")


def main() -> None:
    args = parse_args()
    summary_path = Path(args.summary)
    summary = load_json(summary_path)

    print("[suite_d_fixed_load_diagnostics]")
    print(f"summary={summary_path}")
    print(f"actual_scaling_root={summary.get('actual_scaling_root')}")
    print(f"backend_core_points={summary.get('backend_core_points')}")
    print(f"fixed_load={summary.get('fixed_load')}")
    print(f"overall={summary.get('overall')}")

    for point in summary.get("by_backend_cores", []):
        print_point(point, args.tail_lines)


if __name__ == "__main__":
    main()
