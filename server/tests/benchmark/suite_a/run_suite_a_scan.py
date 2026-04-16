#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Callable, Dict, List, Optional

import run_suite_a_case


JsonDict = Dict[str, object]
DEFAULT_GAPS_MS = "25,20,15"
CONTROLLED_CASE_ARGS = {
    "--inter-trace-gap-ms",
    "--run-root",
    "--output-json",
    "--port-base",
    "--url",
    "--sqlite-db",
    "--server-log",
}


def default_scan_root() -> str:
    return "server/tests/benchmark/results/suite_a/scan"


def parse_gap_list(value: str) -> List[int]:
    gaps: List[int] = []
    for item in value.split(","):
        item = item.strip()
        if item:
            gaps.append(int(item))
    if not gaps:
        raise ValueError("--gaps-ms must contain at least one integer gap")
    return gaps


def reject_controlled_case_args(case_args: List[str]) -> None:
    for token in case_args:
        option = token.split("=", 1)[0]
        if option in CONTROLLED_CASE_ARGS:
            raise ValueError(
                f"{option} is controlled by run_suite_a_scan.py; "
                "use scan-level options instead"
            )


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Suite A gap 扫描 runner；负责多 gap 多轮单 case 编排和聚合"
    )
    parser.add_argument("--scan-root", default=default_scan_root())
    parser.add_argument("--output-summary", default="")
    parser.add_argument("--gaps-ms", default=DEFAULT_GAPS_MS)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--port-base", type=int, default=18180)
    parser.add_argument("--port-stride", type=int, default=1)
    args, case_args = parser.parse_known_args(argv)
    if case_args and case_args[0] == "--":
        case_args = case_args[1:]
    reject_controlled_case_args(case_args)
    args.gap_values = parse_gap_list(args.gaps_ms)
    args.case_args = case_args
    if args.repeats <= 0:
        raise ValueError("--repeats must be > 0")
    if args.port_stride <= 0:
        raise ValueError("--port-stride must be > 0")
    return args


def resolve_scan_root(scan_root_prefix: str) -> tuple[str, str]:
    requested_scan_root = str(Path(scan_root_prefix))
    actual_scan_root = f"{requested_scan_root}-{run_suite_a_case.format_run_timestamp()}"
    return requested_scan_root, actual_scan_root


def ensure_scan_root_resolved(args: argparse.Namespace) -> None:
    if hasattr(args, "actual_scan_root"):
        args.requested_scan_root = getattr(args, "requested_scan_root", args.scan_root)
        return

    # scan-root 也按“前缀 + 时间后缀”处理。
    # 这样同一组 gap 扫描可以重复跑，不会把不同轮次的 result.json 和 SQLite 混在一起。
    requested_scan_root, actual_scan_root = resolve_scan_root(args.scan_root)
    args.requested_scan_root = requested_scan_root
    args.actual_scan_root = actual_scan_root


def build_case_argv(
    args: argparse.Namespace,
    gap_ms: int,
    repeat_index: int,
    global_run_index: int,
) -> List[str]:
    run_root = Path(args.actual_scan_root) / f"gap_{gap_ms:03d}ms" / f"run_{repeat_index + 1:02d}"
    output_json = run_root / "result.json"
    port_base = args.port_base + global_run_index * args.port_stride

    # scan runner 只固定三类实验变量：
    # 1) 当前这轮的 gap；
    # 2) 当前这轮独立的 run-root/output-json；
    # 3) 当前这轮独立的端口。
    # 其它后端/发送参数继续原样交给 run_suite_a_case，避免重复维护两套 CLI。
    return list(args.case_args) + [
        "--inter-trace-gap-ms",
        str(gap_ms),
        "--run-root",
        str(run_root),
        "--output-json",
        str(output_json),
        "--port-base",
        str(port_base),
    ]


def run_case_once(case_argv: List[str]) -> JsonDict:
    case_args = run_suite_a_case.parse_args(case_argv)
    return run_suite_a_case.run_suite_a_case(case_args)


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


