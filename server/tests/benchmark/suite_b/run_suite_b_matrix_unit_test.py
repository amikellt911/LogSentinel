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
                sequence.append(("launch", case["case_id"]))
                self.assertIn(case["trace_lifecycle_profile"], args.server_command)
                return {"pid": case["case_id"]}

            def fake_wait(_port: int, _timeout_sec: float) -> None:
                sequence.append(("wait", _port))

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
                server_command="python3 fake_server.py --profile {trace_lifecycle_profile} --db {sqlite_db} --port {port} > {log_path}",
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
                launch_server=fake_launch,
                wait_for_port=fake_wait,
                run_case=fake_run_case,
                stop_server=fake_stop,
            )

            saved = json.loads(Path(args.output_summary).read_text(encoding="utf-8"))

        self.assertEqual(4, result["total_cases"])
        self.assertEqual(4, len(result["cases"]))
        self.assertEqual(4, saved["total_cases"])
        self.assertEqual(("launch", "protected__clean_baseline"), sequence[0])
        self.assertIn(("case", "minimal", "mixed_realistic"), sequence)
        self.assertEqual(("stop", "minimal__mixed_realistic"), sequence[-1])


if __name__ == "__main__":
    unittest.main()
