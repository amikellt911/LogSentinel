#!/usr/bin/env python3

import json
import tempfile
import unittest
from pathlib import Path

try:
    import run_suite_a_scan as scan_module
except ModuleNotFoundError:
    scan_module = None


class SuiteARunSuiteAScanUnitTest(unittest.TestCase):
    def test_parse_args_defaults_to_three_gaps_and_forwards_case_args(self) -> None:
        if scan_module is None or not hasattr(scan_module, "parse_args"):
            self.fail("parse_args should exist for Suite A scan runner")

        args = scan_module.parse_args(
            [
                "--scan-root",
                "/tmp/suite_a_scan",
                "--port-base",
                "18180",
                "--server-command",
                "fake-server --db {sqlite_db} --port {port}",
                "--trace-count",
                "800",
                "--spans-per-trace",
                "8",
            ]
        )

        self.assertEqual("/tmp/suite_a_scan", args.scan_root)
        self.assertEqual([25, 20, 15], args.gap_values)
        self.assertEqual(5, args.repeats)
        self.assertEqual(18180, args.port_base)
        self.assertIn("--server-command", args.case_args)
        self.assertIn("--trace-count", args.case_args)

    def test_build_case_argv_uses_gap_repeat_and_isolated_run_prefix(self) -> None:
        if scan_module is None or not hasattr(scan_module, "build_case_argv"):
            self.fail("build_case_argv should exist for Suite A scan runner")

        args = scan_module.parse_args(
            [
                "--scan-root",
                "/tmp/suite_a_scan",
                "--gaps-ms",
                "25,20",
                "--repeats",
                "3",
                "--port-base",
                "18180",
                "--port-stride",
                "2",
                "--server-command",
                "fake-server --db {sqlite_db} --port {port}",
                "--trace-count",
                "800",
            ]
        )
        args.actual_scan_root = "/tmp/suite_a_scan-20260416-210000-000ms"

        # 这里锁的是 scan runner 和单 case runner 的边界：
        # scan 只负责给每一轮塞 gap、端口和 run-root 前缀，不替单 case runner接管其它参数。
        argv = scan_module.build_case_argv(args, gap_ms=20, repeat_index=2, global_run_index=4)

        self.assertEqual("20", argv[argv.index("--inter-trace-gap-ms") + 1])
        self.assertEqual("18188", argv[argv.index("--port-base") + 1])
        run_root = argv[argv.index("--run-root") + 1]
        self.assertEqual(
            "/tmp/suite_a_scan-20260416-210000-000ms/gap_020ms/run_03",
            run_root,
        )
        self.assertEqual(
            "/tmp/suite_a_scan-20260416-210000-000ms/gap_020ms/run_03/result.json",
            argv[argv.index("--output-json") + 1],
        )

    def test_run_suite_a_scan_aggregates_results_by_gap(self) -> None:
        if scan_module is None or not hasattr(scan_module, "run_suite_a_scan"):
            self.fail("run_suite_a_scan should exist for Suite A scan runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            output_summary = Path(temp_dir) / "scan_summary.json"
            args = scan_module.parse_args(
                [
                    "--scan-root",
                    str(Path(temp_dir) / "suite_a_scan"),
                    "--output-summary",
                    str(output_summary),
                    "--gaps-ms",
                    "25,20",
                    "--repeats",
                    "2",
                    "--port-base",
                    "18180",
                    "--server-command",
                    "fake-server --db {sqlite_db} --port {port}",
                    "--trace-count",
                    "800",
                    "--spans-per-trace",
                    "8",
                ]
            )
            args.actual_scan_root = str(Path(temp_dir) / "suite_a_scan-actual")

            case_calls = []

            def fake_case_runner(case_argv):
                gap_ms = int(case_argv[case_argv.index("--inter-trace-gap-ms") + 1])
                port_base = int(case_argv[case_argv.index("--port-base") + 1])
                run_root = case_argv[case_argv.index("--run-root") + 1]
                output_json = case_argv[case_argv.index("--output-json") + 1]
                # scan runner 显式指定嵌套 output-json 时，必须先把父目录建好。
                # 否则 run_suite_a_case 真写 result.json 时会直接 FileNotFoundError。
                self.assertTrue(Path(output_json).parent.exists())
                repeat_index = int(Path(run_root).name.split("_")[1])
                case_calls.append((gap_ms, repeat_index, port_base, run_root))

                visible_rate_by_gap = {
                    (25, 1): 0.95,
                    (25, 2): 0.97,
                    (20, 1): 0.90,
                    (20, 2): 0.88,
                }
                drain_tail_by_gap = {
                    (25, 1): 2100,
                    (25, 2): 2300,
                    (20, 1): 3200,
                    (20, 2): 3600,
                }
                timeout = gap_ms == 20 and repeat_index == 2
                return {
                    "requested_run_root": run_root,
                    "actual_run_root": f"{run_root}-actual",
                    "trace_count": 800,
                    "visible_completion_rate_at_stop": visible_rate_by_gap[(gap_ms, repeat_index)],
                    "drain_tail_ms": drain_tail_by_gap[(gap_ms, repeat_index)],
                    "drain_timeout": timeout,
                    "sqlite_counts_final": {"trace_summary": 800, "trace_span": 6400},
                    "output_json": output_json,
                }

            summary = scan_module.run_suite_a_scan(args, case_runner=fake_case_runner)
            saved = json.loads(output_summary.read_text(encoding="utf-8"))

        self.assertEqual(
            [
                (25, 1, 18180, case_calls[0][3]),
                (25, 2, 18181, case_calls[1][3]),
                (20, 1, 18182, case_calls[2][3]),
                (20, 2, 18183, case_calls[3][3]),
            ],
            case_calls,
        )
        self.assertEqual(4, summary["total_runs"])
        self.assertEqual([25, 20], summary["gaps_ms"])
        self.assertEqual(saved["actual_scan_root"], summary["actual_scan_root"])
        # summary.json 后面会直接打包带走，所以扫描汇总也必须自带实验上下文。
        self.assertIn("experiment_context", saved)
        self.assertIn("artifacts", saved)
        self.assertEqual("suite_a", saved["experiment_context"]["suite"])
        gap_25 = summary["aggregate"]["by_gap"]["25"]
        self.assertAlmostEqual(0.96, gap_25["visible_completion_rate_at_stop"]["mean"])
        self.assertEqual(2200.0, gap_25["drain_tail_ms"]["median"])
        gap_20 = summary["aggregate"]["by_gap"]["20"]
        self.assertEqual(0.5, gap_20["drain_timeout"]["timeout_rate"])


if __name__ == "__main__":
    unittest.main()
