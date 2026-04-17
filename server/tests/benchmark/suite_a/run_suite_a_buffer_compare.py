#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Callable, Dict, List, Optional

import run_suite_a_case
from benchmark_metadata import attach_benchmark_metadata, build_cpu_allocation


JsonDict = Dict[str, object]
DEFAULT_COMPARE_ROOT = "server/tests/benchmark/results/suite_a/buffer_compare"
DEFAULT_TRACE_LIFECYCLE_PROFILE = "protected"
DEFAULT_TRACE_SEALED_GRACE_WINDOW_MS = 100
DEFAULT_TRACE_SWEEP_INTERVAL_MS = 200
DEFAULT_BUFFERED_SPAN_THRESHOLDS = "512,128,64"
DEFAULT_BUFFERED_FLUSH_INTERVAL_MS = "5"
DEFAULT_TOP_K = 2
CONTROLLED_CASE_ARGS = {
    "--disable-ai",
    "--disable-buffered-trace-repo",
    "--inter-trace-gap-ms",
    "--output-json",
    "--port-base",
    "--run-root",
    "--server-log",
    "--sqlite-db",
    "--trace-lifecycle-profile",
    "--trace-primary-flush-interval-ms",
    "--trace-primary-flush-span-threshold",
    "--trace-sealed-grace-window-ms",
    "--trace-sweep-interval-ms",
    "--url",
}


def parse_csv_ints(value: str, option_name: str) -> List[int]:
    values: List[int] = []
    for item in value.split(","):
        item = item.strip()
        if item:
            values.append(int(item))
    if not values:
        raise ValueError(f"{option_name} must contain at least one integer")
    return values


def reject_controlled_case_args(case_args: List[str]) -> None:
    for token in case_args:
        option = token.split("=", 1)[0]
        if option in CONTROLLED_CASE_ARGS:
            raise ValueError(
                f"{option} is controlled by run_suite_a_buffer_compare.py; "
                "use compare-level options instead"
            )


def ensure_required_case_args(case_args: List[str]) -> None:
    # compare runner 需要知道固定发送规模，才能保证 buffered/no-buffer 是同口径。
    # 如果 trace-count 只藏在外部脚本里，这里既没法校验，也没法在汇总里给出明确上下文。
    if "--trace-count" not in case_args and not any(arg.startswith("--trace-count=") for arg in case_args):
        raise ValueError("case args must include --trace-count for Suite A buffer compare")


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Suite A buffered vs disable_buffered 同口径对比 runner；"
            "固定 protected 生命周期基线，只比较仓储层 flush/buffer 策略"
        )
    )
    parser.add_argument("--compare-root", default=DEFAULT_COMPARE_ROOT)
    parser.add_argument("--output-summary", default="")
    parser.add_argument("--gap-ms", type=int, default=20)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--port-base", type=int, default=18180)
    parser.add_argument("--port-stride", type=int, default=1)
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
    parser.add_argument("--buffered-span-thresholds", default=DEFAULT_BUFFERED_SPAN_THRESHOLDS)
    parser.add_argument("--buffered-flush-interval-ms", default=DEFAULT_BUFFERED_FLUSH_INTERVAL_MS)
    parser.add_argument("--ai-mode", choices=("off", "on"), default="off")
    parser.add_argument("--top-k", type=int, default=DEFAULT_TOP_K)
    args, case_args = parser.parse_known_args(argv)
    if case_args and case_args[0] == "--":
        case_args = case_args[1:]

    reject_controlled_case_args(case_args)
    ensure_required_case_args(case_args)

    args.trace_lifecycle_profile = args.trace_lifecycle_profile.strip().lower()
    if args.trace_lifecycle_profile != "protected":
        raise ValueError("run_suite_a_buffer_compare.py currently only supports protected profile")

    args.buffered_span_threshold_values = parse_csv_ints(
        args.buffered_span_thresholds,
        "--buffered-span-thresholds",
    )
    args.buffered_flush_interval_values = parse_csv_ints(
        args.buffered_flush_interval_ms,
        "--buffered-flush-interval-ms",
    )
    args.case_args = case_args

    if args.gap_ms <= 0:
        raise ValueError("--gap-ms must be > 0")
    if args.repeats <= 0:
        raise ValueError("--repeats must be > 0")
    if args.port_stride <= 0:
        raise ValueError("--port-stride must be > 0")
    if args.trace_sealed_grace_window_ms <= 0:
        raise ValueError("--trace-sealed-grace-window-ms must be > 0")
    if args.trace_sweep_interval_ms <= 0:
        raise ValueError("--trace-sweep-interval-ms must be > 0")
    if args.top_k <= 0:
        raise ValueError("--top-k must be > 0")
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


