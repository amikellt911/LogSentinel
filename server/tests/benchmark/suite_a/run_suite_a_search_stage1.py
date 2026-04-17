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
DEFAULT_SEARCH_ROOT = "server/tests/benchmark/results/suite_a/stage1_search"
DEFAULT_TRACE_LIFECYCLE_PROFILE = "protected"
DEFAULT_PHASE_A_SEALED_GRACE_MS = "1000,500,200,100"
DEFAULT_PHASE_A_SWEEP_MS = "500,200,100,50"
DEFAULT_PHASE_B_SPAN_THRESHOLDS = "512,256,128,64"
DEFAULT_PHASE_B_FLUSH_INTERVAL_MS = "200,50,5"
DEFAULT_PHASE_A_SPAN_THRESHOLD = 512
DEFAULT_PHASE_A_FLUSH_INTERVAL_MS = 200
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
    "--trace-sealed-grace-window-ms",
    "--trace-primary-flush-interval-ms",
    "--trace-primary-flush-span-threshold",
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


def parse_csv_strings(value: str, option_name: str) -> List[str]:
    values = [item.strip() for item in value.split(",") if item.strip()]
    if not values:
        raise ValueError(f"{option_name} must contain at least one non-empty value")
    return values


def reject_controlled_case_args(case_args: List[str]) -> None:
    for token in case_args:
        option = token.split("=", 1)[0]
        if option in CONTROLLED_CASE_ARGS:
            raise ValueError(
                f"{option} is controlled by run_suite_a_search_stage1.py; "
                "use stage-level options instead"
            )


def ensure_required_case_args(case_args: List[str]) -> None:
    # Phase C 要把 trace-count 从长跑口径压成更小的 AI-on smoke。
    # 如果基准 case 根本没显式给 trace-count，这个脚本就没法可靠替换。
    if "--trace-count" not in case_args and not any(arg.startswith("--trace-count=") for arg in case_args):
        raise ValueError("case args must include --trace-count for Stage 1 search")


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Suite A Stage 1 粗搜 runner；默认只搜索 protected 生命周期下的 grace/sweep/buffer 参数"
    )
    parser.add_argument("--search-root", default=DEFAULT_SEARCH_ROOT)
    parser.add_argument("--output-summary", default="")
    parser.add_argument("--gap-ms", type=int, default=20)
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--port-base", type=int, default=18180)
    parser.add_argument("--port-stride", type=int, default=1)
    # 这一版 Stage 1 默认只救 protected，不再让 minimal 抢主候选。
    # 如果后面真要做诊断型对照，可以单独起另一条搜索命令，不和主叙事混在一起。
    parser.add_argument("--trace-lifecycle-profile", default=DEFAULT_TRACE_LIFECYCLE_PROFILE)
    parser.add_argument("--phase-a-sealed-grace-ms", default=DEFAULT_PHASE_A_SEALED_GRACE_MS)
    parser.add_argument("--phase-a-sweep-ms", default=DEFAULT_PHASE_A_SWEEP_MS)
    parser.add_argument("--phase-b-span-thresholds", default=DEFAULT_PHASE_B_SPAN_THRESHOLDS)
    parser.add_argument("--phase-b-flush-interval-ms", default=DEFAULT_PHASE_B_FLUSH_INTERVAL_MS)
    parser.add_argument("--phase-c-top-k", type=int, default=3)
    parser.add_argument("--phase-c-trace-count", type=int, default=160)
    args, case_args = parser.parse_known_args(argv)
    if case_args and case_args[0] == "--":
        case_args = case_args[1:]

    reject_controlled_case_args(case_args)
    ensure_required_case_args(case_args)

    args.trace_lifecycle_profile = args.trace_lifecycle_profile.strip().lower()
    if not args.trace_lifecycle_profile:
        raise ValueError("--trace-lifecycle-profile must not be empty")
    args.phase_a_sealed_grace_ms_values = parse_csv_ints(
        args.phase_a_sealed_grace_ms,
        "--phase-a-sealed-grace-ms",
    )
    args.phase_a_sweep_ms_values = parse_csv_ints(args.phase_a_sweep_ms, "--phase-a-sweep-ms")
    args.phase_b_span_thresholds = parse_csv_ints(
        args.phase_b_span_thresholds,
        "--phase-b-span-thresholds",
    )
    args.phase_b_flush_interval_ms_values = parse_csv_ints(
        args.phase_b_flush_interval_ms,
        "--phase-b-flush-interval-ms",
    )
    args.case_args = case_args

    if args.gap_ms <= 0:
        raise ValueError("--gap-ms must be > 0")
    if args.repeats <= 0:
        raise ValueError("--repeats must be > 0")
    if args.port_stride <= 0:
        raise ValueError("--port-stride must be > 0")
    if args.phase_c_top_k <= 0:
        raise ValueError("--phase-c-top-k must be > 0")
    if args.phase_c_trace_count <= 0:
        raise ValueError("--phase-c-trace-count must be > 0")
    args.cli_argv = list(argv) if argv is not None else list(sys.argv[1:])
    return args


