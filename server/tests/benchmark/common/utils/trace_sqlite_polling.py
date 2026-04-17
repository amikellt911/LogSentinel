#!/usr/bin/env python3

from __future__ import annotations

import sqlite3
import time
from pathlib import Path
from typing import Any, Callable, Dict, Optional


JsonDict = Dict[str, Any]


def read_sqlite_counts(sqlite_path: Path) -> JsonDict:
    sqlite_uri = f"file:{sqlite_path}?mode=ro"
    # 这里只做只读短查询。
    # benchmark 轮询如果自己拿长事务，会反过来拖慢 WAL 侧的真实排空，测出来的尾巴就不干净了。
    conn = sqlite3.connect(sqlite_uri, uri=True, timeout=5.0)
    try:
        conn.execute("PRAGMA busy_timeout = 5000")
        trace_summary = int(conn.execute("SELECT COUNT(*) FROM trace_summary").fetchone()[0])
        trace_span = int(conn.execute("SELECT COUNT(*) FROM trace_span").fetchone()[0])
        return {
            "trace_summary": trace_summary,
            "trace_span": trace_span,
        }
    finally:
        conn.close()


def wait_until_sqlite_stable(
    sqlite_path: Path,
    sqlite_counter: Callable[[Path], JsonDict] = read_sqlite_counts,
    poll_interval_ms: int = 200,
    stable_rounds: int = 5,
    confirm_sleep_ms: int = 300,
    max_wait_ms: int = 30000,
    sleep_func: Callable[[float], None] = time.sleep,
    monotonic_func: Callable[[], float] = time.monotonic,
    start_ms: Optional[int] = None,
    expected_trace_count: Optional[int] = None,
) -> JsonDict:
    if stable_rounds <= 0:
        raise ValueError("stable_rounds must be > 0")
    if max_wait_ms <= 0:
        raise ValueError("max_wait_ms must be > 0")

    base_ms = start_ms if start_ms is not None else int(monotonic_func() * 1000)
    deadline = monotonic_func() + (max_wait_ms / 1000.0)
    last_counts: Optional[JsonDict] = None
    same_rounds = 0

    while True:
        counts = sqlite_counter(sqlite_path)

        # 一旦调用方知道“理论上应该补齐多少条 trace”，就必须先追这个目标值。
        # 否则中间态恰好连续稳定几轮，也会被误判成 final，后面的 drain_tail_ms 就完全失真了。
        if expected_trace_count is not None and int(counts["trace_summary"]) >= expected_trace_count:
            t_stable_ms = int(monotonic_func() * 1000)
            return {
                "final_counts": counts,
                "t_stable_ms": t_stable_ms,
                "drain_tail_ms": max(0, t_stable_ms - base_ms),
                "drain_timeout": False,
            }

        if expected_trace_count is None:
            if counts == last_counts:
                same_rounds += 1
            else:
                last_counts = counts
                same_rounds = 0

            if same_rounds >= stable_rounds:
                if confirm_sleep_ms > 0:
                    sleep_func(confirm_sleep_ms / 1000.0)
                    confirm_counts = sqlite_counter(sqlite_path)
                    if confirm_counts == counts:
                        t_stable_ms = int(monotonic_func() * 1000)
                        return {
                            "final_counts": confirm_counts,
                            "t_stable_ms": t_stable_ms,
                            "drain_tail_ms": max(0, t_stable_ms - base_ms),
                            "drain_timeout": False,
                        }
                    last_counts = confirm_counts
                    same_rounds = 0
                else:
                    t_stable_ms = int(monotonic_func() * 1000)
                    return {
                        "final_counts": counts,
                        "t_stable_ms": t_stable_ms,
                        "drain_tail_ms": max(0, t_stable_ms - base_ms),
                        "drain_timeout": False,
                    }

        if monotonic_func() >= deadline:
            timeout_counts = sqlite_counter(sqlite_path)
            t_stable_ms = int(monotonic_func() * 1000)
            return {
                "final_counts": timeout_counts,
                "t_stable_ms": t_stable_ms,
                "drain_tail_ms": max(0, t_stable_ms - base_ms),
                "drain_timeout": True,
            }

        sleep_func(poll_interval_ms / 1000.0)