def build_candidates(args: argparse.Namespace) -> List[JsonDict]:
    # 这里故意不用“继续搜更多参数”。
    # 当前目标不是再做搜索，而是把 Stage 1 已经筛出来的 protected 最优点位，
    # 和 disable_buffered_trace_repo 放到同一条基线上正面对比。
    candidates: List[JsonDict] = [
        {
            "candidate_name": "disable_buffered",
            "candidate_mode": "disable_buffered",
            "trace_lifecycle_profile": args.trace_lifecycle_profile,
            "trace_sealed_grace_window_ms": args.trace_sealed_grace_window_ms,
            "trace_sweep_interval_ms": args.trace_sweep_interval_ms,
            "trace_primary_flush_span_threshold": 0,
            "trace_primary_flush_interval_ms": 0,
            "disable_buffered_trace_repo": True,
            "ai_mode": args.ai_mode,
        }
    ]
    for span_threshold in args.buffered_span_threshold_values:
        for flush_interval_ms in args.buffered_flush_interval_values:
            candidates.append(
                {
                    "candidate_name": f"buffered_{span_threshold}_{flush_interval_ms}",
                    "candidate_mode": "buffered",
                    "trace_lifecycle_profile": args.trace_lifecycle_profile,
                    "trace_sealed_grace_window_ms": args.trace_sealed_grace_window_ms,
                    "trace_sweep_interval_ms": args.trace_sweep_interval_ms,
                    "trace_primary_flush_span_threshold": span_threshold,
                    "trace_primary_flush_interval_ms": flush_interval_ms,
                    "disable_buffered_trace_repo": False,
                    "ai_mode": args.ai_mode,
                }
            )
    return candidates


def remove_flag(argv: List[str], flag: str) -> List[str]:
    return [token for token in argv if token != flag]


def build_case_argv(
    args: argparse.Namespace,
    candidate: JsonDict,
    candidate_index: int,
    repeat_index: int,
    global_run_index: int,
) -> List[str]:
    candidate_name = str(candidate["candidate_name"])
    run_root = Path(args.actual_compare_root) / candidate_name / f"run_{repeat_index + 1:02d}"
    output_json = run_root / "result.json"
    port_base = args.port_base + global_run_index * args.port_stride

    case_argv = list(args.case_args)
    case_argv.extend(
        [
            "--inter-trace-gap-ms",
            str(args.gap_ms),
            "--trace-lifecycle-profile",
            str(candidate["trace_lifecycle_profile"]),
            "--trace-sealed-grace-window-ms",
            str(candidate["trace_sealed_grace_window_ms"]),
            "--trace-sweep-interval-ms",
            str(candidate["trace_sweep_interval_ms"]),
            "--run-root",
            str(run_root),
            "--output-json",
            str(output_json),
            "--port-base",
            str(port_base),
        ]
    )

    # disable_buffered 和 buffered 的差异只收口在 repository 这一层：
    # 前者强制走直写 SQLite，后者继续走 buffered repo + flush 参数。
    # 生命周期参数和发送流量都保持一致，避免把两个变量混在一起。
    if bool(candidate["disable_buffered_trace_repo"]):
        case_argv.append("--disable-buffered-trace-repo")
    else:
        case_argv.extend(
            [
                "--trace-primary-flush-span-threshold",
                str(candidate["trace_primary_flush_span_threshold"]),
                "--trace-primary-flush-interval-ms",
                str(candidate["trace_primary_flush_interval_ms"]),
            ]
        )

    if candidate["ai_mode"] == "off":
        case_argv.append("--disable-ai")
    else:
        case_argv = remove_flag(case_argv, "--disable-ai")
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


def extract_optional_option_value(argv: List[str], option: str) -> Optional[str]:
    try:
        return extract_option_value(argv, option)
    except ValueError:
        return None


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


def conservative_tiebreak(candidate: JsonDict) -> tuple[object, ...]:
    if candidate["candidate_mode"] == "disable_buffered":
        return (1, 0, 0)
    return (
        0,
        abs(int(candidate["trace_primary_flush_span_threshold"]) - 512),
        abs(int(candidate["trace_primary_flush_interval_ms"]) - 5),
    )


def candidate_sort_key(candidate: JsonDict) -> tuple[object, ...]:
    # 这里的排序口径故意先看 visible，再看 drain。
    # 因为 Suite A 当前最重要的问题是“停表时到底看到了多少主数据”，
    # drain 只是当 visible 接近时再比较尾巴有多长，避免为了缩短几十毫秒尾巴反而丢掉更多可见完成率。
    return (
        1 if bool(candidate["drain_timeout"]) else 0,
        -float(candidate["visible_completion_rate_at_stop"]),
        float(candidate["drain_tail_ms"]),
        conservative_tiebreak(candidate),
    )


def aggregate_candidate(candidate: JsonDict, repeat_runs: List[JsonDict], case_argvs: List[List[str]]) -> JsonDict:
    aggregated = dict(candidate)
    aggregated["trace_count"] = int(repeat_runs[-1].get("trace_count") or 0)
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
    aggregated["requested_run_root"] = repeat_runs[-1].get("requested_run_root")
    aggregated["actual_run_root"] = repeat_runs[-1].get("actual_run_root")
    return aggregated


def format_flush_label(candidate: JsonDict) -> str:
    if candidate["candidate_mode"] == "disable_buffered":
        return "off"
    return (
        f"{candidate['trace_primary_flush_span_threshold']}"
        f"/{candidate['trace_primary_flush_interval_ms']}"
    )


