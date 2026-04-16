#!/usr/bin/env python3

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import run_suite_a_search_stage1 as search_module
except ModuleNotFoundError:
    search_module = None


def extract_cli_value(argv, option):
    return argv[argv.index(option) + 1]


class SuiteARunSuiteASearchStage1UnitTest(unittest.TestCase):
    def test_parse_args_rejects_controlled_case_args(self) -> None:
        if search_module is None or not hasattr(search_module, "parse_args"):
            self.fail("parse_args should exist for Suite A Stage 1 search runner")

        # Stage 1 要自己切 lifecycle / sweep / flush / AI on-off。
        # 如果还允许调用方把这些变量塞进 case_args，最终跑出来的就不是搜索，而是参数互相覆盖。
        with self.assertRaisesRegex(ValueError, "--trace-primary-flush-span-threshold"):
            search_module.parse_args(
                [
                    "--server-bin",
                    "./server/build/LogSentinel",
                    "--trace-count",
                    "800",
                    "--trace-primary-flush-span-threshold",
                    "64",
                ]
            )

    def test_parse_args_defaults_to_stage1_search_space_and_forwards_case_args(self) -> None:
        if search_module is None or not hasattr(search_module, "parse_args"):
            self.fail("parse_args should exist for Suite A Stage 1 search runner")

        args = search_module.parse_args(
            [
                "--search-root",
                "/tmp/suite_a_stage1",
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

        self.assertEqual("/tmp/suite_a_stage1", args.search_root)
        self.assertEqual(20, args.gap_ms)
        self.assertEqual(1, args.repeats)
        self.assertEqual(["protected", "minimal"], args.phase_a_lifecycle_profiles)
        self.assertEqual([500, 200, 100], args.phase_a_sweep_ms_values)
        self.assertEqual([512, 256, 128, 64], args.phase_b_span_thresholds)
        self.assertEqual([200, 50, 5], args.phase_b_flush_interval_ms_values)
        self.assertEqual(3, args.phase_c_top_k)
        self.assertEqual(160, args.phase_c_trace_count)
        self.assertIn("--server-bin", args.case_args)
        self.assertIn("--trace-count", args.case_args)

    def test_run_stage1_search_orders_cases_and_prints_only_case_lines_plus_topk(self) -> None:
        if search_module is None or not hasattr(search_module, "run_stage1_search"):
            self.fail("run_stage1_search should exist for Suite A Stage 1 search runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            output_summary = Path(temp_dir) / "summary.json"
            args = search_module.parse_args(
                [
                    "--search-root",
                    str(Path(temp_dir) / "suite_a_stage1"),
                    "--output-summary",
                    str(output_summary),
                    "--phase-c-top-k",
                    "2",
                    "--phase-c-trace-count",
                    "160",
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
            args.actual_search_root = str(Path(temp_dir) / "suite_a_stage1-actual")

            case_calls = []
            lines = []

            def fake_case_runner(case_argv):
                run_root = Path(extract_cli_value(case_argv, "--run-root"))
                output_json_path = Path(extract_cli_value(case_argv, "--output-json"))
                self.assertTrue(output_json_path.parent.exists())

                phase = run_root.parts[-2]
                lifecycle = extract_cli_value(case_argv, "--trace-lifecycle-profile")
                sweep_ms = int(extract_cli_value(case_argv, "--trace-sweep-interval-ms"))
                span_threshold = int(extract_cli_value(case_argv, "--trace-primary-flush-span-threshold"))
                flush_interval_ms = int(extract_cli_value(case_argv, "--trace-primary-flush-interval-ms"))
                trace_count = int(extract_cli_value(case_argv, "--trace-count"))
                port_base = int(extract_cli_value(case_argv, "--port-base"))
                ai_mode = "off" if "--disable-ai" in case_argv else "on"

                case_calls.append(
                    (
                        phase,
                        lifecycle,
                        sweep_ms,
                        span_threshold,
                        flush_interval_ms,
                        trace_count,
                        ai_mode,
                        port_base,
                    )
                )

                if phase == "phase_a":
                    phase_a_scores = {
                        ("protected", 500): (0.94625, 1213),
                        ("protected", 200): (0.95250, 1030),
                        ("protected", 100): (0.96125, 910),
                        ("minimal", 500): (0.96875, 830),
                        ("minimal", 200): (0.97750, 710),
                        ("minimal", 100): (0.98375, 620),
                    }
                    visible, drain = phase_a_scores[(lifecycle, sweep_ms)]
                elif phase == "phase_b":
                    phase_b_scores = {
                        (512, 200): (0.98375, 620),
                        (512, 50): (0.98500, 540),
                        (512, 5): (0.98625, 500),
                        (256, 200): (0.98625, 490),
                        (256, 50): (0.98750, 430),
                        (256, 5): (0.98875, 350),
                        (128, 200): (0.98875, 320),
                        (128, 50): (0.99000, 260),
                        (128, 5): (0.99250, 180),
                        (64, 200): (0.98900, 300),
                        (64, 50): (0.99125, 220),
                        (64, 5): (0.99400, 140),
                    }
                    visible, drain = phase_b_scores[(span_threshold, flush_interval_ms)]
                else:
                    phase_c_scores = {
                        (64, 5): (0.99000, 220),
                        (128, 5): (0.98875, 260),
                    }
                    visible, drain = phase_c_scores[(span_threshold, flush_interval_ms)]

                return {
                    "requested_run_root": str(run_root),
                    "actual_run_root": f"{run_root}-actual",
                    "trace_count": trace_count,
                    "inter_trace_gap_ms": 20,
                    "visible_completion_rate_at_stop": visible,
                    "drain_tail_ms": drain,
                    "drain_timeout": False,
                    "sqlite_counts_final": {
                        "trace_summary": trace_count,
                        "trace_span": trace_count * 8,
                    },
                }

            summary = search_module.run_stage1_search(
                args,
                case_runner=fake_case_runner,
                line_writer=lines.append,
            )
            saved = json.loads(output_summary.read_text(encoding="utf-8"))

        self.assertEqual(20, len(case_calls))
        self.assertEqual(
            [
                ("phase_a", "protected", 500, 512, 200, 800, "off", 18180),
                ("phase_a", "protected", 200, 512, 200, 800, "off", 18181),
                ("phase_a", "protected", 100, 512, 200, 800, "off", 18182),
                ("phase_a", "minimal", 500, 512, 200, 800, "off", 18183),
                ("phase_a", "minimal", 200, 512, 200, 800, "off", 18184),
                ("phase_a", "minimal", 100, 512, 200, 800, "off", 18185),
            ],
            case_calls[:6],
        )
        self.assertTrue(all(call[6] == "off" for call in case_calls[:18]))
        self.assertEqual(
            [
                ("phase_c", "minimal", 100, 64, 5, 160, "on", 18198),
                ("phase_c", "minimal", 100, 128, 5, 160, "on", 18199),
            ],
            case_calls[18:],
        )
        self.assertEqual("minimal", summary["phase_a"]["best_candidate"]["trace_lifecycle_profile"])
        self.assertEqual(100, summary["phase_a"]["best_candidate"]["trace_sweep_interval_ms"])
        self.assertEqual(64, summary["phase_b"]["top_candidates"][0]["trace_primary_flush_span_threshold"])
        self.assertEqual(5, summary["phase_b"]["top_candidates"][0]["trace_primary_flush_interval_ms"])
        self.assertEqual(160, summary["phase_c"]["smoke_candidates"][0]["trace_count"])
        self.assertEqual("on", summary["phase_c"]["smoke_candidates"][0]["ai_mode"])
        self.assertEqual(summary["actual_search_root"], saved["actual_search_root"])
        self.assertEqual(22, len(lines))
        self.assertTrue(lines[0].startswith("[phase_a 1/6]"))
        self.assertTrue(lines[-2].startswith("[top1]"))
        self.assertTrue(lines[-1].startswith("[top2]"))
        self.assertTrue(all(not line.lstrip().startswith("{") for line in lines))


if __name__ == "__main__":
    unittest.main()