def resolve_search_root(search_root_prefix: str) -> tuple[str, str]:
    requested_search_root = str(Path(search_root_prefix))
    actual_search_root = f"{requested_search_root}-{run_suite_a_case.format_run_timestamp()}"
    return requested_search_root, actual_search_root


def ensure_search_root_resolved(args: argparse.Namespace) -> None:
    if hasattr(args, "actual_search_root"):
        args.requested_search_root = getattr(args, "requested_search_root", args.search_root)
        return

    requested_search_root, actual_search_root = resolve_search_root(args.search_root)
    args.requested_search_root = requested_search_root
    args.actual_search_root = actual_search_root


def replace_or_append_option(argv: List[str], option: str, value: str) -> List[str]:
    replaced: List[str] = []
    i = 0
    found = False
    while i < len(argv):
        token = argv[i]
        token_option = token.split("=", 1)[0]
        if token_option == option:
            found = True
            if "=" in token:
                replaced.append(f"{option}={value}")
                i += 1
                continue
            replaced.extend([option, value])
            i += 2
            continue
        replaced.append(token)
        i += 1

    if not found:
        replaced.extend([option, value])
    return replaced


def remove_flag(argv: List[str], flag: str) -> List[str]:
    return [token for token in argv if token != flag]


def build_phase_a_candidates(args: argparse.Namespace) -> List[JsonDict]:
    candidates: List[JsonDict] = []
    for sealed_grace_ms in args.phase_a_sealed_grace_ms_values:
        for sweep_ms in args.phase_a_sweep_ms_values:
            candidates.append(
                {
                    "phase": "phase_a",
                    "trace_lifecycle_profile": args.trace_lifecycle_profile,
                    "trace_sealed_grace_window_ms": sealed_grace_ms,
                    "trace_sweep_interval_ms": sweep_ms,
                    "trace_primary_flush_span_threshold": DEFAULT_PHASE_A_SPAN_THRESHOLD,
                    "trace_primary_flush_interval_ms": DEFAULT_PHASE_A_FLUSH_INTERVAL_MS,
                    "trace_count_override": None,
                    "ai_mode": "off",
                }
            )
    return candidates


def build_phase_b_candidates(args: argparse.Namespace, base_candidate: JsonDict) -> List[JsonDict]:
    candidates: List[JsonDict] = []
    for span_threshold in args.phase_b_span_thresholds:
        for flush_interval_ms in args.phase_b_flush_interval_ms_values:
            candidates.append(
                {
                    "phase": "phase_b",
                    "trace_lifecycle_profile": base_candidate["trace_lifecycle_profile"],
                    "trace_sealed_grace_window_ms": base_candidate["trace_sealed_grace_window_ms"],
                    "trace_sweep_interval_ms": base_candidate["trace_sweep_interval_ms"],
                    "trace_primary_flush_span_threshold": span_threshold,
                    "trace_primary_flush_interval_ms": flush_interval_ms,
                    "trace_count_override": None,
                    "ai_mode": "off",
                }
            )
    return candidates