def format_case_line(case_index: int, total: int, candidate: JsonDict) -> str:
    return (
        f"[case {case_index}/{total}] "
        f"mode={candidate['candidate_mode']} "
        f"lifecycle={candidate['trace_lifecycle_profile']} "
        f"grace={candidate['trace_sealed_grace_window_ms']} "
        f"sweep={candidate['trace_sweep_interval_ms']} "
        f"flush={format_flush_label(candidate)} "
        f"ai={candidate['ai_mode']} "
        f"visible={float(candidate['visible_completion_rate_at_stop']):.6f} "
        f"drain={int(round(float(candidate['drain_tail_ms'])))} "
        f"timeout={'true' if candidate['drain_timeout'] else 'false'}"
    )


def format_topk_line(rank: int, candidate: JsonDict) -> str:
    return (
        f"[top{rank}] "
        f"mode={candidate['candidate_mode']} "
        f"grace={candidate['trace_sealed_grace_window_ms']} "
        f"sweep={candidate['trace_sweep_interval_ms']} "
        f"flush={format_flush_label(candidate)} "
        f"visible={float(candidate['visible_completion_rate_at_stop']):.6f} "
        f"drain={int(round(float(candidate['drain_tail_ms'])))} "
        f"timeout={'true' if candidate['drain_timeout'] else 'false'}"
    )


def run_suite_a_buffer_compare(
    args: argparse.Namespace,
    case_runner: Callable[[List[str]], JsonDict] = run_case_once,
    line_writer: Callable[[str], None] = print,
) -> JsonDict:
    ensure_compare_root_resolved(args)
    actual_root = Path(args.actual_compare_root)
    actual_root.mkdir(parents=True, exist_ok=True)

    candidates = build_candidates(args)
    aggregated_candidates: List[JsonDict] = []
    global_run_index = 0
    for candidate_index, candidate in enumerate(candidates):
        repeat_runs: List[JsonDict] = []
        case_argvs: List[List[str]] = []
        for repeat_index in range(args.repeats):
            case_argv = build_case_argv(args, candidate, candidate_index, repeat_index, global_run_index)
            output_json = Path(extract_option_value(case_argv, "--output-json"))
            output_json.parent.mkdir(parents=True, exist_ok=True)
            run_result = case_runner(case_argv)
            repeat_runs.append(run_result)
            case_argvs.append(case_argv)
            global_run_index += 1

        aggregated = aggregate_candidate(candidate, repeat_runs, case_argvs)
        aggregated_candidates.append(aggregated)
        line_writer(format_case_line(candidate_index + 1, len(candidates), aggregated))

    sorted_candidates = sorted(aggregated_candidates, key=candidate_sort_key)
    top_candidates = sorted_candidates[: min(args.top_k, len(sorted_candidates))]
    for rank, candidate in enumerate(top_candidates, start=1):
        line_writer(format_topk_line(rank, candidate))

    summary = {
        "requested_compare_root": args.requested_compare_root,
        "actual_compare_root": args.actual_compare_root,
        "gap_ms": args.gap_ms,
        "repeats": args.repeats,
        "port_base": args.port_base,
        "port_stride": args.port_stride,
        "trace_lifecycle_profile": args.trace_lifecycle_profile,
        "trace_sealed_grace_window_ms": args.trace_sealed_grace_window_ms,
        "trace_sweep_interval_ms": args.trace_sweep_interval_ms,
        "ai_mode": args.ai_mode,
        "case_args": args.case_args,
        "candidate_count": len(aggregated_candidates),
        "candidates": aggregated_candidates,
        "top_candidates": top_candidates,
        "total_case_runs": global_run_index,
    }

    output_summary = args.output_summary or str(actual_root / "summary.json")
    summary = attach_benchmark_metadata(
        payload=summary,
        suite_name="suite_a",
        entry_script=__file__,
        workload={
            "gap_ms": args.gap_ms,
            "repeats": args.repeats,
            "buffered_span_thresholds": list(args.buffered_span_threshold_values),
            "buffered_flush_interval_ms": list(args.buffered_flush_interval_values),
            "top_k": args.top_k,
        },
        cpu_allocation=build_cpu_allocation(
            server_cpuset=extract_optional_option_value(args.case_args, "--server-cpuset"),
        ),
        thread_topology={
            "server_io_threads": extract_optional_option_value(args.case_args, "--server-io-threads"),
            "worker_threads": extract_optional_option_value(args.case_args, "--worker-threads"),
            "dispatch_worker_threads": extract_optional_option_value(args.case_args, "--dispatch-worker-threads"),
        },
        effective_flags={
            "trace_lifecycle_profile": args.trace_lifecycle_profile,
            "trace_sealed_grace_window_ms": args.trace_sealed_grace_window_ms,
            "trace_sweep_interval_ms": args.trace_sweep_interval_ms,
            "ai_mode": args.ai_mode,
        },
        commands={
            "argv": list(getattr(args, "cli_argv", [])),
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
    run_suite_a_buffer_compare(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
