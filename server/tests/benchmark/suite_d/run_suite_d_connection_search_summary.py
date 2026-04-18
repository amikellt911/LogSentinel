#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional


JsonDict = Dict[str, Any]


def parse_connection_set(value: str) -> List[int]:
    connection_values: List[int] = []
    for item in value.split(","):
        stripped = item.strip()
        if stripped:
            connection_values.append(int(stripped))
    if not connection_values:
        raise ValueError("connection_set must contain at least one integer")
    return connection_values


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Suite D 24 核连接确认搜索汇总；同时输出 online 和 final completion 口径"
    )
    parser.add_argument("--search-root", required=True)
    parser.add_argument("--connection-set", required=True)
    parser.add_argument("--output-summary", default="")
    args = parser.parse_args(argv)
    try:
        args.connection_values = parse_connection_set(args.connection_set)
    except ValueError as exc:
        parser.error(str(exc))
    return args


def safe_ratio(numerator: float, denominator: float) -> float:
    if denominator <= 0:
        return 0.0
    return numerator / denominator


def load_case_metrics(case_path: Path) -> JsonDict:
    payload = json.loads(case_path.read_text(encoding="utf-8"))
    offered_traces = float(payload["wrk_metrics"]["offered_traces"])
    final_trace_summary = float(payload["sqlite_counts_final"]["trace_summary"])
    # online 反映的是压测窗口内已经落库了多少；
    # final 则是 case 完整收尾后真正留下来的多少。
    # Suite D 已经证明确实会出现“online 更高，但 final completion 明显更差”的点位，
    # 所以这里必须把两条口径都抽出来，后续 summary/best 才不会继续误判 winner。
    return {
        "online": float(payload["online_completed_traces_per_sec"]),
        "ratio": float(payload["online_completion_ratio"]),
        "qps": float(payload["wrk_metrics"]["requests_per_sec"]),
        "offered_traces": offered_traces,
        "final_trace_summary": final_trace_summary,
        "final_completion_ratio": safe_ratio(final_trace_summary, offered_traces),
        "drain_tail_ms": int(payload["drain_tail_ms"]),
    }


def build_connection_search_summary(search_root: Path, connection_values: Iterable[int]) -> JsonDict:
    normalized_root = Path(search_root)
    grouped: Dict[int, List[JsonDict]] = {int(value): [] for value in connection_values}
    for value in grouped:
        pattern = f"r*_c{value:03d}.json"
        for path in sorted(normalized_root.glob(pattern)):
            grouped[value].append(load_case_metrics(path))

    summary: JsonDict = {
        "search_root": str(normalized_root),
        "connection_set": list(grouped.keys()),
        "by_connections": [],
    }

    for value in summary["connection_set"]:
        records = grouped[int(value)]
        if not records:
            continue
        online_values = [item["online"] for item in records]
        ratio_values = [item["ratio"] for item in records]
        qps_values = [item["qps"] for item in records]
        offered_values = [item["offered_traces"] for item in records]
        final_trace_values = [item["final_trace_summary"] for item in records]
        final_ratio_values = [item["final_completion_ratio"] for item in records]
        drain_values = [item["drain_tail_ms"] for item in records]
        item = {
            "connections": int(value),
            "runs": len(records),
            "median_online_completed_traces_per_sec": statistics.median(online_values),
            "median_online_completion_ratio": statistics.median(ratio_values),
            "median_requests_per_sec": statistics.median(qps_values),
            "median_offered_traces": statistics.median(offered_values),
            "median_final_trace_summary": statistics.median(final_trace_values),
            "median_final_completion_ratio": statistics.median(final_ratio_values),
            "median_drain_tail_ms": int(statistics.median(drain_values)),
            "max_online_completed_traces_per_sec": max(online_values),
            "min_online_completed_traces_per_sec": min(online_values),
        }
        summary["by_connections"].append(item)

    if not summary["by_connections"]:
        raise ValueError(f"no Suite D connection search results found under {normalized_root}")

    summary["best"] = max(summary["by_connections"], key=connection_summary_sort_key)
    return summary


def connection_summary_sort_key(item: JsonDict) -> tuple[float, float, float, float, int, int]:
    # 连接确认搜索的目标不再只是“窗口内看起来完成得快”。
    # 既然 final 口径已经修正成停服后再等 SQLite 稳定，那么 winner 必须先看最终完成比例，
    # 否则就会继续把“online 高一点、但 final 大量掉单”的配置错选成最优。
    return (
        float(item["median_final_completion_ratio"]),
        float(item["median_final_trace_summary"]),
        float(item["median_online_completed_traces_per_sec"]),
        float(item["median_online_completion_ratio"]),
        -int(item["median_drain_tail_ms"]),
        -int(item["connections"]),
    )


def print_connection_search_summary(summary: JsonDict, line_writer=print) -> None:
    for item in summary["by_connections"]:
        line_writer(
            "[summary] "
            f"connections={item['connections']} "
            f"median_online={item['median_online_completed_traces_per_sec']:.2f} "
            f"median_ratio={item['median_online_completion_ratio']:.4f} "
            f"median_final={item['median_final_trace_summary']:.2f} "
            f"median_final_ratio={item['median_final_completion_ratio']:.4f} "
            f"median_drain={item['median_drain_tail_ms']} "
            f"median_qps={item['median_requests_per_sec']:.2f} "
            f"runs={item['runs']}"
        )

    best = summary["best"]
    line_writer(
        "[best] "
        f"connections={best['connections']} "
        f"median_final={best['median_final_trace_summary']:.2f} "
        f"median_final_ratio={best['median_final_completion_ratio']:.4f} "
        f"median_online={best['median_online_completed_traces_per_sec']:.2f} "
        f"median_ratio={best['median_online_completion_ratio']:.4f} "
        f"median_drain={best['median_drain_tail_ms']}"
    )


def write_summary(output_path: Path, summary: JsonDict) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(summary, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    search_root = Path(args.search_root)
    summary = build_connection_search_summary(search_root, args.connection_values)
    print_connection_search_summary(summary)
    output_path = Path(args.output_summary) if args.output_summary else search_root / "summary.json"
    write_summary(output_path, summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