def build_phase_c_candidates(args: argparse.Namespace, top_candidates: List[JsonDict]) -> List[JsonDict]:
    smoke_candidates: List[JsonDict] = []
    for candidate in top_candidates[: args.phase_c_top_k]:
        smoke_candidate = dict(candidate)
        smoke_candidate["phase"] = "phase_c"
        smoke_candidate["trace_count_override"] = args.phase_c_trace_count
        smoke_candidate["ai_mode"] = "on"
        smoke_candidates.append(smoke_candidate)
    return smoke_candidates


def build_case_argv(
    args: argparse.Namespace,
    candidate: JsonDict,
    candidate_index: int,
    repeat_index: int,
    global_run_index: int,
) -> List[str]:
    phase = str(candidate["phase"])
    case_name = f"case_{candidate_index + 1:02d}"
    if args.repeats > 1:
        case_name = f"{case_name}_repeat_{repeat_index + 1:02d}"
    run_root = Path(args.actual_search_root) / phase / case_name
    output_json = run_root / "result.json"
    port_base = args.port_base + global_run_index * args.port_stride

    case_argv = list(args.case_args)
    case_argv = replace_or_append_option(case_argv, "--trace-count", str(candidate.get("trace_count_override") or extract_option_value(case_argv, "--trace-count")))
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
            "--trace-primary-flush-span-threshold",
            str(candidate["trace_primary_flush_span_threshold"]),
            "--trace-primary-flush-interval-ms",
            str(candidate["trace_primary_flush_interval_ms"]),
            "--run-root",
            str(run_root),
            "--output-json",
            str(output_json),
            "--port-base",
            str(port_base),
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
    return (
        0 if candidate["trace_lifecycle_profile"] == "protected" else 1,
        abs(int(candidate["trace_sealed_grace_window_ms"]) - 1000),
        abs(int(candidate["trace_sweep_interval_ms"]) - 500),
        abs(int(candidate["trace_primary_flush_span_threshold"]) - 512),
        abs(int(candidate["trace_primary_flush_interval_ms"]) - 200),
    )


def candidate_sort_key(candidate: JsonDict) -> tuple[object, ...]:
    return (
        1 if bool(candidate["drain_timeout"]) else 0,
        -float(candidate["visible_completion_rate_at_stop"]),
        float(candidate["drain_tail_ms"]),
        conservative_tiebreak(candidate),
    )


def aggregate_candidate(candidate: JsonDict, repeat_runs: List[JsonDict], case_argvs: List[List[str]]) -> JsonDict:
    aggregated = dict(candidate)
    aggregated["trace_count"] = int(
        candidate.get("trace_count_override") or repeat_runs[-1].get("trace_count") or 0
    )
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


def format_case_line(phase_index: int, total: int, candidate: JsonDict) -> str:
    return (
        f"[{candidate['phase']} {phase_index}/{total}] "
        f"lifecycle={candidate['trace_lifecycle_profile']} "
        f"grace={candidate['trace_sealed_grace_window_ms']} "
        f"sweep={candidate['trace_sweep_interval_ms']} "
        f"flush={candidate['trace_primary_flush_span_threshold']}/{candidate['trace_primary_flush_interval_ms']} "
        f"ai={candidate['ai_mode']} "
        f"visible={float(candidate['visible_completion_rate_at_stop']):.6f} "
        f"drain={int(round(float(candidate['drain_tail_ms'])))} "
        f"timeout={'true' if candidate['drain_timeout'] else 'false'}"
    )


def format_topk_line(rank: int, candidate: JsonDict) -> str:
    return (
        f"[top{rank}] "
        f"phase={candidate['phase']} "
        f"lifecycle={candidate['trace_lifecycle_profile']} "
        f"grace={candidate['trace_sealed_grace_window_ms']} "
        f"sweep={candidate['trace_sweep_interval_ms']} "
        f"flush={candidate['trace_primary_flush_span_threshold']}/{candidate['trace_primary_flush_interval_ms']} "
        f"visible={float(candidate['visible_completion_rate_at_stop']):.6f} "
        f"drain={int(round(float(candidate['drain_tail_ms'])))} "
        f"timeout={'true' if candidate['drain_timeout'] else 'false'}"
    )


