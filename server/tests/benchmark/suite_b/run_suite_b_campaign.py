#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Callable, Dict, List, Optional

CURRENT_DIR = Path(__file__).resolve().parent
COMMON_UTILS_DIR = CURRENT_DIR.parent / "common" / "utils"
for candidate_dir in (CURRENT_DIR, COMMON_UTILS_DIR):
    if str(candidate_dir) not in sys.path:
        sys.path.insert(0, str(candidate_dir))

import run_suite_b_matrix
from benchmark_metadata import attach_benchmark_metadata, build_cpu_allocation


DEFAULT_SEEDS = "20260415,20260416,20260417,20260418,20260419"
CONTROLLED_MATRIX_ARGS = {"--seed", "--run-root", "--output-summary"}
CORRECTNESS_METRICS = [
    "trace_completeness_rate",
    "trace_pollution_rate",
    "duplicate_persistence_rate",
]


def default_campaign_root() -> str:
    return "server/tests/benchmark/results/suite_b/campaign"


def parse_seed_list(value: str) -> List[int]:
    seeds: List[int] = []
    for item in value.split(","):
        item = item.strip()
        if item:
            seeds.append(int(item))
    if not seeds:
        raise ValueError("--seeds must contain at least one integer seed")
    return seeds


def reject_controlled_matrix_args(matrix_args: List[str]) -> None:
    for token in matrix_args:
        option = token.split("=", 1)[0]
        if option in CONTROLLED_MATRIX_ARGS:
            raise ValueError(
                f"{option} is controlled by run_suite_b_campaign.py; "
                "use campaign-level options instead"
            )


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Suite B 正式复跑 campaign runner；负责多 seed 矩阵批跑和 run-level 聚合"
    )
    parser.add_argument("--campaign-root", default=default_campaign_root())
    parser.add_argument("--output-summary", default="")
    parser.add_argument("--seeds", default=DEFAULT_SEEDS)
    parser.add_argument("--port-base", type=int, default=19080)
    parser.add_argument("--port-stride", type=int, default=20)
    args, matrix_args = parser.parse_known_args(argv)
    if matrix_args and matrix_args[0] == "--":
        matrix_args = matrix_args[1:]
    reject_controlled_matrix_args(matrix_args)
    args.seed_values = parse_seed_list(args.seeds)
    args.matrix_args = matrix_args
    args.cli_argv = list(argv) if argv is not None else list(sys.argv[1:])
    return args


def extract_optional_matrix_arg(matrix_args: List[str], option: str) -> Optional[str]:
    for index, token in enumerate(matrix_args):
        token_option = token.split("=", 1)[0]
        if token_option != option:
            continue
        if "=" in token:
            return token.split("=", 1)[1]
        if index + 1 >= len(matrix_args):
            return None
        return matrix_args[index + 1]
    return None


def resolve_campaign_root(campaign_root_prefix: str) -> tuple[str, str]:
    requested_campaign_root = str(Path(campaign_root_prefix))
    actual_campaign_root = f"{requested_campaign_root}-{run_suite_b_matrix.format_run_timestamp()}"
    return requested_campaign_root, actual_campaign_root


def ensure_campaign_root_resolved(args: argparse.Namespace) -> None:
    if hasattr(args, "actual_campaign_root"):
        args.requested_campaign_root = getattr(args, "requested_campaign_root", args.campaign_root)
        return

    # campaign-root 和 matrix run-root 都做成“前缀 + 时间后缀”。
    # 这样正式 5 seed 复跑时，每一轮矩阵和总摘要都有独立目录，不会复用旧 SQLite 或旧 manifest。
    requested_campaign_root, actual_campaign_root = resolve_campaign_root(args.campaign_root)
    args.requested_campaign_root = requested_campaign_root
    args.actual_campaign_root = actual_campaign_root


def build_matrix_argv(args: argparse.Namespace, seed: int, run_index: int) -> List[str]:
    run_root = Path(args.actual_campaign_root) / f"run_{run_index + 1:02d}_seed_{seed}"
    port_base = args.port_base + run_index * args.port_stride

    # seed / run-root / port-base 是 campaign 层的控制变量。
    # 其它后端资源参数继续原样透传给 matrix runner，避免重复维护两份 CLI 定义。
    return list(args.matrix_args) + [
        "--seed",
        str(seed),
        "--run-root",
        str(run_root),
        "--port-base",
        str(port_base),
    ]


def run_matrix_once(matrix_argv: List[str]) -> Dict[str, object]:
    matrix_args = run_suite_b_matrix.parse_args(matrix_argv)
    return run_suite_b_matrix.run_suite_b_matrix(matrix_args)


def summarize_numeric_values(values: List[float]) -> Dict[str, object]:
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


def collect_correctness_values(matrix_summaries: List[Dict[str, object]]) -> Dict[str, Dict[str, List[float]]]:
    grouped: Dict[str, Dict[str, List[float]]] = {}
    for summary in matrix_summaries:
        for case in summary.get("cases", []):
            if not isinstance(case, dict) or "case_id" not in case:
                continue
            case_id = str(case["case_id"])
            for metric in CORRECTNESS_METRICS:
                metric_obj = case.get(metric)
                if not isinstance(metric_obj, dict):
                    continue
                value = metric_obj.get("value")
                if isinstance(value, (int, float)):
                    grouped.setdefault(case_id, {}).setdefault(metric, []).append(float(value))
    return grouped


