#!/usr/bin/env python3

import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import run_suite_d_case as suite_d_module
except ModuleNotFoundError:
    suite_d_module = None


class SuiteDRunSuiteDCaseUnitTest(unittest.TestCase):
    def test_parse_args_accepts_single_case_runner_flags(self) -> None:
        if suite_d_module is None or not hasattr(suite_d_module, "parse_args"):
            self.fail("parse_args should exist for Suite D single-case runner")

        args = suite_d_module.parse_args(
            [
                "--server-bin",
                "./server/build/LogSentinel",
                "--run-root",
                "/tmp/suite_d_case",
                "--server-cpuset",
                "1-3",
                "--wrk-cpuset",
                "0",
                "--server-io-threads",
                "2",
                "--dispatch-worker-threads",
                "1",
                "--worker-threads",
                "12",
                "--worker-queue-size",
                "4096",
                "--trace-active-session-limit",
                "512",
                "--trace-buffered-span-limit",
                "4096",
                "--connections",
                "120",
                "--duration",
                "15s",
                "--warmup-duration",
                "3s",
                "--disable-ai",
            ]
        )

        self.assertEqual("./server/build/LogSentinel", args.server_bin)
        self.assertEqual("/tmp/suite_d_case", args.run_root)
        self.assertEqual("1-3", args.server_cpuset)
        self.assertEqual("0", args.wrk_cpuset)
        self.assertEqual(2, args.server_io_threads)
        self.assertEqual(1, args.dispatch_worker_threads)
        self.assertEqual(12, args.worker_threads)
        self.assertEqual(4096, args.worker_queue_size)
        self.assertEqual(512, args.trace_active_session_limit)
        self.assertEqual(4096, args.trace_buffered_span_limit)
        self.assertEqual(120, args.connections)
        self.assertEqual("15s", args.duration)
        self.assertEqual("3s", args.warmup_duration)
        self.assertTrue(args.disable_ai)

    def test_parse_wrk_metrics_extracts_requests_and_suite_d_summary_fields(self) -> None:
        if suite_d_module is None or not hasattr(suite_d_module, "parse_wrk_metrics"):
            self.fail("parse_wrk_metrics should exist for Suite D wrk summary parsing")

        sample_output = """
Running 15s test @ http://127.0.0.1:18080
  2 threads and 120 connections
  Latency Distribution
     95%   12.35ms
     99%   18.90ms
Requests/sec:  812.34
12184 requests in 15.00s, 1.20MB read
trace_model_suite_d metrics: offered_traces=1523 spans_per_trace=8 latency_p95_ms=12.35 latency_p99_ms=18.90
"""

        metrics = suite_d_module.parse_wrk_metrics(sample_output)

        self.assertEqual(12184, metrics["requests"])
        self.assertAlmostEqual(812.34, metrics["requests_per_sec"], places=2)
        self.assertEqual(1523, metrics["offered_traces"])
        self.assertEqual(8, metrics["spans_per_trace"])
        self.assertAlmostEqual(12.35, metrics["latency_p95_ms"], places=2)
        self.assertAlmostEqual(18.90, metrics["latency_p99_ms"], places=2)

    def test_run_suite_d_case_writes_online_completion_and_drain_metrics(self) -> None:
        if suite_d_module is None or not hasattr(suite_d_module, "run_suite_d_case"):
            self.fail("run_suite_d_case should exist for Suite D single-case runner")

        sample_output = """
Running 15s test @ http://127.0.0.1:18080
  2 threads and 120 connections
  Latency Distribution
     95%   12.35ms
     99%   18.90ms
Requests/sec:  812.34
12184 requests in 15.00s, 1.20MB read
trace_model_suite_d metrics: offered_traces=1523 spans_per_trace=8 latency_p95_ms=12.35 latency_p99_ms=18.90
"""

        with tempfile.TemporaryDirectory() as temp_dir:
            requested_root = str(Path(temp_dir) / "suite_d_case")
            wrk_calls = []

            def fake_wrk_runner(command, env):
                wrk_calls.append((list(command), dict(env)))
                command_text = " ".join(command)
                if "-d3s" in command_text:
                    return "warmup ok"
                return sample_output

            def fake_launch(command: str, _log_path: Path):
                return {"process": object(), "log_file": object()}

            def fake_wait(sqlite_path: Path, **kwargs):
                self.assertEqual("suite_d.db", sqlite_path.name)
                self.assertEqual(1523, kwargs["expected_trace_count"])
                return {
                    "final_counts": {"trace_summary": 1523, "trace_span": 12184},
                    "drain_tail_ms": 850,
                    "drain_timeout": False,
                }

            args = SimpleNamespace(
                server_bin="./server/build/LogSentinel",
                server_command="",
                run_root=requested_root,
                output_json="",
                sqlite_db="",
                server_log="",
                port_base=18080,
                server_cpuset="1-3",
                wrk_cpuset="0",
                wrk_bin="wrk",
                wrk_threads=2,
                connections=120,
                duration="15s",
                warmup_duration="3s",
                spans_per_trace=8,
                server_io_threads=2,
                dispatch_worker_threads=1,
                worker_threads=12,
                worker_queue_size=4096,
                trace_active_session_limit=512,
                trace_buffered_span_limit=4096,
                trace_max_dispatch_per_tick=64,
                trace_lifecycle_profile="protected",
                trace_sealed_grace_window_ms=100,
                trace_sweep_interval_ms=200,
                trace_primary_flush_span_threshold=512,
                trace_primary_flush_interval_ms=5,
                startup_timeout_sec=10.0,
                stop_timeout_sec=5.0,
                poll_interval_ms=50,
                stable_rounds=3,
                confirm_sleep_ms=100,
                max_drain_wait_ms=30000,
                disable_ai=True,
                disable_webhook=True,
                no_auto_start_proxy=True,
                disable_buffered_trace_repo=False,
            )

            with mock.patch.object(suite_d_module, "assert_port_available"), \
                mock.patch.object(suite_d_module, "launch_server_process", side_effect=fake_launch), \
                mock.patch.object(suite_d_module, "wait_for_port_ready"), \
                mock.patch.object(suite_d_module, "stop_server_process"):
                result = suite_d_module.run_suite_d_case(
                    args,
                    wrk_runner=fake_wrk_runner,
                    sqlite_counter=lambda _path: {"trace_summary": 1401, "trace_span": 11208},
                    wait_for_stable_runner=fake_wait,
                )

            output_json = Path(result["actual_run_root"]) / "result.json"
            saved = json.loads(output_json.read_text(encoding="utf-8"))

        self.assertEqual(2, len(wrk_calls))
        self.assertAlmostEqual(93.4, result["online_completed_traces_per_sec"], places=3)
        self.assertAlmostEqual(1401 / 1523, result["online_completion_ratio"], places=6)
        self.assertEqual(850, result["drain_tail_ms"])
        self.assertFalse(result["drain_timeout"])
        self.assertEqual(1523, result["wrk_metrics"]["offered_traces"])
        self.assertEqual(1401, result["sqlite_counts_at_stop"]["trace_summary"])
        self.assertEqual(1523, result["sqlite_counts_final"]["trace_summary"])
        self.assertEqual(12184, saved["wrk_metrics"]["requests"])
        self.assertEqual("1-3", saved["server_cpuset"])
        self.assertEqual("0", saved["wrk_cpuset"])


if __name__ == "__main__":
    unittest.main()
