#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shlex
import sys
from pathlib import Path
from typing import Callable, Dict, List, Optional

import run_suite_a_case
from benchmark_metadata import attach_benchmark_metadata, build_cpu_allocation


JsonDict = Dict[str, object]
DEFAULT_COMPARE_ROOT = "server/tests/benchmark/results/suite_a/main_vs_cmp"
DEFAULT_LOAD_POINTS = "light:800:20:1,mid:1600:10:2,heavy:3200:5:2"
DEFAULT_TRACE_LIFECYCLE_PROFILE = "protected"
DEFAULT_TRACE_SEALED_GRACE_WINDOW_MS = 100
DEFAULT_TRACE_SWEEP_INTERVAL_MS = 200
DEFAULT_TRACE_PRIMARY_FLUSH_SPAN_THRESHOLD = 512
DEFAULT_TRACE_PRIMARY_FLUSH_INTERVAL_MS = 5
CONTROLLED_CASE_ARGS = {
    "--disable-ai",
    "--disable-buffered-trace-repo",
    "--disable-webhook",
    "--dispatch-worker-threads",
    "--inter-trace-gap-ms",
    "--output-json",
    "--port-base",
    "--run-root",
    "--send-workers",
    "--server-bin",
    "--server-command",
    "--server-cpuset",
    "--server-io-threads",
    "--server-log",
    "--sqlite-db",
    "--trace-count",
    "--trace-lifecycle-profile",
    "--trace-primary-flush-interval-ms",
    "--trace-primary-flush-span-threshold",
    "--trace-sealed-grace-window-ms",
    "--trace-sweep-interval-ms",
    "--url",
    "--worker-threads",
}


def parse_load_points(value: str) -> List[JsonDict]:
    load_points: List[JsonDict] = []
    for item in value.split(","):
        item = item.strip()
        if not item:
            continue
        parts = [part.strip() for part in item.split(":")]
        if len(parts) != 4:
            raise ValueError(
                "--load-points must use label:trace_count:gap_ms:send_workers format"
            )
        label, trace_count, gap_ms, send_workers = parts
        load_points.append(
            {
                "label": label,
                "trace_count": int(trace_count),
                "gap_ms": int(gap_ms),
                "send_workers": int(send_workers),
            }
        )
    if not load_points:
        raise ValueError("--load-points must contain at least one load point")
    return load_points


