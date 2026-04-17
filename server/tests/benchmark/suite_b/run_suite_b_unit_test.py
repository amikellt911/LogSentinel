#!/usr/bin/env python3

import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import run_suite_b as run_suite_b_module
except ModuleNotFoundError:
    run_suite_b_module = None


class SuiteBRunSuiteBUnitTest(unittest.TestCase):
    def test_parse_args_accepts_single_case_inputs(self) -> None:
        if run_suite_b_module is None or not hasattr(run_suite_b_module, "parse_args"):
            self.fail("parse_args should exist for Suite B run_suite_b runner")

        args = run_suite_b_module.parse_args(
            [
                "--sqlite-db",
                "/tmp/suite_b.db",
                "--profile",
                "mixed_realistic",
                "--trace-lifecycle-profile",
                "protected",
                "--send-workers",
                "3",
            ]
        )

        self.assertEqual("/tmp/suite_b.db", args.sqlite_db)
        self.assertEqual("mixed_realistic", args.profile)
        self.assertEqual("protected", args.trace_lifecycle_profile)
        self.assertEqual(3, args.send_workers)

    def test_run_suite_b_case_calls_sender_then_evaluator_and_writes_output(self) -> None:
        if run_suite_b_module is None or not hasattr(run_suite_b_module, "run_suite_b_case"):
            self.fail("run_suite_b_case should exist for Suite B run_suite_b runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            manifest_path = temp_root / "manifest.jsonl"
            output_json = temp_root / "result.json"
            sqlite_db = temp_root / "suite_b.db"
            order = []

            # 这里用 fake sender / evaluator，不起真实后端。
            # 我们只锁 orchestration：先产 manifest，再做 evaluator，最后把统一 JSON 落盘。
            def fake_sender(sender_args) -> int:
                order.append("sender")
                self.assertEqual(str(manifest_path), sender_args.manifest)
                self.assertEqual(4, sender_args.trace_count)
                manifest_path.write_text(
                    '{"logical_trace_id":1001,"span_id":1,"event_kind":"original",'
                    '"expected_final_action":"merge_into_final_trace",'
                    '"actual_send_start_ms":100,"actual_send_done_ms":107}\n',
                    encoding="utf-8",
                )
                return 0

            def fake_evaluator(**kwargs):
                order.append("evaluator")
                self.assertEqual(manifest_path, kwargs["manifest_path"])
                self.assertEqual(sqlite_db, kwargs["sqlite_path"])
                return {
                    "trace_completeness_rate": {"matched_traces": 1, "total_traces": 1, "value": 1.0},
                    "trace_pollution_rate": {"matched_traces": 0, "total_traces": 1, "value": 0.0},
                    "duplicate_persistence_rate": {"matched_events": 0, "total_events": 0, "value": 0.0},
                }

            args = SimpleNamespace(
                url="http://127.0.0.1:8080/logs/spans",
                profile="mixed_realistic",
                trace_lifecycle_profile="protected",
                seed=20260415,
                trace_count=4,
                spans_per_trace=3,
                base_gap_ms=20,
                trace_gap_ms=80,
                tick_ms=500,
                grace_ms=1000,
                tombstone_window_ms=12500,
                service_name="svc-suite-b",
                manifest=str(manifest_path),
                sqlite_db=str(sqlite_db),
                output_json=str(output_json),
                timeout_sec=1.0,
                send_workers=3,
                dry_run=False,
                poll_interval_sec=0.0,
                stable_rounds=1,
                confirm_sleep_sec=0.0,
                max_wait_sec=1.0,
            )

            result = run_suite_b_module.run_suite_b_case(
                args,
                sender_runner=fake_sender,
                evaluator_runner=fake_evaluator,
            )

            saved = json.loads(output_json.read_text(encoding="utf-8"))

        self.assertEqual(["sender", "evaluator"], order)
        self.assertEqual("mixed_realistic", result["profile"])
        self.assertEqual("protected", result["trace_lifecycle_profile"])
        self.assertEqual(str(manifest_path), result["manifest"])
        self.assertEqual(str(sqlite_db), result["sqlite_db"])
        self.assertEqual(1.0, saved["trace_completeness_rate"]["value"])
        self.assertEqual(0.0, saved["trace_pollution_rate"]["value"])
        self.assertEqual(1, saved["ingest_latency_ms"]["count"])
        self.assertEqual(7, saved["ingest_latency_ms"]["p95"])
        self.assertIn("experiment_context", saved)
        self.assertIn("artifacts", saved)
        self.assertEqual("suite_b", saved["experiment_context"]["suite"])
        self.assertIn("thread_topology", saved["experiment_context"])

    def test_load_ingest_latency_stats_uses_manifest_send_window(self) -> None:
        if run_suite_b_module is None or not hasattr(run_suite_b_module, "load_ingest_latency_stats"):
            self.fail("load_ingest_latency_stats should exist for Suite B latency guardrail")

        with tempfile.TemporaryDirectory() as temp_dir:
            manifest_path = Path(temp_dir) / "manifest.jsonl"
            # manifest 里记录的是 sender 视角的 HTTP 请求起止时间。
            # 这里故意放一条缺失时间的旧格式行，锁住 helper 要能兼容历史 manifest。
            manifest_path.write_text(
                "\n".join(
                    [
                        '{"actual_send_start_ms":100,"actual_send_done_ms":103}',
                        '{"actual_send_start_ms":200,"actual_send_done_ms":210}',
                        '{"actual_send_start_ms":300,"actual_send_done_ms":307}',
                        '{"logical_trace_id":999}',
                    ]
                )
                + "\n",
                encoding="utf-8",
            )

            stats = run_suite_b_module.load_ingest_latency_stats(manifest_path)

        self.assertEqual(3, stats["count"])
        self.assertEqual(3, stats["min"])
        self.assertEqual(10, stats["max"])
        self.assertEqual(7, stats["p50"])
        self.assertEqual(10, stats["p95"])
        self.assertEqual(10, stats["p99"])
        self.assertAlmostEqual(20 / 3, stats["avg"], places=4)


if __name__ == "__main__":
    unittest.main()
