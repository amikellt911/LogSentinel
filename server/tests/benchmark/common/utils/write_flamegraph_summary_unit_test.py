#!/usr/bin/env python3

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import write_flamegraph_summary as flamegraph_module
except ModuleNotFoundError:
    flamegraph_module = None


class WriteFlamegraphSummaryUnitTest(unittest.TestCase):
    def test_build_summary_reads_run_summary_log_and_attaches_metadata(self) -> None:
        if flamegraph_module is None or not hasattr(flamegraph_module, "build_summary"):
            self.fail("build_summary should exist for flamegraph summary writer")

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            run_summary_log = temp_root / "run-summary.log"
            run_summary_log.write_text(
                "\n".join(
                    [
                        "bench_suite=suite_d",
                        "profile=end",
                        "port=18400",
                        "server_cpuset=4-23",
                        "wrk_cpuset=0-3",
                        "server_io_threads=6",
                        "worker_threads=32",
                        "dispatch_worker_threads=4",
                        "duration=15s",
                        "connections=120",
                        "wrk_threads=4",
                        "trace_summary_count=1523",
                        "trace_span_count=12184",
                        "trace_span_stats=8|8.0|8",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )

            summary = flamegraph_module.build_summary(
                bench_suite="suite_d",
                entry_script="/tmp/run_flamegraph_case.sh",
                run_summary_log=run_summary_log,
                output_json=temp_root / "run-summary.json",
                run_dir=temp_root,
                server_log=temp_root / "server.log",
                warmup_log=temp_root / "warmup.log",
                wrk_log=temp_root / "wrk.log",
                perf_data=temp_root / "perf.data",
                perf_script=temp_root / "perf.script",
                flame_svg=temp_root / "trace.svg",
                trace_db=temp_root / "trace-flame.db",
            )

        self.assertEqual("suite_d", summary["bench_suite"])
        self.assertEqual("end", summary["profile"])
        self.assertEqual(1523, summary["trace_db_snapshot"]["trace_summary_count"])
        self.assertEqual(12184, summary["trace_db_snapshot"]["trace_span_count"])
        self.assertEqual("suite_d", summary["experiment_context"]["suite"])
        self.assertEqual("4-23", summary["experiment_context"]["cpu_allocation"]["server_cpuset"])
        self.assertIn("flamegraph_svg", summary["artifacts"])


if __name__ == "__main__":
    unittest.main()