def reject_controlled_case_args(case_args: List[str]) -> None:
    for token in case_args:
        option = token.split("=", 1)[0]
        if option in CONTROLLED_CASE_ARGS:
            raise ValueError(
                f"{option} is controlled by run_suite_a_main_vs_cmp.py; "
                "use compare-level options instead"
            )


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Suite A 新旧版本主叙事对比 runner；"
            "固定 main tuned protected buffered，对比 build-cmp 的 clean path"
        )
    )
    parser.add_argument("--compare-root", default=DEFAULT_COMPARE_ROOT)
    parser.add_argument("--output-summary", default="")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--port-base", type=int, default=18180)
    parser.add_argument("--port-stride", type=int, default=1)
    parser.add_argument("--main-server-bin", default="./server/build/LogSentinel")
    parser.add_argument("--cmp-server-bin", default="./server/build-cmp/LogSentinel")
    parser.add_argument("--server-cpuset", default="")
    parser.add_argument("--server-io-threads", type=int, default=1)
    parser.add_argument("--worker-threads", type=int, default=32)
    parser.add_argument("--dispatch-worker-threads", type=int, default=1)
    parser.add_argument("--load-points", default=DEFAULT_LOAD_POINTS)
    parser.add_argument("--trace-lifecycle-profile", default=DEFAULT_TRACE_LIFECYCLE_PROFILE)
    parser.add_argument(
        "--trace-sealed-grace-window-ms",
        type=int,
        default=DEFAULT_TRACE_SEALED_GRACE_WINDOW_MS,
    )
    parser.add_argument(
        "--trace-sweep-interval-ms",
        type=int,
        default=DEFAULT_TRACE_SWEEP_INTERVAL_MS,
    )
    parser.add_argument(
        "--trace-primary-flush-span-threshold",
        type=int,
        default=DEFAULT_TRACE_PRIMARY_FLUSH_SPAN_THRESHOLD,
    )
    parser.add_argument(
        "--trace-primary-flush-interval-ms",
        type=int,
        default=DEFAULT_TRACE_PRIMARY_FLUSH_INTERVAL_MS,
    )
    args, case_args = parser.parse_known_args(argv)
    if case_args and case_args[0] == "--":
        case_args = case_args[1:]

    reject_controlled_case_args(case_args)
    args.load_points = parse_load_points(args.load_points)
    args.case_args = case_args
    args.trace_lifecycle_profile = args.trace_lifecycle_profile.strip().lower()

    if args.trace_lifecycle_profile != "protected":
        raise ValueError("run_suite_a_main_vs_cmp.py currently only supports protected profile")
    if args.repeats <= 0:
        raise ValueError("--repeats must be > 0")
    if args.port_stride <= 0:
        raise ValueError("--port-stride must be > 0")
    if args.server_io_threads <= 0:
        raise ValueError("--server-io-threads must be > 0")
    if args.worker_threads <= 0:
        raise ValueError("--worker-threads must be > 0")
    if args.dispatch_worker_threads <= 0:
        raise ValueError("--dispatch-worker-threads must be > 0")
    if args.trace_sealed_grace_window_ms <= 0:
        raise ValueError("--trace-sealed-grace-window-ms must be > 0")
    if args.trace_sweep_interval_ms <= 0:
        raise ValueError("--trace-sweep-interval-ms must be > 0")
    if args.trace_primary_flush_span_threshold <= 0:
        raise ValueError("--trace-primary-flush-span-threshold must be > 0")
    if args.trace_primary_flush_interval_ms <= 0:
        raise ValueError("--trace-primary-flush-interval-ms must be > 0")
    args.cli_argv = list(argv) if argv is not None else list(sys.argv[1:])
    return args


def resolve_compare_root(compare_root_prefix: str) -> tuple[str, str]:
    requested_compare_root = str(Path(compare_root_prefix))
    actual_compare_root = f"{requested_compare_root}-{run_suite_a_case.format_run_timestamp()}"
    return requested_compare_root, actual_compare_root


def ensure_compare_root_resolved(args: argparse.Namespace) -> None:
    if hasattr(args, "actual_compare_root"):
        args.requested_compare_root = getattr(args, "requested_compare_root", args.compare_root)
        return

    requested_compare_root, actual_compare_root = resolve_compare_root(args.compare_root)
    args.requested_compare_root = requested_compare_root
    args.actual_compare_root = actual_compare_root


def shell_join(parts: List[str]) -> str:
    return " ".join(shlex.quote(part) for part in parts)


def build_variants(args: argparse.Namespace) -> List[JsonDict]:
    # 这里故意只保留两条线：
    # 1) 旧版 cmp baseline，代表“没有新生命周期包袱”的历史口径；
    # 2) 当前 main tuned，代表“带 protected 生命周期后的主线最佳已知参数组”。
    # 这样输出天然服务论文叙事，不再把 no-buffer 这种内部归因实验混进主图。
    return [
        {
            "variant_name": "cmp_baseline",
            "display_name": "cmp",
        },
        {
            "variant_name": "main_tuned",
            "display_name": "main",
        },
    ]


def build_server_command(args: argparse.Namespace, variant: JsonDict) -> str:
    parts: List[str] = []
    if args.server_cpuset:
        parts.extend(["taskset", "-c", args.server_cpuset])

    if variant["variant_name"] == "cmp_baseline":
        parts.extend(
            [
                args.cmp_server_bin,
                "--db",
                "{sqlite_db}",
                "--port",
                "{port}",
                "--worker-threads",
                str(args.worker_threads),
            ]
        )
        return shell_join(parts)

    parts.extend(
        [
            args.main_server_bin,
            "--db",
            "{sqlite_db}",
            "--port",
            "{port}",
            "--disable-ai",
            "--disable-webhook",
            "--server-io-threads",
            str(args.server_io_threads),
            "--worker-threads",
            str(args.worker_threads),
            "--dispatch-worker-threads",
            str(args.dispatch_worker_threads),
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
        ]
    )
    return shell_join(parts)


