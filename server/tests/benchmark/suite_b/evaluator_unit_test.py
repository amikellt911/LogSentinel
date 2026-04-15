#!/usr/bin/env python3

import json
import sqlite3
import tempfile
import unittest
from pathlib import Path

try:
    import evaluator as evaluator_module
except ModuleNotFoundError:
    evaluator_module = None


class SuiteBEvaluatorUnitTest(unittest.TestCase):
    def test_build_expected_views_extracts_merge_ignore_and_replay(self) -> None:
        if evaluator_module is None or not hasattr(evaluator_module, "build_expected_views"):
            self.fail("build_expected_views should exist for Suite B evaluator")

        rows = [
            {"logical_trace_id": 1001, "span_id": 1, "event_kind": "original", "expected_final_action": "merge_into_final_trace"},
            {"logical_trace_id": 1001, "span_id": 2, "event_kind": "original", "expected_final_action": "merge_into_final_trace"},
            {"logical_trace_id": 1001, "span_id": 2, "event_kind": "replay_clone", "expected_final_action": "ignore_after_cutoff"},
            {"logical_trace_id": 1001, "span_id": 3, "event_kind": "original", "expected_final_action": "ignore_after_cutoff"},
        ]

        expected_views = evaluator_module.build_expected_views(rows)

        # merge 集合是“最终应该保住哪些 span”，所以按 span_id 去重。
        self.assertEqual({"1", "2"}, expected_views["expected_merge_sets"]["1001"])
        # ignore 列表保留事件级颗粒度，后面 duplicate / misclassification 要按事件数来算。
        self.assertEqual(2, len(expected_views["expected_ignore_events"]["1001"]))
        self.assertEqual(1, len(expected_views["expected_replay_events"]["1001"]))

    def test_wait_until_counts_stable_uses_repeated_same_snapshot(self) -> None:
        if evaluator_module is None or not hasattr(evaluator_module, "wait_until_counts_stable"):
            self.fail("wait_until_counts_stable should exist for Suite B evaluator")

        counts = [
            (1, 2),
            (2, 4),
            (2, 4),
            (2, 4),
            (2, 4),
        ]

        def fake_query() -> tuple[int, int]:
            if counts:
                return counts.pop(0)
            return (2, 4)

        slept = []

        def fake_sleep(seconds: float) -> None:
            slept.append(seconds)

        stable_counts = evaluator_module.wait_until_counts_stable(
            query_counts=fake_query,
            poll_interval_sec=0.01,
            stable_rounds=2,
            confirm_sleep_sec=0.0,
            max_wait_sec=1.0,
            sleep_func=fake_sleep,
        )

        self.assertEqual((2, 4), stable_counts)
        self.assertGreaterEqual(len(slept), 1)

    def test_evaluate_suite_b_computes_three_primary_metrics(self) -> None:
        if evaluator_module is None or not hasattr(evaluator_module, "evaluate_suite_b"):
            self.fail("evaluate_suite_b should exist for Suite B evaluator")

        manifest_rows = [
            {"logical_trace_id": 1001, "span_id": 1, "event_kind": "original", "expected_final_action": "merge_into_final_trace"},
            {"logical_trace_id": 1001, "span_id": 2, "event_kind": "original", "expected_final_action": "merge_into_final_trace"},
            {"logical_trace_id": 1001, "span_id": 2, "event_kind": "replay_clone", "expected_final_action": "ignore_after_cutoff"},
            {"logical_trace_id": 1002, "span_id": 1, "event_kind": "original", "expected_final_action": "merge_into_final_trace"},
            {"logical_trace_id": 1002, "span_id": 2, "event_kind": "original", "expected_final_action": "merge_into_final_trace"},
            {"logical_trace_id": 1002, "span_id": 3, "event_kind": "original", "expected_final_action": "ignore_after_cutoff"},
            {"logical_trace_id": 1003, "span_id": 1, "event_kind": "original", "expected_final_action": "merge_into_final_trace"},
            {"logical_trace_id": 1003, "span_id": 2, "event_kind": "original", "expected_final_action": "merge_into_final_trace"},
        ]

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            manifest_path = temp_root / "manifest.jsonl"
            manifest_path.write_text(
                "\n".join(json.dumps(row, ensure_ascii=True) for row in manifest_rows) + "\n",
                encoding="utf-8",
            )

            sqlite_path = temp_root / "suite_b.db"
            conn = sqlite3.connect(sqlite_path)
            conn.executescript(
                """
                CREATE TABLE trace_summary (
                  trace_id TEXT PRIMARY KEY,
                  span_count INTEGER NOT NULL
                );
                CREATE TABLE trace_span (
                  trace_id TEXT NOT NULL,
                  span_id TEXT NOT NULL
                );
                INSERT INTO trace_summary(trace_id, span_count) VALUES ('1001', 3), ('1002', 3), ('1003', 1);
                INSERT INTO trace_span(trace_id, span_id) VALUES
                  ('1001', '1'),
                  ('1001', '2'),
                  ('1002', '1'),
                  ('1002', '2'),
                  ('1002', '3'),
                  ('1003', '1');
                """
            )
            conn.commit()
            conn.close()

            result = evaluator_module.evaluate_suite_b(
                manifest_path=manifest_path,
                sqlite_path=sqlite_path,
                poll_interval_sec=0.0,
                stable_rounds=1,
                confirm_sleep_sec=0.0,
                max_wait_sec=1.0,
            )

        self.assertEqual(3, result["trace_completeness_rate"]["total_traces"])
        self.assertEqual(2, result["trace_completeness_rate"]["matched_traces"])
        self.assertAlmostEqual(2.0 / 3.0, result["trace_completeness_rate"]["value"])
        self.assertEqual(1, result["trace_pollution_rate"]["matched_traces"])
        self.assertAlmostEqual(1.0 / 3.0, result["trace_pollution_rate"]["value"])
        self.assertEqual(1, result["duplicate_persistence_rate"]["matched_events"])
        self.assertEqual(1, result["duplicate_persistence_rate"]["total_events"])
        self.assertAlmostEqual(1.0, result["duplicate_persistence_rate"]["value"])


if __name__ == "__main__":
    unittest.main()
