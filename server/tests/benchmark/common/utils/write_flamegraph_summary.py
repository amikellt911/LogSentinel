#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any, Dict, Optional

from benchmark_metadata import attach_benchmark_metadata, build_cpu_allocation


JsonDict = Dict[str, Any]
INT_RE = re.compile(r"^-?\d+$")
FLOAT_RE = re.compile(r"^-?\d+\.\d+$")


def coerce_value(raw_value: str) -> Any:
    value = raw_value.strip()
    if value == "<unset>":
        return None
    lowered = value.lower()
    if lowered == "true":
        return True
    if lowered == "false":
        return False
    if INT_RE.fullmatch(value):
        return int(value)
    if FLOAT_RE.fullmatch(value):
        return float(value)
    return value


def parse_key_value_log(log_path: Path) -> JsonDict:
    parsed: JsonDict = {}
    for raw_line in log_path.read_text(encoding="utf-8").splitlines():
        if "=" not in raw_line:
            continue
        key, value = raw_line.split("=", 1)
        parsed[key.strip()] = coerce_value(value)
    return parsed


def parse_trace_span_stats(raw_value: Optional[str]) -> JsonDict:
    if not raw_value:
        return {}
    parts = str(raw_value).split("|")
    if len(parts) != 3:
        return {"raw": raw_value}
    return {
        "min": coerce_value(parts[0]),
        "avg": coerce_value(parts[1]),
        "max": coerce_value(parts[2]),
    }


def build_summary(
    bench_suite: str,
    entry_script: str,
    run_summary_log: Path,
    output_json: Path,
    run_dir: Path,
    server_log: Path,
    warmup_log: Path,
    wrk_log: Path,
    perf_data: Path,
    perf_script: Path,
    flame_svg: Path,
    trace_db: Path,
) -> JsonDict:
    summary_fields = parse_key_value_log(run_summary_log)
    trace_db_snapshot = {
        "trace_summary_count": summary_fields.get("trace_summary_count"),
        "trace_span_count": summary_fields.get("trace_span_count"),
        "trace_span_stats": parse_trace_span_stats(summary_fields.get("trace_span_stats")),
    }

    summary: JsonDict = {
        "bench_suite": bench_suite,
        "profile": summary_fields.get("profile"),
        "load_generator": summary_fields.get("load_generator"),
        "trace_db_snapshot": trace_db_snapshot,
    }

    # flamegraph 只是解释图，不是参数搜索。
    # 但它一旦脱离了当时的拓扑、绑核和 trace 水位配置，图本身就失去论文证据价值；
    # 所以这里直接复用 run-summary.log，把同一轮 shell 里已经固定下来的变量重组进 JSON。
    return attach_benchmark_metadata(
        payload=summary,
        suite_name=bench_suite,
        entry_script=entry_script,
        workload={
            "profile": summary_fields.get("profile"),
            "port": summary_fields.get("port"),
            "warmup_duration": summary_fields.get("warmup_duration"),
            "warmup_connections": summary_fields.get("warmup_connections"),
            "warmup_settle_sec": summary_fields.get("warmup_settle_sec"),
            "drain_wait_sec": summary_fields.get("drain_wait_sec"),
            "duration": summary_fields.get("duration"),
            "connections": summary_fields.get("connections"),
            "wrk_threads": summary_fields.get("wrk_threads"),
            "active_pool_size": summary_fields.get("active_pool_size"),
            "trace_wrk_role_plan": summary_fields.get("trace_wrk_role_plan"),
            "load_generator": summary_fields.get("load_generator"),
            "paced_batch_traces": summary_fields.get("paced_batch_traces"),
            "paced_batch_sleep_ms": summary_fields.get("paced_batch_sleep_ms"),
            "paced_request_timeout_ms": summary_fields.get("paced_request_timeout_ms"),
            "paced_service_name": summary_fields.get("paced_service_name"),
        },
        cpu_allocation=build_cpu_allocation(
            server_cpuset=summary_fields.get("server_cpuset"),
            wrk_cpuset=summary_fields.get("wrk_cpuset"),
        ),
        thread_topology={
            "server_io_threads": summary_fields.get("server_io_threads"),
            "worker_threads": summary_fields.get("worker_threads"),
            "dispatch_worker_threads": summary_fields.get("dispatch_worker_threads"),
            "worker_queue_size": summary_fields.get("worker_queue_size"),
        },
        effective_flags={
            "trace_capacity": summary_fields.get("trace_capacity"),
            "trace_token_limit": summary_fields.get("trace_token_limit"),
            "trace_sweep_interval_ms": summary_fields.get("trace_sweep_interval_ms"),
            "trace_idle_timeout_ms": summary_fields.get("trace_idle_timeout_ms"),
            "trace_max_dispatch_per_tick": summary_fields.get("trace_max_dispatch_per_tick"),
            "trace_buffered_span_limit": summary_fields.get("trace_buffered_span_limit"),
            "trace_active_session_limit": summary_fields.get("trace_active_session_limit"),
            "trace_lifecycle_profile": summary_fields.get("trace_lifecycle_profile"),
            "disable_ai": summary_fields.get("disable_ai"),
            "disable_webhook": summary_fields.get("disable_webhook"),
            "no_auto_start_proxy": summary_fields.get("no_auto_start_proxy"),
        },
        commands={
            "wrk_script": summary_fields.get("wrk_script"),
            "load_generator": summary_fields.get("load_generator"),
            "perf_freq": summary_fields.get("perf_freq"),
            "perf_call_graph": summary_fields.get("perf_call_graph"),
            "perf_event": summary_fields.get("perf_event"),
        },
        artifacts={
            "run_summary_json": str(output_json),
            "run_summary_log": str(run_summary_log),
            "run_dir": str(run_dir),
            "server_log": str(server_log),
            "warmup_log": str(warmup_log),
            "wrk_log": str(wrk_log),
            "perf_data": str(perf_data),
            "perf_script": str(perf_script),
            "flamegraph_svg": str(flame_svg),
            "trace_db": str(trace_db),
        },
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Write structured flamegraph benchmark summary JSON")
    parser.add_argument("--bench-suite", required=True)
    parser.add_argument("--entry-script", required=True)
    parser.add_argument("--run-summary-log", required=True)
    parser.add_argument("--output-json", required=True)
    parser.add_argument("--run-dir", required=True)
    parser.add_argument("--server-log", required=True)
    parser.add_argument("--warmup-log", required=True)
    parser.add_argument("--wrk-log", required=True)
    parser.add_argument("--perf-data", required=True)
    parser.add_argument("--perf-script", required=True)
    parser.add_argument("--flame-svg", required=True)
    parser.add_argument("--trace-db", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    summary = build_summary(
        bench_suite=args.bench_suite,
        entry_script=args.entry_script,
        run_summary_log=Path(args.run_summary_log),
        output_json=Path(args.output_json),
        run_dir=Path(args.run_dir),
        server_log=Path(args.server_log),
        warmup_log=Path(args.warmup_log),
        wrk_log=Path(args.wrk_log),
        perf_data=Path(args.perf_data),
        perf_script=Path(args.perf_script),
        flame_svg=Path(args.flame_svg),
        trace_db=Path(args.trace_db),
    )
    output_path = Path(args.output_json)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(summary, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