def build_case_argv(
    args: argparse.Namespace,
    load_point: JsonDict,
    variant: JsonDict,
    repeat_index: int,
    global_run_index: int,
) -> List[str]:
    run_root = (
        Path(args.actual_compare_root)
        / str(load_point["label"])
        / str(variant["variant_name"])
        / f"run_{repeat_index + 1:02d}"
    )
    output_json = run_root / "result.json"
    port_base = args.port_base + global_run_index * args.port_stride

    case_argv = list(args.case_args)
    case_argv.extend(
        [
            "--server-command",
            build_server_command(args, variant),
            "--trace-count",
            str(load_point["trace_count"]),
            "--inter-trace-gap-ms",
            str(load_point["gap_ms"]),
            "--send-workers",
            str(load_point["send_workers"]),
            "--run-root",
            str(run_root),
            "--output-json",
            str(output_json),
            "--port-base",
            str(port_base),
        ]
    )
    return case_argv


def extract_option_value(argv: List[str], option: str) -> str:
    for index, token in enumerate(argv):
        token_option = token.split("=", 1)[0]
        if token_option != option:
            continue
        if "=" in token:
            return token.split("=", 1)[1]
        if index + 1 >= len(argv):
            raise ValueError(f"{option} requires a value")
        return argv[index + 1]
    raise ValueError(f"{option} not found")


def run_case_once(case_argv: List[str]) -> JsonDict:
    case_args = run_suite_a_case.parse_args(case_argv)
    return run_suite_a_case.run_suite_a_case(case_args)


def summarize_numeric_values(values: List[float]) -> JsonDict:
    if not values:
        return {"count": 0, "mean": None, "median": None, "min": None, "max": None}
    sorted_values = sorted(float(value) for value in values)
    count = len(sorted_values)
    middle = count // 2
    if count % 2 == 1:
        median = sorted_values[middle]
    else:
        median = (sorted_values[middle - 1] + sorted_values[middle]) / 2.0
    return {
        "count": count,
        "mean": sum(sorted_values) / count,
        "median": median,
        "min": sorted_values[0],
        "max": sorted_values[-1],
    }


def summarize_bool_values(values: List[bool]) -> JsonDict:
    count = len(values)
    true_count = sum(1 for value in values if value)
    false_count = count - true_count
    return {
        "count": count,
        "true_count": true_count,
        "false_count": false_count,
        "timeout_rate": (true_count / count) if count > 0 else None,
    }


def variant_sort_key(variant_summary: JsonDict) -> tuple[object, ...]:
    return (
        1 if bool(variant_summary["drain_timeout"]) else 0,
        -float(variant_summary["visible_completion_rate_at_stop"]),
        float(variant_summary["drain_tail_ms"]),
        0 if variant_summary["variant_name"] == "cmp_baseline" else 1,
    )


def aggregate_variant(
    variant: JsonDict,
    load_point: JsonDict,
    repeat_runs: List[JsonDict],
    case_argvs: List[List[str]],
) -> JsonDict:
    aggregated = dict(variant)
    aggregated["label"] = load_point["label"]
    aggregated["trace_count"] = int(load_point["trace_count"])
    aggregated["gap_ms"] = int(load_point["gap_ms"])
    aggregated["send_workers"] = int(load_point["send_workers"])
    aggregated["repeats"] = len(repeat_runs)
    aggregated["runs"] = []
    for case_argv, run_result in zip(case_argvs, repeat_runs):
        aggregated["runs"].append(
            {
                "case_argv": case_argv,
                "requested_run_root": run_result.get("requested_run_root"),
                "actual_run_root": run_result.get("actual_run_root"),
                "visible_completion_rate_at_stop": run_result.get("visible_completion_rate_at_stop"),
                "drain_tail_ms": run_result.get("drain_tail_ms"),
                "drain_timeout": run_result.get("drain_timeout"),
            }
        )

    visible_values = [float(run["visible_completion_rate_at_stop"]) for run in repeat_runs]
    drain_values = [float(run["drain_tail_ms"]) for run in repeat_runs]
    timeout_values = [bool(run["drain_timeout"]) for run in repeat_runs]
    aggregated["visible_completion_rate_at_stop"] = summarize_numeric_values(visible_values)["mean"]
    aggregated["drain_tail_ms"] = summarize_numeric_values(drain_values)["mean"]
    aggregated["drain_timeout"] = any(timeout_values)
    aggregated["visible_completion_rate_at_stop_stats"] = summarize_numeric_values(visible_values)
    aggregated["drain_tail_ms_stats"] = summarize_numeric_values(drain_values)
    aggregated["drain_timeout_stats"] = summarize_bool_values(timeout_values)
    return aggregated


