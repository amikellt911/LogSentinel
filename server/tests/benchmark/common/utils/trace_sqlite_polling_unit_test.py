#!/usr/bin/env python3

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import trace_sqlite_polling as polling_module
except ModuleNotFoundError:
    polling_module = None


class TraceSqlitePollingUnitTest(unittest.TestCase):
    def test_wait_until_sqlite_stable_reaches_expected_trace_count_before_declaring_stable(self) -> None:
        if polling_module is None or not hasattr(polling_module, "wait_until_sqlite_stable"):
            self.fail("wait_until_sqlite_stable should exist for shared SQLite polling helper")

        counts = iter(
            [
                {"trace_summary": 12, "trace_span": 96},
                {"trace_summary": 12, "trace_span": 96},
                {"trace_summary": 15, "trace_span": 120},
                {"trace_summary": 15, "trace_span": 120},
                {"trace_summary": 15, "trace_span": 120},
            ]
        )
        ticks = iter([10.0, 10.05, 10.10, 10.15, 10.20, 10.25, 10.30])
        sleep_calls = []

        # 这里锁的是共享 helper 的核心语义：
        # 先追平 expected_trace_count，再谈稳定；否则 Suite A / Suite D 都会把中间态错判成最终完成。
        stable = polling_module.wait_until_sqlite_stable(
            sqlite_path=Path("/tmp/shared_suite.db"),
            sqlite_counter=lambda _path: next(counts),
            poll_interval_ms=50,
            stable_rounds=2,
            confirm_sleep_ms=0,
            max_wait_ms=5000,
            sleep_func=lambda seconds: sleep_calls.append(seconds),
            monotonic_func=lambda: next(ticks),
            start_ms=1000,
            expected_trace_count=15,
        )

        self.assertEqual({"trace_summary": 15, "trace_span": 120}, stable["final_counts"])
        self.assertEqual(10150, stable["t_stable_ms"])
        self.assertEqual(9150, stable["drain_tail_ms"])
        self.assertFalse(stable["drain_timeout"])
        self.assertEqual([0.05, 0.05], sleep_calls)

    def test_wait_until_sqlite_stable_returns_timeout_counts_when_never_catches_up(self) -> None:
        if polling_module is None or not hasattr(polling_module, "wait_until_sqlite_stable"):
            self.fail("wait_until_sqlite_stable should exist for shared SQLite polling helper")

        counts = iter(
            [
                {"trace_summary": 8, "trace_span": 64},
                {"trace_summary": 8, "trace_span": 64},
                {"trace_summary": 8, "trace_span": 64},
                {"trace_summary": 8, "trace_span": 64},
            ]
        )
        ticks = iter([10.0, 10.05, 10.10, 10.15, 10.20, 10.25])
        sleep_calls = []

        # timeout 分支也要回最后一次可见快照。
        # 否则 benchmark 只会得到“超时了”，却不知道卡在多少条 trace，后面根本没法分析尾巴。
        stable = polling_module.wait_until_sqlite_stable(
            sqlite_path=Path("/tmp/shared_suite.db"),
            sqlite_counter=lambda _path: next(counts),
            poll_interval_ms=50,
            stable_rounds=2,
            confirm_sleep_ms=0,
            max_wait_ms=120,
            sleep_func=lambda seconds: sleep_calls.append(seconds),
            monotonic_func=lambda: next(ticks),
            start_ms=1000,
            expected_trace_count=10,
        )

        self.assertEqual({"trace_summary": 8, "trace_span": 64}, stable["final_counts"])
        self.assertEqual(10200, stable["t_stable_ms"])
        self.assertEqual(9200, stable["drain_tail_ms"])
        self.assertTrue(stable["drain_timeout"])
        self.assertEqual([0.05, 0.05], sleep_calls)


if __name__ == "__main__":
    unittest.main()
