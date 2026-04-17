#!/usr/bin/env python3

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import run_suite_d_topology_search as search_module
except ModuleNotFoundError:
    search_module = None


def extract_cli_value(argv, option):
    return argv[argv.index(option) + 1]


class SuiteDRunSuiteDTopologySearchUnitTest(unittest.TestCase):
    def test_parse_args_defaults_to_three_frozen_candidates(self) -> None:
        if search_module is None or not hasattr(search_module, "parse_args"):
            self.fail("parse_args should exist for Suite D topology search runner")

        args = search_module.parse_args(
            [
                "--search-root",
                "/tmp/suite_d_topology",
                "--server-bin",
                "./server/build/LogSentinel",
            ]
        )

        self.assertEqual("/tmp/suite_d_topology", args.search_root)
        self.assertEqual(18180, args.port_base)
        self.assertEqual(4, args.sender_cores)
        self.assertEqual(20, args.backend_cores)
        self.assertEqual("0-3", args.wrk_cpuset)
        self.assertEqual("4-23", args.server_cpuset)
        self.assertEqual("off", args.ai_mode)
        self.assertEqual(search_module.DEFAULT_CANDIDATES, args.candidates)

    def test_candidate_sort_key_prefers_online_then_ratio_then_shorter_drain(self) -> None:
        if search_module is None or not hasattr(search_module, "candidate_sort_key"):
            self.fail("candidate_sort_key should exist for Suite D topology search runner")

        lower_online = {
            "candidate_name": "t1",
            "online_completed_traces_per_sec": 90.0,
            "online_completion_ratio": 0.9500,
            "drain_tail_ms": 100,
        }
        better_ratio = {
            "candidate_name": "t2",
            "online_completed_traces_per_sec": 93.4,
            "online_completion_ratio": 0.9200,
            "drain_tail_ms": 900,
        }
        better_drain = {
            "candidate_name": "t3",
            "online_completed_traces_per_sec": 93.4,
            "online_completion_ratio": 0.9200,
            "drain_tail_ms": 850,
        }

        ordered = sorted(
            [lower_online, better_ratio, better_drain],
            key=search_module.candidate_sort_key,
        )

        self.assertEqual(["t3", "t2", "t1"], [item["candidate_name"] for item in ordered])

    def test_run_topology_search_outputs_candidate_lines_and_top_candidates_summary(self) -> None:
        if search_module is None or not hasattr(search_module, "run_topology_search"):
            self.fail("run_topology_search should exist for Suite D topology search runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            output_summary = Path(temp_dir) / "summary.json"
            args = search_module.parse_args(
                [
                    "--search-root",
                    str(Path(temp_dir) / "suite_d_topology"),
                    "--output-summary",
                    str(output_summary),
                    "--server-bin",
                    "./server/build/LogSentinel",
                ]
            )
            args.actual_search_root = str(Path(temp_dir) / "suite_d_topology-actual")
            lines = []
            case_calls = []

            def fake_case_runner(case_args):
                run_root = Path(case_args.run_root)
                output_json_path = Path(case_args.output_json)
                self.assertTrue(output_json_path.parent.exists())

                case_calls.append(
                    {
                        "candidate_name": run_root.parts[-2],
                        "server_io_threads": case_args.server_io_threads,
                        "dispatch_worker_threads": case_args.dispatch_worker_threads,
                        "worker_threads": case_args.worker_threads,
                        "server_cpuset": case_args.server_cpuset,
                        "wrk_cpuset": case_args.wrk_cpuset,
                        "disable_ai": case_args.disable_ai,
                    }
                )

                candidate_name = run_root.parts[-2]
                base = {
                    "requested_run_root": str(run_root),
                    "actual_run_root": f"{run_root}-actual",
                    "wrk_metrics": {
                        "requests": 12184,
                        "requests_per_sec": 812.34,
                        "offered_traces": 1523,
                        "latency_p95_ms": 12.35,
                        "latency_p99_ms": 18.90,
                    },
                    "drain_timeout": False,
                }
                if candidate_name == "t1":
                    return {
                        **base,
                        "online_completed_traces_per_sec": 90.40,
                        "online_completion_ratio": 0.9100,
                        "drain_tail_ms": 930,
                    }
                if candidate_name == "t2":
                    return {
                        **base,
                        "online_completed_traces_per_sec": 93.40,
                        "online_completion_ratio": 0.9200,
                        "drain_tail_ms": 850,
                    }
                return {
                    **base,
                    "online_completed_traces_per_sec": 93.40,
                    "online_completion_ratio": 0.9150,
                    "drain_tail_ms": 820,
                }

            summary = search_module.run_topology_search(
                args,
                case_runner=fake_case_runner,
                line_writer=lines.append,
            )
            saved = json.loads(output_summary.read_text(encoding="utf-8"))

        self.assertEqual(3, len(case_calls))
        self.assertEqual("0-3", case_calls[0]["wrk_cpuset"])
        self.assertEqual("4-23", case_calls[0]["server_cpuset"])
        self.assertTrue(case_calls[0]["disable_ai"])
        self.assertEqual(
            "[candidate 2/3] name=t2 online=93.40 ratio=0.9200 drain=850 qps=812.34 winner_so_far=t2",
            lines[1],
        )
        self.assertEqual("t2", summary["top_candidates"][0]["candidate_name"])
        self.assertEqual("t2", saved["top_candidates"][0]["candidate_name"])
        self.assertEqual(3, len(saved["candidates"]))


if __name__ == "__main__":
    unittest.main()