def build_load_summary(load_point: JsonDict, variant_summaries: List[JsonDict]) -> JsonDict:
    variants = {summary["variant_name"]: summary for summary in variant_summaries}
    cmp_summary = variants["cmp_baseline"]
    main_summary = variants["main_tuned"]
    delta_main_vs_cmp = {
        # delta 固定使用 main - cmp。
        # visible 为负值说明主线比旧版少看到一点数据，drain 为正值说明主线尾巴更长。
        # 这样后面讲“新增生命周期成本被压缩到多小”时，不需要再临时切换正负号语义。
        "visible_completion_rate_at_stop": float(main_summary["visible_completion_rate_at_stop"])
        - float(cmp_summary["visible_completion_rate_at_stop"]),
        "drain_tail_ms": float(main_summary["drain_tail_ms"]) - float(cmp_summary["drain_tail_ms"]),
    }
    winner = sorted(variant_summaries, key=variant_sort_key)[0]["variant_name"]
    return {
        "label": load_point["label"],
        "trace_count": int(load_point["trace_count"]),
        "gap_ms": int(load_point["gap_ms"]),
        "send_workers": int(load_point["send_workers"]),
        "variants": variants,
        "delta_main_vs_cmp": delta_main_vs_cmp,
        "winner": winner,
    }


def format_load_line(load_index: int, total: int, load_summary: JsonDict) -> str:
    cmp_summary = load_summary["variants"]["cmp_baseline"]
    main_summary = load_summary["variants"]["main_tuned"]
    delta = load_summary["delta_main_vs_cmp"]
    return (
        f"[load {load_index}/{total}] "
        f"label={load_summary['label']} "
        f"trace={load_summary['trace_count']} "
        f"gap={load_summary['gap_ms']} "
        f"send_workers={load_summary['send_workers']} "
        f"cmp_visible={float(cmp_summary['visible_completion_rate_at_stop']):.6f} "
        f"cmp_drain={int(round(float(cmp_summary['drain_tail_ms'])))} "
        f"main_visible={float(main_summary['visible_completion_rate_at_stop']):.6f} "
        f"main_drain={int(round(float(main_summary['drain_tail_ms'])))} "
        f"delta_visible={float(delta['visible_completion_rate_at_stop']):+.6f} "
        f"delta_drain={int(round(float(delta['drain_tail_ms']))):+d} "
        f"winner={load_summary['winner']}"
    )


def format_overall_line(overall: JsonDict) -> str:
    return (
        "[overall] "
        f"worst_visible_delta={float(overall['worst_visible_delta_main_vs_cmp']):+.6f} "
        f"worst_drain_delta={int(round(float(overall['worst_drain_delta_main_vs_cmp']))):+d}"
    )


