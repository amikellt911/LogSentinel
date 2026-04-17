#!/usr/bin/env python3

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import run_suite_a_buffer_compare as compare_module
except ModuleNotFoundError:
    compare_module = None


def extract_cli_value(argv, option):
    return argv[argv.index(option) + 1]


class SuiteARunSuiteABufferCompareUnitTest(unittest.TestCase):
    def test_parse_args_defaults_to_protected_same_baseline_compare(self) -> None:
        if compare_module is None or not hasattr(compare_module, "parse_args"):
            self.fail("parse_args should exist for Suite A buffer compare runner")

        # compare runner 的职责不是继续搜参数，而是钉死 protected 基线后做同口径对比。
        # 这里锁默认值，就是为了防止后面又把 lifecycle/minimal 或别的搜索变量偷偷混回来。
        args = compare_module.parse_args(
            [
                "--compare-root",
                "/tmp/suite_a_buffer_compare",
                "--port-base",
                "18180",
                "--server-bin",
                "./server/build/LogSentinel",
                "--trace-count",
                "800",
                "--spans-per-trace",
                "8",
            ]
        )

        self.assertEqual("/tmp/suite_a_buffer_compare", args.compare_root)
        self.assertEqual(20, args.gap_ms)
        self.assertEqual(5, args.repeats)
        self.assertEqual("protected", args.trace_lifecycle_profile)
        self.assertEqual(100, args.trace_sealed_grace_window_ms)
        self.assertEqual(200, args.trace_sweep_interval_ms)
        self.assertEqual([512, 128, 64], args.buffered_span_threshold_values)
        self.assertEqual([5], args.buffered_flush_interval_values)
        self.assertIn("--server-bin", args.case_args)
        self.assertIn("--trace-count", args.case_args)

    def test_run_compare_orders_cases_and_prints_only_case_lines_plus_topk(self) -> None:
        if compare_module is None or not hasattr(compare_module, "run_suite_a_buffer_compare"):
            self.fail("run_suite_a_buffer_compare should exist for Suite A buffer compare runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            output_summary = Path(temp_dir) / "summary.json"
            args = compare_module.parse_args(
                [
                    "--compare-root",
                    str(Path(temp_dir) / "suite_a_buffer_compare"),
                    "--output-summary",
                    str(output_summary),
                    "--repeats",
                    "2",
                    "--port-base",
                    "18180",
                    "--server-bin",
                    "./server/build/LogSentinel",
                    "--trace-count",
                    "800",
                    "--spans-per-trace",
                    "8",
                ]
            )
            args.actual_compare_root = str(Path(temp_dir) / "suite_a_buffer_compare-actual")

            case_calls = []
            lines = []

            def fake_case_runner(case_argv):
                run_root = Path(extract_cli_value(case_argv, "--run-root"))
                output_json_path = Path(extract_cli_value(case_argv, "--output-json"))
                self.assertTrue(output_json_path.parent.exists())

                mode = "disable_buffered" if "--disable-buffered-trace-repo" in case_argv else "buffered"
                span_threshold = (
                    0
                    if mode == "disable_buffered"
                    else int(extract_cli_value(case_argv, "--trace-primary-flush-span-threshold"))
                )
                flush_interval_ms = (
                    0
                    if mode == "disable_buffered"
                    else int(extract_cli_value(case_argv, "--trace-primary-flush-interval-ms"))
                )
                port_base = int(extract_cli_value(case_argv, "--port-base"))
                repeat_index = int(run_root.name.split("_")[1])

                case_calls.append(
                    (
                        mode,
                        span_threshold,
                        flush_interval_ms,
                        repeat_index,
                        port_base,
                    )
                )

                # 这组假数据专门把“visible 更高但 drain 略长”的候选放进来，
                # 用来锁排序规则必须先看 visible，再看 drain。
                score_table = {
                    ("disable_buffered", 0, 0, 1): (0.99125, 210),
                    ("disable_buffered", 0, 0, 2): (0.99000, 205),
                    ("buffered", 512, 5, 1): (0.99625, 152),
                    ("buffered", 512, 5, 2): (0.99500, 150),
                    ("buffered", 128, 5, 1): (0.99625, 151),
                    ("buffered", 128, 5, 2): (0.99625, 153),
                    ("buffered", 64, 5, 1): (0.99250, 149),
                    ("buffered", 64, 5, 2): (0.99375, 152),
                }
                visible, drain = score_table[(mode, span_threshold, flush_interval_ms, repeat_index)]
                return {
                    "requested_run_root": str(run_root),
                    "actual_run_root": f"{run_root}-actual",
                    "trace_count": 800,
                    "inter_trace_gap_ms": 20,
                    "visible_completion_rate_at_stop": visible,
                    "drain_tail_ms": drain,
                    "drain_timeout": False,
                    "sqlite_counts_final": {
                        "trace_summary": 800,
                        "trace_span": 6400,
                    },
                }

            summary = compare_module.run_suite_a_buffer_compare(
                args,
                case_runner=fake_case_runner,
                line_writer=lines.append,
            )
            saved = json.loads(output_summary.read_text(encoding="utf-8"))

        self.assertEqual(
            [
                ("disable_buffered", 0, 0, 1, 18180),
                ("disable_buffered", 0, 0, 2, 18181),
                ("buffered", 512, 5, 1, 18182),
                ("buffered", 512, 5, 2, 18183),
                ("buffered", 128, 5, 1, 18184),
                ("buffered", 128, 5, 2, 18185),
                ("buffered", 64, 5, 1, 18186),
                ("buffered", 64, 5, 2, 18187),
            ],
            case_calls,
        )
        self.assertEqual(4, summary["candidate_count"])
        self.assertEqual(8, summary["total_case_runs"])
        self.assertEqual("disable_buffered", summary["candidates"][0]["candidate_mode"])
        self.assertEqual("buffered", summary["top_candidates"][0]["candidate_mode"])
        self.assertEqual(128, summary["top_candidates"][0]["trace_primary_flush_span_threshold"])
        self.assertEqual(512, summary["top_candidates"][1]["trace_primary_flush_span_threshold"])
        self.assertEqual(summary["actual_compare_root"], saved["actual_compare_root"])
        self.assertIn("experiment_context", saved)
        self.assertIn("artifacts", saved)
        self.assertEqual("suite_a", saved["experiment_context"]["suite"])
        self.assertEqual(6, len(lines))
        self.assertTrue(lines[0].startswith("[case 1/4]"))
        self.assertTrue(lines[-2].startswith("[top1]"))
        self.assertTrue(lines[-1].startswith("[top2]"))
        self.assertTrue(all(not line.lstrip().startswith("{") for line in lines))


if __name__ == "__main__":
    unittest.main()
