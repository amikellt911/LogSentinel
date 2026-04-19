#!/usr/bin/env python3

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import run_suite_d_scaling_fixed_load as fixed_scaling_module
except ModuleNotFoundError:
    fixed_scaling_module = None


class SuiteDRunSuiteDScalingFixedLoadUnitTest(unittest.TestCase):
    def test_fixed_load_defaults_pin_sender_and_backend_points(self) -> None:
        if fixed_scaling_module is None:
            self.fail("run_suite_d_scaling_fixed_load module should exist")

        # fixed-load 曲线的核心是“输入负载不跟后端核数一起涨”。
        # 这里先锁默认值，避免后续又退化成旧的 sender/backend 联动 scaling。
        self.assertEqual([4, 8, 12, 16, 20, 24], fixed_scaling_module.DEFAULT_BACKEND_CORE_POINTS)
        self.assertEqual("0-3", fixed_scaling_module.DEFAULT_WRK_CPUSET)
        self.assertEqual(4, fixed_scaling_module.DEFAULT_WRK_THREADS)
        self.assertEqual(90, fixed_scaling_module.DEFAULT_CONNECTIONS)
        self.assertEqual(4, fixed_scaling_module.DEFAULT_BACKEND_CORE_OFFSET)

    def test_run_fixed_load_scaling_keeps_wrk_load_constant(self) -> None:
        if fixed_scaling_module is None or not hasattr(fixed_scaling_module, "run_fixed_load_scaling"):
            self.fail("run_fixed_load_scaling should exist for Suite D fixed-load runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            output_summary = Path(temp_dir) / "summary.json"
            args = fixed_scaling_module.parse_args(
                [
                    "--scaling-root",
                    str(Path(temp_dir) / "suite_d_fixed"),
                    "--output-summary",
                    str(output_summary),
                    "--server-bin",
                    "./server/build/LogSentinel",
                    "--backend-core-points",
                    "4,8,12,16,20,24",
                    "--wrk-cpuset",
                    "0-3",
                    "--wrk-threads",
                    "4",
                    "--connections",
                    "90",
                    "--backend-core-offset",
                    "4",
                ]
            )
            args.actual_scaling_root = str(Path(temp_dir) / "suite_d_fixed-actual")
            lines = []
            case_calls = []

            def fake_case_runner(case_args):
                run_root = Path(case_args.run_root)
                backend_cores = int(run_root.parts[-2].split("_", 1)[1])
                # 这里记录 runner 派生出的真实运行参数。
                # 如果 wrk 负载或 sender 绑核随 backend_cores 改变，这条测试会直接红灯。
                case_calls.append(
                    {
                        "backend_cores": backend_cores,
                        "wrk_cpuset": case_args.wrk_cpuset,
                        "server_cpuset": case_args.server_cpuset,
                        "wrk_threads": case_args.wrk_threads,
                        "connections": case_args.connections,
                        "server_io_threads": case_args.server_io_threads,
                        "dispatch_worker_threads": case_args.dispatch_worker_threads,
                        "worker_threads": case_args.worker_threads,
                        "disable_ai": case_args.disable_ai,
                    }
                )
                return {
                    "requested_run_root": str(run_root),
                    "actual_run_root": f"{run_root}-actual",
                    "wrk_metrics": {
                        "requests": backend_cores * 1000,
                        "requests_per_sec": float(100000 + backend_cores),
                        "offered_traces": 200000,
                        "latency_p95_ms": 0.7,
                        "latency_p99_ms": 1.2,
                    },
                    "online_completed_traces_per_sec": float(8000 + backend_cores * 10),
                    "online_completion_ratio": 0.50 + backend_cores * 0.001,
                    "drain_tail_ms": 9000 - backend_cores * 100,
                    "drain_timeout": False,
                }

            summary = fixed_scaling_module.run_fixed_load_scaling(
                args,
                case_runner=fake_case_runner,
                line_writer=lines.append,
            )
            saved = json.loads(output_summary.read_text(encoding="utf-8"))

        self.assertEqual(6, len(case_calls))
        self.assertEqual("0-3", case_calls[0]["wrk_cpuset"])
        self.assertEqual("0-3", case_calls[-1]["wrk_cpuset"])
        self.assertEqual("4-7", case_calls[0]["server_cpuset"])
        self.assertEqual("4-27", case_calls[-1]["server_cpuset"])
        self.assertEqual({4}, {item["wrk_threads"] for item in case_calls})
        self.assertEqual({90}, {item["connections"] for item in case_calls})
        self.assertTrue(all(item["disable_ai"] for item in case_calls))
        self.assertEqual(
            "[point 6/6] backend=24 wrk=0-3 server=4-27 online=8240.00 ratio=0.5240 drain=6600 qps=100024.00",
            lines[-1],
        )
        self.assertEqual(24, summary["overall"]["best_backend_cores"])
        self.assertEqual(24, saved["overall"]["best_backend_cores"])
        self.assertEqual(6, len(saved["by_backend_cores"]))
        self.assertEqual("0-3", saved["experiment_context"]["cpu_allocation"]["wrk_cpuset"])
        self.assertEqual("4-27", saved["by_backend_cores"][-1]["server_cpuset"])
        self.assertEqual(90, saved["experiment_context"]["workload"]["connections"])

    def test_thread_topology_override_keeps_backend_cpuset(self) -> None:
        if fixed_scaling_module is None or not hasattr(fixed_scaling_module, "run_fixed_load_scaling"):
            self.fail("run_fixed_load_scaling should exist for Suite D fixed-load runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            output_summary = Path(temp_dir) / "summary.json"
            args = fixed_scaling_module.parse_args(
                [
                    "--scaling-root",
                    str(Path(temp_dir) / "suite_d_fixed"),
                    "--output-summary",
                    str(output_summary),
                    "--server-bin",
                    "./server/build/LogSentinel",
                    "--backend-core-points",
                    "24",
                    "--backend-core-offset",
                    "68",
                    "--force-server-io-threads",
                    "5",
                    "--force-dispatch-worker-threads",
                    "3",
                    "--force-worker-threads",
                    "28",
                ]
            )
            args.actual_scaling_root = str(Path(temp_dir) / "suite_d_fixed-actual")
            case_calls = []

            def fake_case_runner(case_args):
                # 这个诊断开关只覆写线程拓扑，不应该偷偷减少 server_cpuset。
                # 如果 cpuset 也变小，就无法判断“24 核资源 + 20 核线程拓扑”是否更稳。
                case_calls.append(
                    {
                        "server_cpuset": case_args.server_cpuset,
                        "server_io_threads": case_args.server_io_threads,
                        "dispatch_worker_threads": case_args.dispatch_worker_threads,
                        "worker_threads": case_args.worker_threads,
                        "worker_queue_size": case_args.worker_queue_size,
                    }
                )
                return {
                    "requested_run_root": str(Path(case_args.run_root)),
                    "actual_run_root": f"{case_args.run_root}-actual",
                    "wrk_metrics": {
                        "requests": 1000,
                        "requests_per_sec": 100000.0,
                        "offered_traces": 200000,
                        "latency_p95_ms": 0.7,
                        "latency_p99_ms": 1.2,
                    },
                    "online_completed_traces_per_sec": 9000.0,
                    "online_completion_ratio": 0.45,
                    "drain_tail_ms": 8000,
                    "drain_timeout": False,
                }

            summary = fixed_scaling_module.run_fixed_load_scaling(
                args,
                case_runner=fake_case_runner,
                line_writer=lambda _line: None,
            )
            saved = json.loads(output_summary.read_text(encoding="utf-8"))

        self.assertEqual(
            [
                {
                    "server_cpuset": "68-91",
                    "server_io_threads": 5,
                    "dispatch_worker_threads": 3,
                    "worker_threads": 28,
                    "worker_queue_size": 7168,
                }
            ],
            case_calls,
        )
        self.assertEqual(5, summary["by_backend_cores"][0]["server_io_threads"])
        self.assertEqual(3, summary["by_backend_cores"][0]["dispatch_worker_threads"])
        self.assertEqual(28, summary["by_backend_cores"][0]["worker_threads"])
        self.assertEqual(
            {
                "server_io_threads": 5,
                "dispatch_worker_threads": 3,
                "worker_threads": 28,
            },
            saved["experiment_context"]["thread_topology"]["forced_topology"],
        )

    def test_cleanup_sqlite_db_and_cooldown_after_each_case(self) -> None:
        if fixed_scaling_module is None or not hasattr(fixed_scaling_module, "run_fixed_load_scaling"):
            self.fail("run_fixed_load_scaling should exist for Suite D fixed-load runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            output_summary = Path(temp_dir) / "summary.json"
            args = fixed_scaling_module.parse_args(
                [
                    "--scaling-root",
                    str(Path(temp_dir) / "suite_d_fixed"),
                    "--output-summary",
                    str(output_summary),
                    "--server-bin",
                    "./server/build/LogSentinel",
                    "--backend-core-points",
                    "20,24",
                    "--cleanup-sqlite-db",
                    "--cooldown-sec",
                    "7",
                ]
            )
            args.actual_scaling_root = str(Path(temp_dir) / "suite_d_fixed-actual")
            db_paths = []
            sleeps = []
            lines = []

            def fake_case_runner(case_args):
                run_root = Path(case_args.run_root)
                actual_run_root = Path(f"{run_root}-actual")
                actual_run_root.mkdir(parents=True, exist_ok=True)
                sqlite_db = actual_run_root / "suite_d.db"
                sqlite_db.write_text("fake sqlite payload", encoding="utf-8")
                db_paths.append(sqlite_db)
                return {
                    "requested_run_root": str(run_root),
                    "actual_run_root": str(actual_run_root),
                    "sqlite_db": str(sqlite_db),
                    "wrk_metrics": {
                        "requests": 1000,
                        "requests_per_sec": 100000.0,
                        "offered_traces": 200000,
                        "latency_p95_ms": 0.7,
                        "latency_p99_ms": 1.2,
                    },
                    "online_completed_traces_per_sec": 9000.0,
                    "online_completion_ratio": 0.45,
                    "drain_tail_ms": 8000,
                    "drain_timeout": False,
                }

            summary = fixed_scaling_module.run_fixed_load_scaling(
                args,
                case_runner=fake_case_runner,
                line_writer=lines.append,
                sleeper=sleeps.append,
            )
            saved = json.loads(output_summary.read_text(encoding="utf-8"))

        self.assertEqual([7.0, 7.0], sleeps)
        self.assertEqual(2, len(db_paths))
        self.assertTrue(all(not path.exists() for path in db_paths))
        self.assertTrue(any("[cleanup_sqlite_db]" in line for line in lines))
        self.assertTrue(any("[cooldown]" in line for line in lines))
        self.assertTrue(summary["fixed_load"]["cleanup_sqlite_db"])
        self.assertEqual(7.0, summary["fixed_load"]["cooldown_sec"])
        self.assertTrue(saved["experiment_context"]["effective_flags"]["cleanup_sqlite_db"])
        self.assertEqual(7.0, saved["experiment_context"]["effective_flags"]["cooldown_sec"])

    def test_trace_limit_override_replaces_derived_limits(self) -> None:
        if fixed_scaling_module is None or not hasattr(fixed_scaling_module, "run_fixed_load_scaling"):
            self.fail("run_fixed_load_scaling should exist for Suite D fixed-load runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            output_summary = Path(temp_dir) / "summary.json"
            args = fixed_scaling_module.parse_args(
                [
                    "--scaling-root",
                    str(Path(temp_dir) / "suite_d_fixed"),
                    "--output-summary",
                    str(output_summary),
                    "--server-bin",
                    "./server/build/LogSentinel",
                    "--backend-core-points",
                    "24",
                    "--trace-active-session-limit",
                    "4096",
                    "--trace-buffered-span-limit",
                    "32768",
                ]
            )
            args.actual_scaling_root = str(Path(temp_dir) / "suite_d_fixed-actual")
            case_calls = []

            def fake_case_runner(case_args):
                # 这组覆写是为了验证 active/buffer 两个容量闸门是不是能真正进到单 case runner。
                # 如果这里还是默认 2048/16384，就说明 CLI 只停留在 wrapper，没有打进后端配置。
                case_calls.append(
                    {
                        "trace_active_session_limit": case_args.trace_active_session_limit,
                        "trace_buffered_span_limit": case_args.trace_buffered_span_limit,
                    }
                )
                return {
                    "requested_run_root": str(Path(case_args.run_root)),
                    "actual_run_root": f"{case_args.run_root}-actual",
                    "wrk_metrics": {
                        "requests": 1000,
                        "requests_per_sec": 100000.0,
                        "offered_traces": 200000,
                        "latency_p95_ms": 0.7,
                        "latency_p99_ms": 1.2,
                    },
                    "online_completed_traces_per_sec": 9000.0,
                    "online_completion_ratio": 0.45,
                    "drain_tail_ms": 8000,
                    "drain_timeout": False,
                }

            summary = fixed_scaling_module.run_fixed_load_scaling(
                args,
                case_runner=fake_case_runner,
                line_writer=lambda _line: None,
            )
            saved = json.loads(output_summary.read_text(encoding="utf-8"))

        self.assertEqual(
            [{"trace_active_session_limit": 4096, "trace_buffered_span_limit": 32768}],
            case_calls,
        )
        self.assertEqual(4096, summary["by_backend_cores"][0]["trace_active_session_limit"])
        self.assertEqual(32768, summary["by_backend_cores"][0]["trace_buffered_span_limit"])
        self.assertEqual(
            {
                "trace_active_session_limit": 4096,
                "trace_buffered_span_limit": 32768,
            },
            saved["fixed_load"]["forced_trace_limits"],
        )
        self.assertEqual(
            {
                "trace_active_session_limit": 4096,
                "trace_buffered_span_limit": 32768,
            },
            saved["experiment_context"]["effective_flags"]["forced_trace_limits"],
        )

    def test_sqlite_root_override_moves_db_out_of_run_root(self) -> None:
        if fixed_scaling_module is None or not hasattr(fixed_scaling_module, "run_fixed_load_scaling"):
            self.fail("run_fixed_load_scaling should exist for Suite D fixed-load runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            sqlite_root = Path(temp_dir) / "tmpfs_like_sqlite"
            output_summary = Path(temp_dir) / "summary.json"
            args = fixed_scaling_module.parse_args(
                [
                    "--scaling-root",
                    str(Path(temp_dir) / "suite_d_fixed"),
                    "--output-summary",
                    str(output_summary),
                    "--server-bin",
                    "./server/build/LogSentinel",
                    "--backend-core-points",
                    "20",
                    "--sqlite-root",
                    str(sqlite_root),
                ]
            )
            args.actual_scaling_root = str(Path(temp_dir) / "suite_d_fixed-actual")
            case_calls = []

            def fake_case_runner(case_args):
                # 这个覆写的目的不是改 result/log 目录，而是把高频写的 SQLite DB 单独放到另一个根目录。
                # 如果 sqlite_db 还落在 run_root 下面，就说明 /dev/shm 之类的隔离路径根本没生效。
                case_calls.append(
                    {
                        "run_root": case_args.run_root,
                        "sqlite_db": case_args.sqlite_db,
                    }
                )
                return {
                    "requested_run_root": str(Path(case_args.run_root)),
                    "actual_run_root": f"{case_args.run_root}-actual",
                    "sqlite_db": case_args.sqlite_db,
                    "wrk_metrics": {
                        "requests": 1000,
                        "requests_per_sec": 100000.0,
                        "offered_traces": 200000,
                        "latency_p95_ms": 0.7,
                        "latency_p99_ms": 1.2,
                    },
                    "online_completed_traces_per_sec": 9000.0,
                    "online_completion_ratio": 0.45,
                    "drain_tail_ms": 8000,
                    "drain_timeout": False,
                }

            summary = fixed_scaling_module.run_fixed_load_scaling(
                args,
                case_runner=fake_case_runner,
                line_writer=lambda _line: None,
            )
            saved = json.loads(output_summary.read_text(encoding="utf-8"))

            # 这些断言必须放在 TemporaryDirectory 生命周期内。
            # 否则 temp_dir 先被清理掉，目录不存在会变成测试自身的假失败，而不是 sqlite-root 逻辑失效。
            self.assertEqual(1, len(case_calls))
            sqlite_db_path = Path(case_calls[0]["sqlite_db"])
            run_root_path = Path(case_calls[0]["run_root"])
            self.assertTrue(str(sqlite_db_path).startswith(str(sqlite_root)))
            self.assertFalse(str(sqlite_db_path).startswith(str(run_root_path)))
            self.assertTrue(sqlite_db_path.parent.exists())
            self.assertEqual(str(sqlite_root), summary["fixed_load"]["sqlite_root"])
            self.assertEqual(str(sqlite_root), saved["fixed_load"]["sqlite_root"])


if __name__ == "__main__":
    unittest.main()