def run_suite_a_main_vs_cmp(
    args: argparse.Namespace,
    case_runner: Callable[[List[str]], JsonDict] = run_case_once,
    line_writer: Callable[[str], None] = print,
) -> JsonDict:
    ensure_compare_root_resolved(args)
    actual_root = Path(args.actual_compare_root)
    actual_root.mkdir(parents=True, exist_ok=True)

    variants = build_variants(args)
    by_load: List[JsonDict] = []
    global_run_index = 0
    for load_index, load_point in enumerate(args.load_points, start=1):
        variant_summaries: List[JsonDict] = []
        for variant in variants:
            repeat_runs: List[JsonDict] = []
            case_argvs: List[List[str]] = []
            for repeat_index in range(args.repeats):
                case_argv = build_case_argv(args, load_point, variant, repeat_index, global_run_index)
                output_json = Path(extract_option_value(case_argv, "--output-json"))
                output_json.parent.mkdir(parents=True, exist_ok=True)
                run_result = case_runner(case_argv)
                repeat_runs.append(run_result)
                case_argvs.append(case_argv)
                global_run_index += 1
            variant_summaries.append(aggregate_variant(variant, load_point, repeat_runs, case_argvs))

        load_summary = build_load_summary(load_point, variant_summaries)
        by_load.append(load_summary)
        line_writer(format_load_line(load_index, len(args.load_points), load_summary))

    visible_deltas = [
        float(load_summary["delta_main_vs_cmp"]["visible_completion_rate_at_stop"]) for load_summary in by_load
    ]
    drain_deltas = [float(load_summary["delta_main_vs_cmp"]["drain_tail_ms"]) for load_summary in by_load]
    overall = {
        "load_count": len(by_load),
        "worst_visible_delta_main_vs_cmp": min(visible_deltas) if visible_deltas else 0.0,
        "worst_drain_delta_main_vs_cmp": max(drain_deltas) if drain_deltas else 0.0,
    }
    line_writer(format_overall_line(overall))

    summary = {
        "requested_compare_root": args.requested_compare_root,
        "actual_compare_root": args.actual_compare_root,
        "repeats": args.repeats,
        "port_base": args.port_base,
        "port_stride": args.port_stride,
        "main_server_bin": args.main_server_bin,
        "cmp_server_bin": args.cmp_server_bin,
        "server_cpuset": args.server_cpuset,
        "server_io_threads": args.server_io_threads,
        "worker_threads": args.worker_threads,
        "dispatch_worker_threads": args.dispatch_worker_threads,
        "trace_lifecycle_profile": args.trace_lifecycle_profile,
        "trace_sealed_grace_window_ms": args.trace_sealed_grace_window_ms,
        "trace_sweep_interval_ms": args.trace_sweep_interval_ms,
        "trace_primary_flush_span_threshold": args.trace_primary_flush_span_threshold,
        "trace_primary_flush_interval_ms": args.trace_primary_flush_interval_ms,
        "load_points": args.load_points,
        "variants": variants,
        "case_args": args.case_args,
        "by_load": by_load,
        "overall": overall,
        "total_case_runs": global_run_index,
    }

    output_summary = args.output_summary or str(actual_root / "summary.json")
    summary = attach_benchmark_metadata(
        payload=summary,
        suite_name="suite_a",
        entry_script=__file__,
        workload={
            "repeats": args.repeats,
            "load_points": list(args.load_points),
        },
        cpu_allocation=build_cpu_allocation(server_cpuset=args.server_cpuset),
        thread_topology={
            "server_io_threads": args.server_io_threads,
            "worker_threads": args.worker_threads,
            "dispatch_worker_threads": args.dispatch_worker_threads,
        },
        effective_flags={
            "trace_lifecycle_profile": args.trace_lifecycle_profile,
            "trace_sealed_grace_window_ms": args.trace_sealed_grace_window_ms,
            "trace_sweep_interval_ms": args.trace_sweep_interval_ms,
            "trace_primary_flush_span_threshold": args.trace_primary_flush_span_threshold,
            "trace_primary_flush_interval_ms": args.trace_primary_flush_interval_ms,
        },
        commands={
            "argv": list(getattr(args, "cli_argv", [])),
            "main_server_bin": args.main_server_bin,
            "cmp_server_bin": args.cmp_server_bin,
            "case_args": list(args.case_args),
        },
        artifacts={
            "summary_json": output_summary,
            "requested_compare_root": args.requested_compare_root,
            "actual_compare_root": args.actual_compare_root,
        },
    )
    output_path = Path(output_summary)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(summary, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
    return summary


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    run_suite_a_main_vs_cmp(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
