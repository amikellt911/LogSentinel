#!/usr/bin/env python3

import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import run_suite_d_connection_search_summary as suite_d_summary_module
except ModuleNotFoundError:
    suite_d_summary_module = None


class SuiteDConnectionSearchSummaryUnitTest(unittest.TestCase):
    def test_build_summary_prefers_final_completion_ratio_over_online_peak(self) -> None:
        if suite_d_summary_module is None or not hasattr(
            suite_d_summary_module, "build_connection_search_summary"
        ):
            self.fail("build_connection_search_summary should exist for Suite D connection search summary")

        with tempfile.TemporaryDirectory() as temp_dir:
            search_root = Path(temp_dir)

            def write_case(
                name: str,
                connections: int,
                online: float,
                ratio: float,
                qps: float,
                final_trace_summary: int,
                offered_traces: int,
                drain_tail_ms: int,
            ) -> None:
                payload = {
                    "connections": connections,
                    "online_completed_traces_per_sec": online,
                    "online_completion_ratio": ratio,
                    "drain_tail_ms": drain_tail_ms,
                    "wrk_metrics": {
                        "requests_per_sec": qps,
                        "offered_traces": offered_traces,
                    },
                    "sqlite_counts_final": {
                        "trace_summary": final_trace_summary,
                    },
                }
                (search_root / f"{name}.json").write_text(
                    json.dumps(payload, ensure_ascii=True) + "\n",
                    encoding="utf-8",
                )

            # 90 这档故意让 online 更高，但 final completion 明显更差。
            # 如果 best 还选它，就说明连接搜索仍然在用旧口径误判 winner。
            write_case("r1_c090", 90, 9800.0, 0.5100, 154000.0, 241494, 288907, 8576)
            write_case("r2_c090", 90, 9720.0, 0.5008, 153100.0, 240880, 288500, 8400)
            write_case("r1_c108", 108, 9284.3, 0.4755, 155180.5, 292563, 292900, 11598)
            write_case("r2_c108", 108, 9310.0, 0.4762, 155050.0, 292200, 292780, 11610)

            summary = suite_d_summary_module.build_connection_search_summary(search_root, [90, 108])

        by_connections = {item["connections"]: item for item in summary["by_connections"]}
        self.assertIn(90, by_connections)
        self.assertIn(108, by_connections)
        self.assertIn("median_final_trace_summary", by_connections[90])
        self.assertIn("median_final_completion_ratio", by_connections[90])
        self.assertIn("median_drain_tail_ms", by_connections[90])
        self.assertEqual(108, summary["best"]["connections"])
        self.assertGreater(
            float(by_connections[108]["median_final_completion_ratio"]),
            float(by_connections[90]["median_final_completion_ratio"]),
        )
        self.assertLess(
            float(by_connections[108]["median_online_completed_traces_per_sec"]),
            float(by_connections[90]["median_online_completed_traces_per_sec"]),
        )

    def test_print_summary_lines_include_final_metrics(self) -> None:
        if suite_d_summary_module is None or not hasattr(
            suite_d_summary_module, "print_connection_search_summary"
        ):
            self.fail("print_connection_search_summary should exist for Suite D connection search summary")

        summary = {
            "by_connections": [
                {
                    "connections": 108,
                    "runs": 3,
                    "median_online_completed_traces_per_sec": 9284.33,
                    "median_online_completion_ratio": 0.4755,
                    "median_requests_per_sec": 155180.51,
                    "median_final_trace_summary": 292563.0,
                    "median_final_completion_ratio": 0.9988,
                    "median_drain_tail_ms": 11598,
                }
            ],
            "best": {
                "connections": 108,
                "median_online_completed_traces_per_sec": 9284.33,
                "median_online_completion_ratio": 0.4755,
                "median_final_trace_summary": 292563.0,
                "median_final_completion_ratio": 0.9988,
                "median_drain_tail_ms": 11598,
            },
        }

        buffer = io.StringIO()
        with redirect_stdout(buffer):
            suite_d_summary_module.print_connection_search_summary(summary)

        output = buffer.getvalue()
        self.assertIn("median_final=292563.00", output)
        self.assertIn("median_final_ratio=0.9988", output)
        self.assertIn("median_drain=11598", output)


if __name__ == "__main__":
    unittest.main()