def execute_phase(
    args: argparse.Namespace,
    candidates: List[JsonDict],
    global_run_index: int,
    case_runner: Callable[[List[str]], JsonDict],
    line_writer: Callable[[str], None],
) -> tuple[List[JsonDict], int]:
    phase_results: List[JsonDict] = []
    total = len(candidates)

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
        phase_results.append(aggregated)
        line_writer(format_case_line(candidate_index + 1, total, aggregated))

    return phase_results, global_run_index


def run_stage1_search(
    args: argparse.Namespace,
    case_runner: Callable[[List[str]], JsonDict] = run_case_once,
    line_writer: Callable[[str], None] = print,
) -> JsonDict:
    ensure_search_root_resolved(args)
    actual_root = Path(args.actual_search_root)
    actual_root.mkdir(parents=True, exist_ok=True)

    phase_a_candidates = build_phase_a_candidates(args)
    phase_a_results, global_run_index = execute_phase(
        args,
        phase_a_candidates,
        global_run_index=0,
        case_runner=case_runner,
        line_writer=line_writer,
    )
    phase_a_sorted = sorted(phase_a_results, key=candidate_sort_key)
    phase_a_best = phase_a_sorted[0]

    phase_b_candidates = build_phase_b_candidates(args, phase_a_best)
    phase_b_results, global_run_index = execute_phase(
        args,
        phase_b_candidates,
        global_run_index=global_run_index,
        case_runner=case_runner,
        line_writer=line_writer,
    )
    phase_b_sorted = sorted(phase_b_results, key=candidate_sort_key)
    phase_b_top_candidates = phase_b_sorted[: min(args.phase_c_top_k, len(phase_b_sorted))]

    phase_c_candidates = build_phase_c_candidates(args, phase_b_top_candidates)
    phase_c_results, global_run_index = execute_phase(
        args,
        phase_c_candidates,
        global_run_index=global_run_index,
        case_runner=case_runner,
        line_writer=line_writer,
    )

    for rank, candidate in enumerate(phase_b_top_candidates, start=1):
        line_writer(format_topk_line(rank, candidate))

    summary = {
        "requested_search_root": args.requested_search_root,
        "actual_search_root": args.actual_search_root,
        "gap_ms": args.gap_ms,
        "repeats": args.repeats,
        "port_base": args.port_base,
        "port_stride": args.port_stride,
        "trace_lifecycle_profile": args.trace_lifecycle_profile,
        "case_args": args.case_args,
        "phase_a": {
            "candidates": phase_a_results,
            "best_candidate": phase_a_best,
        },
        "phase_b": {
            "base_candidate": phase_a_best,
            "candidates": phase_b_results,
            "top_candidates": phase_b_top_candidates,
        },
        "phase_c": {
            "smoke_candidates": phase_c_results,
        },
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
            "phase_a_sealed_grace_ms": list(args.phase_a_sealed_grace_ms_values),
            "phase_a_sweep_ms": list(args.phase_a_sweep_ms_values),
            "phase_b_span_thresholds": list(args.phase_b_span_thresholds),
            "phase_b_flush_interval_ms": list(args.phase_b_flush_interval_ms_values),
            "phase_c_top_k": args.phase_c_top_k,
            "phase_c_trace_count": args.phase_c_trace_count,
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
            "controlled_case_args": sorted(CONTROLLED_CASE_ARGS),
        },
        commands={
            "argv": list(getattr(args, "cli_argv", [])),
            "case_args": list(args.case_args),
        },
        artifacts={
            "summary_json": output_summary,
            "requested_search_root": args.requested_search_root,
            "actual_search_root": args.actual_search_root,
        },
    )
    output_path = Path(output_summary)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(summary, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
    return summary


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    run_stage1_search(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
