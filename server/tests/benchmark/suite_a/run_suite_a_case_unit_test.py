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
    import run_suite_a_case as suite_a_module
except ModuleNotFoundError:
    suite_a_module = None


class SuiteARunSuiteACaseUnitTest(unittest.TestCase):
    def test_build_span_payload_includes_required_http_fields(self) -> None:
        if suite_a_module is None or not hasattr(suite_a_module, "build_span_payload"):
            self.fail("build_span_payload should exist for Suite A sender payload")

        # 这里锁的是 /logs/spans Handler 的必填字段，不让 sender 再因为漏字段被 HTTP 400 全拒。
        payload = suite_a_module.build_span_payload(
            trace_key=1001,
            span_id=2,
            spans_per_trace=8,
            service_name="svc-suite-a",
        )

        self.assertEqual(1001, payload["trace_key"])
        self.assertEqual(2, payload["span_id"])
        self.assertIn("start_time_ms", payload)
        self.assertEqual("svc-suite-a", payload["service_name"])
        self.assertIn("name", payload)
        self.assertFalse(payload["trace_end"])

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

    def test_parse_args_accepts_benchmark_server_passthrough_flags(self) -> None:
        if suite_a_module is None or not hasattr(suite_a_module, "parse_args"):
            self.fail("parse_args should exist for Suite A fixed sender runner")

        args = suite_a_module.parse_args(
            [
                "--server-bin",
                "./server/build/LogSentinel",
                "--run-root",
                "/tmp/suite_a_probe",
                "--trace-count",
                "320",
                "--inter-trace-gap-ms",
                "125",
                "--disable-ai",
                "--trace-lifecycle-profile",
                "minimal",
                "--trace-sealed-grace-window-ms",
                "200",
                "--trace-sweep-interval-ms",
                "100",
                "--trace-primary-flush-span-threshold",
                "64",
                "--trace-primary-flush-interval-ms",
                "5",
            ]
        )

        self.assertTrue(args.disable_ai)
        self.assertEqual("minimal", args.trace_lifecycle_profile)
        self.assertEqual(200, args.trace_sealed_grace_window_ms)
        self.assertEqual(100, args.trace_sweep_interval_ms)
        self.assertEqual(64, args.trace_primary_flush_span_threshold)
        self.assertEqual(5, args.trace_primary_flush_interval_ms)

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
            disable_ai=True,
            disable_webhook=True,
            disable_buffered_trace_repo=False,
            trace_lifecycle_profile="minimal",
            trace_sealed_grace_window_ms=200,
            trace_sweep_interval_ms=100,
            trace_primary_flush_span_threshold=64,
            trace_primary_flush_interval_ms=5,
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
        self.assertIn("--disable-ai", command)
        self.assertIn("--trace-lifecycle-profile minimal", command)
        self.assertIn("--trace-sealed-grace-window-ms 200", command)
        self.assertIn("--trace-sweep-interval-ms 100", command)
        self.assertIn("--trace-primary-flush-span-threshold 64", command)
        self.assertIn("--trace-primary-flush-interval-ms 5", command)

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
                # 这里只认“主数据最终补齐到目标 trace 数”才算完成。
                # 如果 run_suite_a_case 没把 trace_count 往下传，这条测试就应该红灯。
                self.assertEqual(320, kwargs["expected_trace_count"])
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

    def test_run_suite_a_case_records_resolved_server_command_in_result_json(self) -> None:
        if suite_a_module is None or not hasattr(suite_a_module, "run_suite_a_case"):
            self.fail("run_suite_a_case should exist for Suite A fixed sender runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            requested_root = str(Path(temp_dir) / "suite_a_compare_probe")
            launched_commands = []

            def fake_launch(command: str, _log_path: Path):
                # 这里只记录 run_suite_a_case 真正拿去起进程的命令，不碰真实子进程。
                # 这样可以精确锁住“结果 JSON 里的命令”和“实际 launch 的命令”必须是同一条。
                launched_commands.append(command)
                return {"process": object(), "log_file": object()}

            def fake_sender(sender_args):
                return {
                    "trace_count": sender_args.trace_count,
                    "spans_per_trace": sender_args.spans_per_trace,
                    "inter_trace_gap_ms": sender_args.inter_trace_gap_ms,
                    "t_stop_ms": 2200,
                    "sender_stats": {
                        "total_requests": 6400,
                        "success_requests": 6400,
                        "non_2xx_requests": 0,
                        "transport_errors": 0,
                    },
                }

            args = SimpleNamespace(
                url="",
                sqlite_db="",
                server_command="fake-server --db {sqlite_db} --port {port} --log {log_path}",
                server_bin="",
                run_root=requested_root,
                port_base=18186,
                server_log="",
                server_cpuset="",
                server_io_threads=1,
                worker_threads=32,
                dispatch_worker_threads=1,
                startup_timeout_sec=10.0,
                stop_timeout_sec=5.0,
                trace_count=800,
                spans_per_trace=8,
                inter_trace_gap_ms=25,
                send_workers=1,
                request_timeout_ms=1000,
                service_name="svc-suite-a",
                poll_interval_ms=200,
                stable_rounds=5,
                confirm_sleep_ms=300,
                max_drain_wait_ms=30000,
                output_json="",
            )

            with mock.patch.object(suite_a_module, "assert_port_available"), \
                mock.patch.object(suite_a_module, "launch_server_process", side_effect=fake_launch), \
                mock.patch.object(suite_a_module, "wait_for_port_ready"), \
                mock.patch.object(suite_a_module, "stop_server_process"):
                # 这里走 auto-start 分支，但把起停后端和 SQLite 稳定等待全部替换成假实现。
                # 测试目标只剩一个：最终结果文件里必须能看到解析占位符后的真实启动命令。
                result = suite_a_module.run_suite_a_case(
                    args,
                    sender_runner=fake_sender,
                    sqlite_counter=lambda _path: {"trace_summary": 800, "trace_span": 6400},
                    wait_for_stable_runner=lambda **_kwargs: {
                        "final_counts": {"trace_summary": 800, "trace_span": 6400},
                        "drain_tail_ms": 1305,
                        "drain_timeout": False,
                    },
                )

            output_json = Path(result["actual_run_root"]) / "result.json"
            saved = json.loads(output_json.read_text(encoding="utf-8"))

        self.assertEqual(1, len(launched_commands))
        self.assertEqual(launched_commands[0], result["resolved_server_command"])
        self.assertEqual(launched_commands[0], saved["resolved_server_command"])
        self.assertIn(result["sqlite_db"], result["resolved_server_command"])
        self.assertIn("--port 18186", result["resolved_server_command"])

if __name__ == "__main__":
    unittest.main()