def build_correctness_aggregate(matrix_summaries: List[Dict[str, object]]) -> Dict[str, Dict[str, Dict[str, object]]]:
    grouped = collect_correctness_values(matrix_summaries)
    return {
        case_id: {
            metric: summarize_numeric_values(values)
            for metric, values in metric_values.items()
        }
        for case_id, metric_values in grouped.items()
    }


def build_ingest_p95_delta_aggregate(matrix_summaries: List[Dict[str, object]]) -> Dict[str, Dict[str, Dict[str, object]]]:
    grouped: Dict[str, Dict[str, List[float]]] = {}
    for summary in matrix_summaries:
        delta_by_profile = summary.get("ingest_p95_latency_delta_by_profile", {})
        if not isinstance(delta_by_profile, dict):
            continue
        for profile, delta in delta_by_profile.items():
            if not isinstance(delta, dict):
                continue
            for field in ("absolute_ms", "relative"):
                value = delta.get(field)
                if isinstance(value, (int, float)):
                    grouped.setdefault(str(profile), {}).setdefault(field, []).append(float(value))

    # p95 护栏不能把所有请求混成一锅重新算。
    # 这里先尊重每一轮 matrix 自己的 p95，再对 run-level p95 delta 做 median/min/max，
    # 这样更能反映正式实验的稳定性，而不是被某一次偶发抖动单点带偏。
    return {
        profile: {
            field: summarize_numeric_values(values)
            for field, values in field_values.items()
        }
        for profile, field_values in grouped.items()
    }


def build_sqlite_unique_constraint_aggregate(matrix_summaries: List[Dict[str, object]]) -> Dict[str, Dict[str, object]]:
    grouped: Dict[str, List[float]] = {}
    for summary in matrix_summaries:
        counts = summary.get("sqlite_unique_constraint_fail_count_by_case", {})
        if not isinstance(counts, dict):
            continue
        for case_id, count in counts.items():
            if isinstance(count, (int, float)):
                grouped.setdefault(str(case_id), []).append(float(count))

    # UNIQUE 冲突次数只表达“后端尝试过重复 summary 写入”，不是最终重复落库条数。
    # campaign 层保留它的 run-level 统计，主要服务结果解释和日志诊断。
    return {
        case_id: summarize_numeric_values(values)
        for case_id, values in grouped.items()
    }


def build_campaign_aggregate(matrix_summaries: List[Dict[str, object]]) -> Dict[str, object]:
    return {
        "correctness_by_case": build_correctness_aggregate(matrix_summaries),
        "ingest_p95_latency_delta_by_profile": build_ingest_p95_delta_aggregate(matrix_summaries),
        "sqlite_unique_constraint_fail_count_by_case": build_sqlite_unique_constraint_aggregate(matrix_summaries),
    }


def run_suite_b_campaign(
    args: argparse.Namespace,
    matrix_runner: Callable[[List[str]], Dict[str, object]] = run_matrix_once,
) -> Dict[str, object]:
    ensure_campaign_root_resolved(args)
    actual_root = Path(args.actual_campaign_root)
    actual_root.mkdir(parents=True, exist_ok=True)

    runs: List[Dict[str, object]] = []
    matrix_summaries: List[Dict[str, object]] = []
    for run_index, seed in enumerate(args.seed_values):
        matrix_argv = build_matrix_argv(args, seed, run_index)
        matrix_summary = matrix_runner(matrix_argv)
        matrix_summaries.append(matrix_summary)
        runs.append(
            {
                "run_index": run_index + 1,
                "seed": seed,
                "port_base": args.port_base + run_index * args.port_stride,
                "matrix_argv": matrix_argv,
                "requested_run_root": matrix_summary.get("requested_run_root"),
                "actual_run_root": matrix_summary.get("actual_run_root"),
                "total_cases": matrix_summary.get("total_cases"),
            }
        )

    summary = {
        "total_runs": len(runs),
        "requested_campaign_root": args.requested_campaign_root,
        "actual_campaign_root": args.actual_campaign_root,
        "seeds": args.seed_values,
        "port_base": args.port_base,
        "port_stride": args.port_stride,
        "matrix_args": args.matrix_args,
        "runs": runs,
        "aggregate": build_campaign_aggregate(matrix_summaries),
    }

    output_summary = args.output_summary or str(actual_root / "summary.json")
    summary = attach_benchmark_metadata(
        payload=summary,
        suite_name="suite_b",
        entry_script=__file__,
        workload={
            "seeds": list(args.seed_values),
            "port_base": args.port_base,
            "port_stride": args.port_stride,
        },
        cpu_allocation=build_cpu_allocation(
            server_cpuset=extract_optional_matrix_arg(args.matrix_args, "--server-cpuset"),
        ),
        thread_topology={
            "server_io_threads": extract_optional_matrix_arg(args.matrix_args, "--server-io-threads"),
            "worker_threads": extract_optional_matrix_arg(args.matrix_args, "--worker-threads"),
            "dispatch_worker_threads": extract_optional_matrix_arg(args.matrix_args, "--dispatch-worker-threads"),
        },
        effective_flags={
            "controlled_matrix_args": sorted(CONTROLLED_MATRIX_ARGS),
        },
        commands={
            "argv": list(getattr(args, "cli_argv", [])),
            "matrix_args": list(args.matrix_args),
        },
        artifacts={
            "summary_json": output_summary,
            "requested_campaign_root": args.requested_campaign_root,
            "actual_campaign_root": args.actual_campaign_root,
        },
    )
    output_path = Path(output_summary)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(summary, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
    return summary


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    summary = run_suite_b_campaign(args)
    print(json.dumps(summary, ensure_ascii=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
