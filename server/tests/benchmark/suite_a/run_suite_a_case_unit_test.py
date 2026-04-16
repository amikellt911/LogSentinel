#!/usr/bin/env python3

import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

try:
    import run_suite_a_case as suite_a_module
except ModuleNotFoundError:
    suite_a_module = None


class SuiteARunSuiteACaseUnitTest(unittest.TestCase):
    def test_parse_args_accepts_server_autostart_flags(self) -> None:
        if suite_a_module is None or not hasattr(suite_a_module, "parse_args"):
            self.fail("parse_args should exist for Suite A fixed sender runner")

        args = suite_a_module.parse_args(
            [
                "--server-bin",
                "./server/build/LogSentinel",
                "--run-root",
                "/tmp/suite_a_probe",
                "--port-base",
                "18180",
                "--server-cpuset",
                "1-2",
                "--server-io-threads",
                "1",
                "--worker-threads",
                "32",
                "--dispatch-worker-threads",
                "1",
                "--trace-count",
                "320",
                "--inter-trace-gap-ms",
                "125",
            ]
        )

        self.assertEqual("./server/build/LogSentinel", args.server_bin)
        self.assertEqual("/tmp/suite_a_probe", args.run_root)
        self.assertEqual(18180, args.port_base)
        self.assertEqual("1-2", args.server_cpuset)
        self.assertEqual(1, args.server_io_threads)
        self.assertEqual(32, args.worker_threads)
        self.assertEqual(1, args.dispatch_worker_threads)

    def test_parse_args_accepts_fixed_sender_and_sqlite_flags(self) -> None:
        if suite_a_module is None or not hasattr(suite_a_module, "parse_args"):
            self.fail("parse_args should exist for Suite A fixed sender runner")

        args = suite_a_module.parse_args(
            [
                "--url",
                "http://127.0.0.1:18080/logs/spans",
                "--sqlite-db",
                "/tmp/suite_a.db",
                "--trace-count",
                "320",
                "--spans-per-trace",
                "8",
                "--inter-trace-gap-ms",
                "100",
                "--send-workers",
                "2",
                "--poll-interval-ms",
                "200",
                "--stable-rounds",
                "5",
                "--max-drain-wait-ms",
                "30000",
                "--output-json",
                "/tmp/result.json",
            ]
        )

        self.assertEqual("http://127.0.0.1:18080/logs/spans", args.url)
        self.assertEqual("/tmp/suite_a.db", args.sqlite_db)
        self.assertEqual(320, args.trace_count)
        self.assertEqual(8, args.spans_per_trace)
        self.assertEqual(100, args.inter_trace_gap_ms)
        self.assertEqual(2, args.send_workers)
        self.assertEqual(200, args.poll_interval_ms)
        self.assertEqual(5, args.stable_rounds)
        self.assertEqual(30000, args.max_drain_wait_ms)
        self.assertEqual("/tmp/result.json", args.output_json)

    def test_resolve_run_artifacts_adds_timestamp_suffix_and_derives_paths(self) -> None:
        if suite_a_module is None or not hasattr(suite_a_module, "resolve_run_artifacts"):
            self.fail("resolve_run_artifacts should exist for Suite A auto-start mode")

        requested, actual, artifacts = suite_a_module.resolve_run_artifacts(
            run_root="/tmp/suite_a_probe",
            output_json="",
            sqlite_db="",
            server_log="",
            now_func=lambda: 1776303718.237,
        )

        self.assertEqual("/tmp/suite_a_probe", requested)
        self.assertEqual("/tmp/suite_a_probe-20260416-094158-237ms", actual)
        self.assertEqual("/tmp/suite_a_probe-20260416-094158-237ms/result.json", artifacts["output_json"])
        self.assertEqual("/tmp/suite_a_probe-20260416-094158-237ms/suite_a.db", artifacts["sqlite_db"])
        self.assertEqual("/tmp/suite_a_probe-20260416-094158-237ms/server.log", artifacts["server_log"])

    def test_resolve_server_command_builds_baseline_mock_ai_command(self) -> None:
        if suite_a_module is None or not hasattr(suite_a_module, "resolve_server_command"):
            self.fail("resolve_server_command should exist for Suite A auto-start mode")

        args = SimpleNamespace(
            server_command="",
            server_bin="./server/build/LogSentinel",
            server_cpuset="1-2",
            server_io_threads=1,
            worker_threads=32,
            dispatch_worker_threads=1,
            port_base=18180,
        )
        artifacts = {
            "sqlite_db": "/tmp/suite_a_probe/suite_a.db",
            "server_log": "/tmp/suite_a_probe/server.log",
            "port": 18180,
        }

        # 这里锁的是当前 baseline 口径：
        # 自动起后端时默认要开 AI mock、关 webhook，并把资源参数一起带进去。
        command = suite_a_module.resolve_server_command(args, artifacts)

        self.assertIn("taskset -c 1-2", command)
        self.assertIn("./server/build/LogSentinel", command)
        self.assertIn("--db /tmp/suite_a_probe/suite_a.db", command)
        self.assertIn("--port 18180", command)
        self.assertIn("--trace-ai-provider mock", command)
        self.assertIn("--auto-start-proxy", command)
        self.assertIn("--disable-webhook", command)
        self.assertIn("--server-io-threads 1", command)
        self.assertIn("--worker-threads 32", command)
        self.assertIn("--dispatch-worker-threads 1", command)

    def test_run_suite_a_case_writes_metrics_from_sender_and_sqlite_counts(self) -> None:
        if suite_a_module is None or not hasattr(suite_a_module, "run_suite_a_case"):
            self.fail("run_suite_a_case should exist for Suite A fixed sender runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            sqlite_db = temp_root / "suite_a.db"
            output_json = temp_root / "result.json"
            events = []

            # 这里不用真实 HTTP 和真实 SQLite。
            # 这条测试只锁 orchestration：先发 clean trace，记 t_stop，再查 stop/final 计数并落统一 JSON。
            def fake_sender(sender_args):
                events.append(("sender", sender_args.trace_count, sender_args.send_workers))
                return {
                    "trace_count": sender_args.trace_count,
                    "spans_per_trace": sender_args.spans_per_trace,
                    "t_stop_ms": 1200,
                    "sender_stats": {
                        "total_requests": 2560,
                        "success_requests": 2500,
                        "non_2xx_requests": 60,
                        "transport_errors": 0,
                    },
                }

            # 第一枪是 stop 时刻快照，后面两枪用来等稳定。
            count_sequence = iter(
                [
                    {"trace_summary": 180, "trace_span": 1440},
                ]
            )

            def fake_read_counts(_sqlite_path: Path):
                value = next(count_sequence)
                events.append(("counts", value["trace_summary"], value["trace_span"]))
                return value

            def fake_wait(sqlite_path: Path, **kwargs):
                events.append(("wait", sqlite_path.name, kwargs["stable_rounds"], kwargs["poll_interval_ms"]))
                return {
                    "final_counts": {"trace_summary": 280, "trace_span": 2240},
                    "drain_tail_ms": 800,
                    "drain_timeout": False,
                }

            args = SimpleNamespace(
                url="http://127.0.0.1:18080/logs/spans",
                sqlite_db=str(sqlite_db),
                trace_count=320,
                spans_per_trace=8,
                inter_trace_gap_ms=100,
                send_workers=1,
                request_timeout_ms=1000,
                service_name="svc-suite-a",
                poll_interval_ms=200,
                stable_rounds=5,
                confirm_sleep_ms=300,
                max_drain_wait_ms=30000,
                output_json=str(output_json),
            )

            result = suite_a_module.run_suite_a_case(
                args,
                sender_runner=fake_sender,
                sqlite_counter=fake_read_counts,
                wait_for_stable_runner=fake_wait,
            )

            saved = json.loads(output_json.read_text(encoding="utf-8"))

        self.assertEqual(("sender", 320, 1), events[0])
        self.assertEqual(("counts", 180, 1440), events[1])
        self.assertEqual(("wait", "suite_a.db", 5, 200), events[2])
        self.assertEqual(320, result["trace_count"])
        self.assertEqual(180, result["visible_trace_count_at_stop"])
        self.assertAlmostEqual(180 / 320, result["visible_completion_rate_at_stop"], places=6)
        self.assertEqual(800, result["drain_tail_ms"])
        self.assertEqual(280, result["sqlite_counts_final"]["trace_summary"])
        self.assertEqual(2240, result["sqlite_counts_final"]["trace_span"])
        self.assertEqual(2560, saved["sender_stats"]["total_requests"])

    def test_wait_until_sqlite_stable_returns_final_counts_and_tail(self) -> None:
        if suite_a_module is None or not hasattr(suite_a_module, "wait_until_sqlite_stable"):
            self.fail("wait_until_sqlite_stable should exist for Suite A SQLite polling")

        monotonic_points = iter([10.0, 10.2, 10.4, 10.6, 10.8, 11.0, 11.0])
        sleep_calls = []
        count_sequence = iter(
            [
                {"trace_summary": 10, "trace_span": 80},
                {"trace_summary": 12, "trace_span": 96},
                {"trace_summary": 12, "trace_span": 96},
                {"trace_summary": 12, "trace_span": 96},
            ]
        )

        # 这条测试锁的是“连续稳定若干轮才算 drain 完毕”，
        # 避免实现退化回拍脑袋固定 sleep。
        stable = suite_a_module.wait_until_sqlite_stable(
            sqlite_path=Path("/tmp/suite_a.db"),
            sqlite_counter=lambda _path: next(count_sequence),
            poll_interval_ms=200,
            stable_rounds=2,
            confirm_sleep_ms=0,
            max_wait_ms=2000,
            sleep_func=lambda seconds: sleep_calls.append(seconds),
            monotonic_func=lambda: next(monotonic_points),
            start_ms=1000,
        )

        self.assertEqual({"trace_summary": 12, "trace_span": 96}, stable["final_counts"])
        self.assertEqual(10800, stable["t_stable_ms"])
        self.assertEqual(9800, stable["drain_tail_ms"])
        self.assertFalse(stable["drain_timeout"])
        self.assertEqual([0.2, 0.2, 0.2], sleep_calls)


if __name__ == "__main__":
    unittest.main()
