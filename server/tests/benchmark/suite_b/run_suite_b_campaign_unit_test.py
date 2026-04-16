#!/usr/bin/env python3

import json
import tempfile
import unittest
from pathlib import Path

try:
    import run_suite_b_campaign as campaign_module
except ModuleNotFoundError:
    campaign_module = None


class SuiteBRunSuiteBCampaignUnitTest(unittest.TestCase):
    def test_parse_args_defaults_to_five_fixed_seeds_and_forwards_matrix_args(self) -> None:
        if campaign_module is None or not hasattr(campaign_module, "parse_args"):
            self.fail("parse_args should exist for Suite B campaign runner")

        args = campaign_module.parse_args(
            [
                "--campaign-root",
                "/tmp/suite_b_campaign",
                "--port-base",
                "19680",
                "--server-bin",
                "./server/build/LogSentinel",
                "--disable-ai",
                "--disable-webhook",
            ]
        )

        self.assertEqual([20260415, 20260416, 20260417, 20260418, 20260419], args.seed_values)
        self.assertEqual("/tmp/suite_b_campaign", args.campaign_root)
        self.assertEqual(19680, args.port_base)
        self.assertIn("--server-bin", args.matrix_args)
        self.assertIn("--disable-ai", args.matrix_args)
        self.assertIn("--disable-webhook", args.matrix_args)

    def test_build_matrix_argv_uses_seed_port_stride_and_isolated_run_prefix(self) -> None:
        if campaign_module is None or not hasattr(campaign_module, "build_matrix_argv"):
            self.fail("build_matrix_argv should exist for Suite B campaign runner")

        args = campaign_module.parse_args(
            [
                "--campaign-root",
                "/tmp/suite_b_campaign",
                "--seeds",
                "11,22",
                "--port-base",
                "21000",
                "--port-stride",
                "20",
                "--server-cpuset",
                "3-15",
            ]
        )
        args.actual_campaign_root = "/tmp/suite_b_campaign-20260416-110000-000ms"

        # 这里锁的是 campaign runner 和 matrix runner 的边界：
        # campaign 只负责给每一轮塞 seed、端口和 run-root 前缀，其它后端资源参数原样交给 matrix。
        argv = campaign_module.build_matrix_argv(args, seed=22, run_index=1)

        self.assertIn("--server-cpuset", argv)
        self.assertIn("3-15", argv)
        self.assertEqual("22", argv[argv.index("--seed") + 1])
        self.assertEqual("21020", argv[argv.index("--port-base") + 1])
        run_root = argv[argv.index("--run-root") + 1]
        self.assertEqual(
            "/tmp/suite_b_campaign-20260416-110000-000ms/run_02_seed_22",
            run_root,
        )

    def test_run_suite_b_campaign_aggregates_run_level_results(self) -> None:
        if campaign_module is None or not hasattr(campaign_module, "run_suite_b_campaign"):
            self.fail("run_suite_b_campaign should exist for Suite B campaign runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            output_summary = Path(temp_dir) / "campaign_summary.json"
            args = campaign_module.parse_args(
                [
                    "--campaign-root",
                    str(Path(temp_dir) / "suite_b_campaign"),
                    "--output-summary",
                    str(output_summary),
                    "--seeds",
                    "101,202,303",
                    "--port-base",
                    "22000",
                    "--port-stride",
                    "10",
                    "--trace-count",
                    "10",
                ]
            )
            args.actual_campaign_root = str(Path(temp_dir) / "suite_b_campaign-actual")

            matrix_calls = []

            def fake_matrix_runner(matrix_argv):
                seed = int(matrix_argv[matrix_argv.index("--seed") + 1])
                port_base = int(matrix_argv[matrix_argv.index("--port-base") + 1])
                run_root = matrix_argv[matrix_argv.index("--run-root") + 1]
                matrix_calls.append((seed, port_base, run_root))
                # 这里故意让 protected 的 completeness 随 seed 变化，
                # 用来证明 campaign 聚合的是 run-level 结果，而不是只保留最后一次。
                completeness = {101: 1.0, 202: 0.8, 303: 0.6}[seed]
                return {
                    "total_cases": 2,
                    "requested_run_root": run_root,
                    "actual_run_root": f"{run_root}-matrix",
                    "ingest_p95_latency_delta_by_profile": {
                        "late_replay_stress": {
                            "absolute_ms": {101: 1.0, 202: 3.0, 303: 5.0}[seed],
                            "relative": 0.1,
                        }
                    },
                    "sqlite_unique_constraint_fail_count_by_case": {
                        "minimal__late_replay_stress": {101: 2, 202: 4, 303: 6}[seed],
                    },
                    "cases": [
                        {
                            "case_id": "protected__late_replay_stress",
                            "trace_completeness_rate": {"value": completeness},
                            "trace_pollution_rate": {"value": 0.0},
                            "duplicate_persistence_rate": {"value": 0.0},
                        },
                        {
                            "case_id": "minimal__late_replay_stress",
                            "trace_completeness_rate": {"value": 0.1},
                            "trace_pollution_rate": {"value": 0.2},
                            "duplicate_persistence_rate": {"value": 0.3},
                        },
                    ],
                }

            summary = campaign_module.run_suite_b_campaign(args, matrix_runner=fake_matrix_runner)
            saved = json.loads(output_summary.read_text(encoding="utf-8"))

        self.assertEqual([(101, 22000, matrix_calls[0][2]), (202, 22010, matrix_calls[1][2]), (303, 22020, matrix_calls[2][2])], matrix_calls)
        self.assertEqual(3, summary["total_runs"])
        self.assertEqual([101, 202, 303], summary["seeds"])
        self.assertEqual(saved["actual_campaign_root"], summary["actual_campaign_root"])
        protected_stats = summary["aggregate"]["correctness_by_case"]["protected__late_replay_stress"]["trace_completeness_rate"]
        self.assertAlmostEqual(0.8, protected_stats["mean"])
        self.assertEqual(0.6, protected_stats["min"])
        self.assertEqual(1.0, protected_stats["max"])
        p95_stats = summary["aggregate"]["ingest_p95_latency_delta_by_profile"]["late_replay_stress"]["absolute_ms"]
        self.assertEqual(3.0, p95_stats["median"])
        unique_stats = summary["aggregate"]["sqlite_unique_constraint_fail_count_by_case"]["minimal__late_replay_stress"]
        self.assertEqual(4.0, unique_stats["mean"])


if __name__ == "__main__":
    unittest.main()
