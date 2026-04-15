#!/usr/bin/env python3

import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

try:
    import run_suite_b_matrix as matrix_module
except ModuleNotFoundError:
    matrix_module = None


class SuiteBRunSuiteBMatrixUnitTest(unittest.TestCase):
    def test_parse_args_accepts_server_command_and_profile_lists(self) -> None:
        if matrix_module is None or not hasattr(matrix_module, "parse_args"):
            self.fail("parse_args should exist for Suite B matrix runner")

        args = matrix_module.parse_args(
            [
                "--server-command",
                "python3 -m http.server {port}",
                "--run-root",
                "/tmp/suite_b_matrix",
                "--sender-profiles",
                "clean_baseline,mixed_realistic",
                "--trace-lifecycle-profiles",
                "protected,minimal",
            ]
        )

        self.assertEqual("python3 -m http.server {port}", args.server_command)
        self.assertEqual("/tmp/suite_b_matrix", args.run_root)
        self.assertEqual("clean_baseline,mixed_realistic", args.sender_profiles)
        self.assertEqual("protected,minimal", args.trace_lifecycle_profiles)

    def test_parse_args_accepts_server_bin_and_resource_flags(self) -> None:
        # 这条测试锁的是“新入口能不能替代手写 server-command 模板”。
        # 只要这些字段能稳定进 argparse，后面 4 核和 16 核的差别就只是换 CLI 数字，不用改脚本代码。
        if matrix_module is None or not hasattr(matrix_module, "parse_args"):
            self.fail("parse_args should exist for Suite B matrix runner")

        args = matrix_module.parse_args(
            [
                "--server-bin",
                "./server/build/LogSentinel",
                "--server-cpuset",
                "0-2",
                "--server-io-threads",
                "3",
                "--worker-threads",
                "8",
                "--dispatch-worker-threads",
                "4",
                "--worker-queue-size",
                "4096",
                "--trace-capacity",
                "16",
                "--trace-token-limit",
                "0",
                "--trace-sweep-interval-ms",
                "200",
                "--trace-idle-timeout-ms",
                "800",
                "--trace-max-dispatch-per-tick",
                "64",
                "--trace-buffered-span-limit",
                "8192",
                "--trace-active-session-limit",
                "1024",
                "--disable-ai",
                "--disable-webhook",
                "--disable-buffered-trace-repo",
                "--no-auto-start-proxy",
            ]
        )

        self.assertEqual("./server/build/LogSentinel", args.server_bin)
        self.assertEqual("0-2", args.server_cpuset)
        self.assertEqual(3, args.server_io_threads)
        self.assertEqual(8, args.worker_threads)
        self.assertEqual(4, args.dispatch_worker_threads)
        self.assertEqual(4096, args.worker_queue_size)
        self.assertEqual(16, args.trace_capacity)
        self.assertEqual(0, args.trace_token_limit)
        self.assertEqual(200, args.trace_sweep_interval_ms)
        self.assertEqual(800, args.trace_idle_timeout_ms)
        self.assertEqual(64, args.trace_max_dispatch_per_tick)
        self.assertEqual(8192, args.trace_buffered_span_limit)
        self.assertEqual(1024, args.trace_active_session_limit)
        self.assertTrue(args.disable_ai)
        self.assertTrue(args.disable_webhook)
        self.assertTrue(args.disable_buffered_trace_repo)
        self.assertTrue(args.no_auto_start_proxy)

    def test_build_server_command_uses_default_builder_when_template_missing(self) -> None:
        # 这里直接锁默认命令拼装结果，避免后面某个参数漏传后，
        # 矩阵 runner 表面还能跑，实际却悄悄回退到后端默认配置。
        if matrix_module is None or not hasattr(matrix_module, "resolve_server_command"):
            self.fail("resolve_server_command should exist for Suite B matrix runner")

        args = SimpleNamespace(
            server_command="",
            server_bin="./server/build/LogSentinel",
            server_cpuset="0-3",
            server_io_threads=5,
            no_auto_start_proxy=True,
            worker_threads=12,
            dispatch_worker_threads=4,
            worker_queue_size=4096,
            trace_capacity=16,
            trace_token_limit=0,
            trace_sweep_interval_ms=200,
            trace_idle_timeout_ms=800,
            trace_max_dispatch_per_tick=64,
            trace_buffered_span_limit=8192,
            trace_active_session_limit=1024,
            disable_ai=True,
            disable_webhook=True,
            disable_buffered_trace_repo=False,
        )
        case = {
            "sqlite_db": "/tmp/suite_b.db",
            "trace_lifecycle_profile": "protected",
            "port": 19123,
            "server_log": "/tmp/server.log",
            "case_id": "protected__clean_baseline",
            "run_dir": "/tmp/run_dir",
        }

        command = matrix_module.resolve_server_command(args, case)

        self.assertIn("taskset -c 0-3", command)
        self.assertIn("./server/build/LogSentinel", command)
        self.assertIn("--db /tmp/suite_b.db", command)
        self.assertIn("--port 19123", command)
        self.assertIn("--trace-lifecycle-profile protected", command)
        self.assertIn("--server-io-threads 5", command)
        self.assertIn("--worker-threads 12", command)
        self.assertIn("--dispatch-worker-threads 4", command)
        self.assertIn("--worker-queue-size 4096", command)
        self.assertIn("--trace-capacity 16", command)
        self.assertIn("--trace-token-limit 0", command)
        self.assertIn("--trace-sweep-interval-ms 200", command)
        self.assertIn("--trace-idle-timeout-ms 800", command)
        self.assertIn("--trace-max-dispatch-per-tick 64", command)
        self.assertIn("--trace-buffered-span-limit 8192", command)
        self.assertIn("--trace-active-session-limit 1024", command)
        self.assertIn("--disable-ai", command)
        self.assertIn("--disable-webhook", command)
        self.assertIn("--no-auto-start-proxy", command)

    def test_build_case_matrix_creates_isolated_paths(self) -> None:
        if matrix_module is None or not hasattr(matrix_module, "build_case_matrix"):
            self.fail("build_case_matrix should exist for Suite B matrix runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            args = SimpleNamespace(
                run_root=str(Path(temp_dir) / "suite_b"),
                sender_profiles="clean_baseline,mixed_realistic",
                trace_lifecycle_profiles="protected,minimal",
                port_base=19080,
            )

            cases = matrix_module.build_case_matrix(args)

        self.assertEqual(4, len(cases))
        self.assertEqual("protected__clean_baseline", cases[0]["case_id"])
        self.assertEqual("minimal__mixed_realistic", cases[-1]["case_id"])
        self.assertNotEqual(cases[0]["sqlite_db"], cases[1]["sqlite_db"])
        self.assertTrue(cases[0]["manifest"].endswith("manifest.jsonl"))
        self.assertTrue(cases[0]["output_json"].endswith("result.json"))
        self.assertTrue(cases[0]["server_log"].endswith("server.log"))
        self.assertEqual(19080, cases[0]["port"])
        self.assertEqual(19083, cases[-1]["port"])

    def test_run_suite_b_matrix_launches_server_per_case_and_writes_summary(self) -> None:
        if matrix_module is None or not hasattr(matrix_module, "run_suite_b_matrix"):
            self.fail("run_suite_b_matrix should exist for Suite B matrix runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            run_root = temp_root / "runs"
            sequence = []

            def fake_launch(case, args):
                # 这条断言保证 run_suite_b_matrix 主流程里确实吃到了新的默认命令构造，
                # 而不是测试里单独调 helper 能过，真正跑矩阵时又掉回空字符串。
                sequence.append(("launch", case["case_id"]))
                self.assertIn(case["trace_lifecycle_profile"], args.server_command)
                self.assertIn("--server-io-threads 3", args.server_command)
                self.assertIn("--worker-threads 6", args.server_command)
                self.assertIn("--dispatch-worker-threads 2", args.server_command)
                self.assertIn("--disable-ai", args.server_command)
                self.assertIn("taskset -c 0-3", args.server_command)
                return {"pid": case["case_id"]}

            def fake_assert_port_available(port: int) -> None:
                sequence.append(("port_check", port))

            def fake_wait(_port: int, _timeout_sec: float) -> None:
                sequence.append(("wait", _port))

            def fake_check_process_alive(_process_info) -> None:
                return None

            def fake_run_case(case_args):
                sequence.append(("case", case_args.trace_lifecycle_profile, case_args.profile))
                Path(case_args.manifest).write_text("{}", encoding="utf-8")
                return {
                    "profile": case_args.profile,
                    "trace_lifecycle_profile": case_args.trace_lifecycle_profile,
                    "trace_completeness_rate": {"matched_traces": 1, "total_traces": 1, "value": 1.0},
                }

            def fake_stop(process_info, _timeout_sec: float) -> None:
                sequence.append(("stop", process_info["pid"]))

            args = SimpleNamespace(
                server_command="",
                server_bin="python3 fake_server.py",
                server_cpuset="0-3",
                server_io_threads=3,
                no_auto_start_proxy=True,
                worker_threads=6,
                dispatch_worker_threads=2,
                worker_queue_size=2048,
                trace_capacity=12,
                trace_token_limit=0,
                trace_sweep_interval_ms=200,
                trace_idle_timeout_ms=800,
                trace_max_dispatch_per_tick=64,
                trace_buffered_span_limit=4096,
                trace_active_session_limit=512,
                disable_ai=True,
                disable_webhook=False,
                disable_buffered_trace_repo=False,
                run_root=str(run_root),
                sender_profiles="clean_baseline,mixed_realistic",
                trace_lifecycle_profiles="protected,minimal",
                port_base=19080,
                startup_timeout_sec=1.0,
                stop_timeout_sec=1.0,
                url_template="http://127.0.0.1:{port}/logs/spans",
                seed=20260415,
                trace_count=2,
                spans_per_trace=3,
                base_gap_ms=20,
                trace_gap_ms=80,
                tick_ms=500,
                grace_ms=1000,
                tombstone_window_ms=12500,
                service_name="svc-suite-b",
                timeout_sec=1.0,
                send_workers=2,
                dry_run=True,
                poll_interval_sec=0.0,
                stable_rounds=1,
                confirm_sleep_sec=0.0,
                max_wait_sec=1.0,
                output_summary=str(temp_root / "summary.json"),
            )

            result = matrix_module.run_suite_b_matrix(
                args,
                assert_port_available=fake_assert_port_available,
                launch_server=fake_launch,
                wait_for_port=fake_wait,
                check_process_alive=fake_check_process_alive,
                run_case=fake_run_case,
                stop_server=fake_stop,
            )

            saved = json.loads(Path(args.output_summary).read_text(encoding="utf-8"))

        self.assertEqual(4, result["total_cases"])
        self.assertEqual(4, len(result["cases"]))
        self.assertEqual(4, saved["total_cases"])
        self.assertEqual(("port_check", 19080), sequence[0])
        self.assertEqual(("launch", "protected__clean_baseline"), sequence[1])
        self.assertIn(("case", "minimal", "mixed_realistic"), sequence)
        self.assertEqual(("stop", "minimal__mixed_realistic"), sequence[-1])

    def test_assert_port_available_rejects_occupied_port(self) -> None:
        # 这条测试锁的是“ready 前先判空端口”，
        # 避免旧进程占着端口时，被误判成新 case 已经成功启动。
        if matrix_module is None or not hasattr(matrix_module, "assert_port_available"):
            self.fail("assert_port_available should exist for Suite B matrix runner")

        def fake_probe(_host: str, _port: int) -> int:
            return 0

        with self.assertRaises(RuntimeError):
            matrix_module.assert_port_available(19080, probe_connect=fake_probe)


if __name__ == "__main__":
    unittest.main()