def summarize_bool_values(values: List[bool]) -> Dict[str, object]:
    count = len(values)
    true_count = sum(1 for value in values if value)
    false_count = count - true_count
    return {
        "count": count,
        "true_count": true_count,
        "false_count": false_count,
        "timeout_rate": (true_count / count) if count > 0 else None,
    }


def build_gap_aggregate(results: List[JsonDict]) -> Dict[str, Dict[str, object]]:
    grouped: Dict[str, List[JsonDict]] = {}
    for result in results:
        gap_key = str(int(result["inter_trace_gap_ms"]))
        grouped.setdefault(gap_key, []).append(result)

    aggregate: Dict[str, Dict[str, object]] = {}
    for gap_key, gap_results in grouped.items():
        aggregate[gap_key] = {
            "visible_completion_rate_at_stop": summarize_numeric_values(
                [float(result["visible_completion_rate_at_stop"]) for result in gap_results]
            ),
            "drain_tail_ms": summarize_numeric_values(
                [float(result["drain_tail_ms"]) for result in gap_results]
            ),
            "drain_timeout": summarize_bool_values(
                [bool(result["drain_timeout"]) for result in gap_results]
            ),
        }
    return aggregate


def run_suite_a_scan(
    args: argparse.Namespace,
    case_runner: Callable[[List[str]], JsonDict] = run_case_once,
) -> Dict[str, object]:
    ensure_scan_root_resolved(args)
    actual_root = Path(args.actual_scan_root)
    actual_root.mkdir(parents=True, exist_ok=True)

    runs: List[Dict[str, object]] = []
    raw_results: List[JsonDict] = []
    global_run_index = 0
    for gap_ms in args.gap_values:
        for repeat_index in range(args.repeats):
            case_argv = build_case_argv(args, gap_ms, repeat_index, global_run_index)
            # run_suite_a_case 会自己创建“加时间后缀后的真实 run-root”，
            # 但如果这里显式指定了嵌套 output-json，父目录还是要由 scan runner 先补齐。
            # 否则单 case 结束写 result.json 时，会因为 gap/run_xx 这层前缀目录还不存在而直接失败。
            output_json = Path(case_argv[case_argv.index("--output-json") + 1])
            output_json.parent.mkdir(parents=True, exist_ok=True)
            result = case_runner(case_argv)
            # 单 case runner 正常会返回 inter_trace_gap_ms。
            # 这里再补一层兜底，是为了让 scan 聚合永远认当前编排出来的 gap 真值，
            # 而不是把分组正确性寄托在外部 fake runner 或旧结果字段一定完整。
            result["inter_trace_gap_ms"] = int(result.get("inter_trace_gap_ms", gap_ms))
            raw_results.append(result)
            runs.append(
                {
                    "run_index": global_run_index + 1,
                    "gap_ms": gap_ms,
                    "repeat_index": repeat_index + 1,
                    "port_base": args.port_base + global_run_index * args.port_stride,
                    "case_argv": case_argv,
                    "requested_run_root": result.get("requested_run_root"),
                    "actual_run_root": result.get("actual_run_root"),
                    "visible_completion_rate_at_stop": result.get("visible_completion_rate_at_stop"),
                    "drain_tail_ms": result.get("drain_tail_ms"),
                    "drain_timeout": result.get("drain_timeout"),
                }
            )
            global_run_index += 1

    summary = {
        "total_runs": len(runs),
        "requested_scan_root": args.requested_scan_root,
        "actual_scan_root": args.actual_scan_root,
        "gaps_ms": args.gap_values,
        "repeats": args.repeats,
        "port_base": args.port_base,
        "port_stride": args.port_stride,
        "case_args": args.case_args,
        "runs": runs,
        "aggregate": {
            "by_gap": build_gap_aggregate(raw_results),
        },
    }

    output_summary = args.output_summary or str(actual_root / "summary.json")
    output_path = Path(output_summary)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(summary, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
    return summary


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    summary = run_suite_a_scan(args)
    print(json.dumps(summary, ensure_ascii=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
