#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import sqlite3
import time
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Set, Tuple


JsonDict = Dict[str, Any]


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Suite B 生命周期鲁棒性 evaluator")
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--sqlite-db", required=True)
    parser.add_argument("--output-json", default="")
    parser.add_argument("--poll-interval-sec", type=float, default=0.2)
    parser.add_argument("--stable-rounds", type=int, default=5)
    parser.add_argument("--confirm-sleep-sec", type=float, default=0.3)
    parser.add_argument("--max-wait-sec", type=float, default=30.0)
    return parser.parse_args(argv)


def normalize_trace_id(value: Any) -> str:
    return str(value)


def normalize_span_id(value: Any) -> str:
    return str(value)


def load_manifest_rows(path: Path) -> List[JsonDict]:
    rows: List[JsonDict] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        rows.append(json.loads(line))
    return rows


def build_expected_views(rows: List[JsonDict]) -> JsonDict:
    all_trace_ids: Set[str] = set()
    expected_merge_sets: Dict[str, Set[str]] = {}
    expected_ignore_events: Dict[str, List[JsonDict]] = {}
    expected_replay_events: Dict[str, List[JsonDict]] = {}

    for row in rows:
        trace_id = normalize_trace_id(row["logical_trace_id"])
        span_id = normalize_span_id(row["span_id"])

        normalized = dict(row)
        normalized["logical_trace_id"] = trace_id
        normalized["span_id"] = span_id
        if normalized.get("parent_span_id") is not None:
            normalized["parent_span_id"] = normalize_span_id(normalized["parent_span_id"])

        all_trace_ids.add(trace_id)
        expected_ignore_events.setdefault(trace_id, [])
        expected_replay_events.setdefault(trace_id, [])
        expected_merge_sets.setdefault(trace_id, set())

        # manifest 存的是“发送事件”，不是“最终 trace”。
        # 所以 evaluator 先把事件层折叠成两种视图：
        # 1) merge_set：最终本该保住哪些 span_id，用集合去重；
        # 2) ignore/replay 列表：本来应该被忽略的脏事件，保留事件级颗粒度，后面算 duplicate/misclassification 还要按事件数统计。
        if normalized.get("expected_final_action") == "merge_into_final_trace":
            expected_merge_sets[trace_id].add(span_id)
        if normalized.get("expected_final_action") == "ignore_after_cutoff":
            expected_ignore_events[trace_id].append(normalized)
        if normalized.get("event_kind") == "replay_clone":
            expected_replay_events[trace_id].append(normalized)

    for trace_id in all_trace_ids:
        expected_merge_sets.setdefault(trace_id, set())
        expected_ignore_events.setdefault(trace_id, [])
        expected_replay_events.setdefault(trace_id, [])

    return {
        "expected_merge_sets": expected_merge_sets,
        "expected_ignore_events": expected_ignore_events,
        "expected_replay_events": expected_replay_events,
    }


def query_global_counts(conn: sqlite3.Connection) -> Tuple[int, int]:
    summary_count = int(conn.execute("SELECT COUNT(*) FROM trace_summary").fetchone()[0])
    span_count = int(conn.execute("SELECT COUNT(*) FROM trace_span").fetchone()[0])
    return (summary_count, span_count)


def wait_until_counts_stable(
    query_counts: Callable[[], Tuple[int, int]],
    poll_interval_sec: float = 0.2,
    stable_rounds: int = 5,
    confirm_sleep_sec: float = 0.3,
    max_wait_sec: float = 30.0,
    sleep_func: Callable[[float], None] = time.sleep,
) -> Tuple[int, int]:
    if stable_rounds <= 0:
        raise ValueError("stable_rounds must be > 0")
    if max_wait_sec <= 0:
        raise ValueError("max_wait_sec must be > 0")

    deadline = time.monotonic() + max_wait_sec
    last_counts: Optional[Tuple[int, int]] = None
    same_rounds = 0

    while True:
        counts = query_counts()
        if counts == last_counts:
            same_rounds += 1
        else:
            last_counts = counts
            same_rounds = 0

        # 这里不直接用固定 sleep，因为 sender 发完以后，后端还可能在 dispatch / flush / SQLite commit。
        # evaluator 真正想等的是“结果已经排干并稳定”，而不是拍脑袋等 2 秒。
        if same_rounds >= stable_rounds:
            if confirm_sleep_sec > 0:
                sleep_func(confirm_sleep_sec)
            confirm_counts = query_counts()
            if confirm_counts == counts:
                return counts
            last_counts = confirm_counts
            same_rounds = 0

        if time.monotonic() >= deadline:
            raise RuntimeError("sqlite counts did not become stable before timeout")

        if poll_interval_sec > 0:
            sleep_func(poll_interval_sec)


def wait_until_sqlite_stable(
    conn: sqlite3.Connection,
    poll_interval_sec: float = 0.2,
    stable_rounds: int = 5,
    confirm_sleep_sec: float = 0.3,
    max_wait_sec: float = 30.0,
) -> Tuple[int, int]:
    return wait_until_counts_stable(
        query_counts=lambda: query_global_counts(conn),
        poll_interval_sec=poll_interval_sec,
        stable_rounds=stable_rounds,
        confirm_sleep_sec=confirm_sleep_sec,
        max_wait_sec=max_wait_sec,
    )


