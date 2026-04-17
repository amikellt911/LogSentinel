#!/usr/bin/env python3

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import run_suite_a_main_vs_cmp as compare_module
except ModuleNotFoundError:
    compare_module = None


def extract_cli_value(argv, option):
    return argv[argv.index(option) + 1]


class SuiteARunSuiteAMainVsCmpUnitTest(unittest.TestCase):
    def test_parse_args_defaults_to_three_story_load_points(self) -> None:
        if compare_module is None or not hasattr(compare_module, "parse_args"):
            self.fail("parse_args should exist for Suite A main-vs-cmp runner")

        # 这条测试锁的是“主叙事不是扫小参数，而是固定 tuned 参数去打三档负载点”。
        # 如果默认负载点被人随手改成单点，后面的论文口径就又会退回到偶然 case。
        args = compare_module.parse_args(
            [
                "--compare-root",
                "/tmp/suite_a_main_vs_cmp",
                "--main-server-bin",
                "./server/build/LogSentinel",
                "--cmp-server-bin",
                "./server/build-cmp/LogSentinel",
                "--spans-per-trace",
                "8",
            ]
        )

        self.assertEqual("/tmp/suite_a_main_vs_cmp", args.compare_root)
        self.assertEqual(3, args.repeats)
        self.assertEqual("protected", args.trace_lifecycle_profile)
        self.assertEqual(100, args.trace_sealed_grace_window_ms)
        self.assertEqual(200, args.trace_sweep_interval_ms)
        self.assertEqual(512, args.trace_primary_flush_span_threshold)
        self.assertEqual(5, args.trace_primary_flush_interval_ms)
        self.assertEqual(
            [
                {"label": "light", "trace_count": 800, "gap_ms": 20, "send_workers": 1},
                {"label": "mid", "trace_count": 1600, "gap_ms": 10, "send_workers": 2},
                {"label": "heavy", "trace_count": 3200, "gap_ms": 5, "send_workers": 2},
            ],
            args.load_points,
        )
        self.assertIn("--spans-per-trace", args.case_args)

    def test_run_compare_aggregates_by_load_and_prints_story_lines(self) -> None:
        if compare_module is None or not hasattr(compare_module, "run_suite_a_main_vs_cmp"):
            self.fail("run_suite_a_main_vs_cmp should exist for Suite A main-vs-cmp runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            output_summary = Path(temp_dir) / "summary.json"
            args = compare_module.parse_args(
                [
                    "--compare-root",
                    str(Path(temp_dir) / "suite_a_main_vs_cmp"),
                    "--output-summary",
                    str(output_summary),
                    "--repeats",
                    "2",
                    "--port-base",
                    "18180",
                    "--main-server-bin",
                    "./server/build/LogSentinel",
                    "--cmp-server-bin",
                    "./server/build-cmp/LogSentinel",
                    "--server-cpuset",
                    "2-3",
                    "--spans-per-trace",
                    "8",
                ]
            )
            args.actual_compare_root = str(Path(temp_dir) / "suite_a_main_vs_cmp-actual")

            case_calls = []
            lines = []

            def fake_case_runner(case_argv):
                server_command = extract_cli_value(case_argv, "--server-command")
                run_root = Path(extract_cli_value(case_argv, "--run-root"))
                output_json_path = Path(extract_cli_value(case_argv, "--output-json"))
                self.assertTrue(output_json_path.parent.exists())

                variant = "cmp_baseline" if "build-cmp" in server_command else "main_tuned"
                load_label = run_root.parts[-3]
                repeat_index = int(run_root.name.split("_")[1])
                trace_count = int(extract_cli_value(case_argv, "--trace-count"))
                gap_ms = int(extract_cli_value(case_argv, "--inter-trace-gap-ms"))
                send_workers = int(extract_cli_value(case_argv, "--send-workers"))
                port_base = int(extract_cli_value(case_argv, "--port-base"))

                if variant == "main_tuned":
                    self.assertIn("--trace-lifecycle-profile protected", server_command)
                    self.assertIn("--trace-sealed-grace-window-ms 100", server_command)
                    self.assertIn("--trace-sweep-interval-ms 200", server_command)
                    self.assertIn("--trace-primary-flush-span-threshold 512", server_command)
                    self.assertIn("--trace-primary-flush-interval-ms 5", server_command)
                else:
                    self.assertNotIn("--trace-primary-flush-span-threshold", server_command)
                    self.assertNotIn("--trace-lifecycle-profile", server_command)

                case_calls.append(
                    (
                        load_label,
                        variant,
                        trace_count,
                        gap_ms,
                        send_workers,
                        repeat_index,
                        port_base,
                    )
                )

                # 这组假数据故意让 main_tuned 在每档负载下都比旧版略差一点，
                # 这样测试才能锁住 summary 里的 delta 是按 main-cmp 计算的，而不是反过来。
                score_table = {
                    ("light", "cmp_baseline", 1): (1.00000, 0),
                    ("light", "cmp_baseline", 2): (1.00000, 1),
                    ("light", "main_tuned", 1): (0.99625, 152),
                    ("light", "main_tuned", 2): (0.99500, 153),
                    ("mid", "cmp_baseline", 1): (1.00000, 0),
                    ("mid", "cmp_baseline", 2): (1.00000, 0),
                    ("mid", "main_tuned", 1): (0.995625, 152),
                    ("mid", "main_tuned", 2): (0.995375, 152),
                    ("heavy", "cmp_baseline", 1): (1.00000, 0),
                    ("heavy", "cmp_baseline", 2): (0.999375, 1),
                    ("heavy", "main_tuned", 1): (0.99250, 202),
                    ("heavy", "main_tuned", 2): (0.99125, 203),
                }
                visible, drain = score_table[(load_label, variant, repeat_index)]
                return {
                    "requested_run_root": str(run_root),
                    "actual_run_root": f"{run_root}-actual",
                    "trace_count": trace_count,
                    "inter_trace_gap_ms": gap_ms,
                    "visible_completion_rate_at_stop": visible,
                    "drain_tail_ms": drain,
                    "drain_timeout": False,
                }

            summary = compare_module.run_suite_a_main_vs_cmp(
                args,
                case_runner=fake_case_runner,
                line_writer=lines.append,
            )
            saved = json.loads(output_summary.read_text(encoding="utf-8"))

        self.assertEqual(
            [
                ("light", "cmp_baseline", 800, 20, 1, 1, 18180),
                ("light", "cmp_baseline", 800, 20, 1, 2, 18181),
                ("light", "main_tuned", 800, 20, 1, 1, 18182),
                ("light", "main_tuned", 800, 20, 1, 2, 18183),
                ("mid", "cmp_baseline", 1600, 10, 2, 1, 18184),
                ("mid", "cmp_baseline", 1600, 10, 2, 2, 18185),
                ("mid", "main_tuned", 1600, 10, 2, 1, 18186),
                ("mid", "main_tuned", 1600, 10, 2, 2, 18187),
                ("heavy", "cmp_baseline", 3200, 5, 2, 1, 18188),
                ("heavy", "cmp_baseline", 3200, 5, 2, 2, 18189),
                ("heavy", "main_tuned", 3200, 5, 2, 1, 18190),
                ("heavy", "main_tuned", 3200, 5, 2, 2, 18191),
            ],
            case_calls,
        )
        self.assertEqual(3, len(summary["by_load"]))
        self.assertEqual("light", summary["by_load"][0]["label"])
        self.assertEqual("cmp_baseline", summary["by_load"][0]["winner"])
        self.assertAlmostEqual(-0.004375, summary["by_load"][0]["delta_main_vs_cmp"]["visible_completion_rate_at_stop"])
        self.assertEqual(152.0, summary["by_load"][1]["delta_main_vs_cmp"]["drain_tail_ms"])
        self.assertAlmostEqual(-0.0078125, summary["overall"]["worst_visible_delta_main_vs_cmp"])
        self.assertEqual(202.0, summary["overall"]["worst_drain_delta_main_vs_cmp"])
        self.assertEqual(summary["actual_compare_root"], saved["actual_compare_root"])
        self.assertEqual(4, len(lines))
        self.assertTrue(lines[0].startswith("[load 1/3]"))
        self.assertTrue(lines[-1].startswith("[overall]"))
        self.assertTrue(all(not line.lstrip().startswith("{") for line in lines))


if __name__ == "__main__":
    unittest.main()