def load_sqlite_snapshot(conn: sqlite3.Connection) -> JsonDict:
    persisted_summary_span_count: Dict[str, int] = {}
    persisted_span_sets: Dict[str, Set[str]] = {}

    for trace_id, span_count in conn.execute("SELECT trace_id, span_count FROM trace_summary"):
        persisted_summary_span_count[normalize_trace_id(trace_id)] = int(span_count)

    for trace_id, span_id in conn.execute("SELECT trace_id, span_id FROM trace_span ORDER BY trace_id ASC, span_id ASC"):
        trace_key = normalize_trace_id(trace_id)
        persisted_span_sets.setdefault(trace_key, set()).add(normalize_span_id(span_id))

    return {
        "persisted_summary_span_count": persisted_summary_span_count,
        "persisted_span_sets": persisted_span_sets,
    }


def build_rate_result(matched: int, total: int, matched_label: str, total_label: str) -> JsonDict:
    return {
        matched_label: matched,
        total_label: total,
        "value": (matched / total) if total > 0 else 0.0,
    }


def calc_trace_completeness_rate(expected_merge_sets: Dict[str, Set[str]], persisted_span_sets: Dict[str, Set[str]]) -> JsonDict:
    total_traces = len(expected_merge_sets)
    matched_traces = 0

    for trace_id, expected_set in expected_merge_sets.items():
        actual_set = persisted_span_sets.get(trace_id, set())
        if expected_set.issubset(actual_set):
            matched_traces += 1

    return build_rate_result(matched_traces, total_traces, "matched_traces", "total_traces")


def calc_trace_pollution_rate(expected_merge_sets: Dict[str, Set[str]], persisted_span_sets: Dict[str, Set[str]]) -> JsonDict:
    total_traces = len(expected_merge_sets)
    matched_traces = 0

    for trace_id, expected_set in expected_merge_sets.items():
        actual_set = persisted_span_sets.get(trace_id, set())
        if actual_set - expected_set:
            matched_traces += 1

    return build_rate_result(matched_traces, total_traces, "matched_traces", "total_traces")


def calc_duplicate_persistence_rate(
    expected_merge_sets: Dict[str, Set[str]],
    expected_replay_events: Dict[str, List[JsonDict]],
    persisted_span_sets: Dict[str, Set[str]],
    persisted_summary_span_count: Dict[str, int],
) -> JsonDict:
    total_events = 0
    matched_events = 0

    for trace_id, replay_rows in expected_replay_events.items():
        expected_set = expected_merge_sets.get(trace_id, set())
        actual_set = persisted_span_sets.get(trace_id, set())
        summary_count = persisted_summary_span_count.get(trace_id, 0)

        # duplicate 第一刀先走“trace 级副作用”归因：
        # 只要 replay 所在 trace 因额外 span 或 summary_count 膨胀而明显偏离期望，就把这条 replay 事件记成 bad。
        # 它还不是最细粒度的单事件因果判定，但已经足够支撑第一版论文主指标打通。
        bad_side_effect = bool(actual_set - expected_set) or summary_count > len(expected_set)
        for _ in replay_rows:
            total_events += 1
            if bad_side_effect:
                matched_events += 1

    return build_rate_result(matched_events, total_events, "matched_events", "total_events")


def evaluate_suite_b(
    manifest_path: Path,
    sqlite_path: Path,
    poll_interval_sec: float = 0.2,
    stable_rounds: int = 5,
    confirm_sleep_sec: float = 0.3,
    max_wait_sec: float = 30.0,
) -> JsonDict:
    manifest_rows = load_manifest_rows(manifest_path)
    expected_views = build_expected_views(manifest_rows)

    conn = sqlite3.connect(sqlite_path)
    try:
        sqlite_final_counts = wait_until_sqlite_stable(
            conn,
            poll_interval_sec=poll_interval_sec,
            stable_rounds=stable_rounds,
            confirm_sleep_sec=confirm_sleep_sec,
            max_wait_sec=max_wait_sec,
        )
        snapshot = load_sqlite_snapshot(conn)
    finally:
        conn.close()

    result = {
        "sqlite_final_counts": {
            "trace_summary": sqlite_final_counts[0],
            "trace_span": sqlite_final_counts[1],
        },
        "trace_completeness_rate": calc_trace_completeness_rate(
            expected_views["expected_merge_sets"],
            snapshot["persisted_span_sets"],
        ),
        "trace_pollution_rate": calc_trace_pollution_rate(
            expected_views["expected_merge_sets"],
            snapshot["persisted_span_sets"],
        ),
        "duplicate_persistence_rate": calc_duplicate_persistence_rate(
            expected_views["expected_merge_sets"],
            expected_views["expected_replay_events"],
            snapshot["persisted_span_sets"],
            snapshot["persisted_summary_span_count"],
        ),
        "expected_trace_count": len(expected_views["expected_merge_sets"]),
        "expected_ignore_event_count": sum(len(rows) for rows in expected_views["expected_ignore_events"].values()),
        "expected_replay_event_count": sum(len(rows) for rows in expected_views["expected_replay_events"].values()),
    }
    return result


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    result = evaluate_suite_b(
        manifest_path=Path(args.manifest),
        sqlite_path=Path(args.sqlite_db),
        poll_interval_sec=args.poll_interval_sec,
        stable_rounds=args.stable_rounds,
        confirm_sleep_sec=args.confirm_sleep_sec,
        max_wait_sec=args.max_wait_sec,
    )

    output = json.dumps(result, ensure_ascii=True, indent=2)
    print(output)

    if args.output_json:
        output_path = Path(args.output_json)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(output + "\n", encoding="utf-8")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
