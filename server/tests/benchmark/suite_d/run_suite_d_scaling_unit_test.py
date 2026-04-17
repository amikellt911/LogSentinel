#!/usr/bin/env python3

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import run_suite_d_scaling as scaling_module
except ModuleNotFoundError:
    scaling_module = None


class SuiteDRunSuiteDScalingUnitTest(unittest.TestCase):
    def test_frozen_core_points_and_topology_maps_exist(self) -> None:
        if scaling_module is None:
            self.fail("run_suite_d_scaling module should exist")

        # 先锁死冻结后的主曲线骨架，避免后面有人把总核数点位或比例映射悄悄改漂。
        self.assertEqual([4, 8, 12, 16, 20, 24], scaling_module.DEFAULT_TOTAL_CORE_POINTS)
        self.assertEqual({"sender_cores": 1, "backend_cores": 3}, scaling_module.DEFAULT_CORE_SPLIT[4])
        self.assertEqual({"sender_cores": 4, "backend_cores": 20}, scaling_module.DEFAULT_CORE_SPLIT[24])
        self.assertEqual(
            {
                "server_io_threads": 4,
                "dispatch_worker_threads": 3,
                "worker_threads": 24,
            },
            scaling_module.DEFAULT_TOPOLOGY_MAP[16],
        )

    def test_run_scaling_builds_summary_from_frozen_core_points(self) -> None:
        if scaling_module is None or not hasattr(scaling_module, "run_scaling"):
            self.fail("run_scaling should exist for Suite D scaling runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            output_summary = Path(temp_dir) / "summary.json"
            args = scaling_module.parse_args(
                [
                    "--scaling-root",
                    str(Path(temp_dir) / "suite_d_scaling"),
                    "--output-summary",
                    str(output_summary),
                    "--server-bin",
                    "./server/build/LogSentinel",
                    "--core-base-offset",
                    "160",
                ]
            )
            args.actual_scaling_root = str(Path(temp_dir) / "suite_d_scaling-actual")
            lines = []
            case_calls = []

            def fake_case_runner(case_args):
                run_root = Path(case_args.run_root)
                total_cores = int(run_root.parts[-2].split("_", 1)[1])
                # 这里显式记录 runner 派生出来的部署参数，专门验证“按总核数自动缩放”的逻辑。
                case_calls.append(
                    {
                        "total_cores": total_cores,
                        "wrk_cpuset": case_args.wrk_cpuset,
                        "server_cpuset": case_args.server_cpuset,
                        "wrk_threads": case_args.wrk_threads,
                        "connections": case_args.connections,
                        "worker_queue_size": case_args.worker_queue_size,
                        "trace_active_session_limit": case_args.trace_active_session_limit,
                        "trace_buffered_span_limit": case_args.trace_buffered_span_limit,
                        "disable_ai": case_args.disable_ai,
                    }
                )
                return {
                    "requested_run_root": str(run_root),
                    "actual_run_root": f"{run_root}-actual",
                    "wrk_metrics": {
                        "requests": total_cores * 100,
                        "requests_per_sec": float(total_cores * 10),
                        "offered_traces": total_cores * 12,
                        "latency_p95_ms": 20.0 - total_cores * 0.2,
                        "latency_p99_ms": 30.0 - total_cores * 0.2,
                    },
                    "online_completed_traces_per_sec": float(total_cores * 8),
                    "online_completion_ratio": 0.70 + total_cores * 0.01,
                    "drain_tail_ms": 2000 - total_cores * 40,
                    "drain_timeout": False,
                }

            summary = scaling_module.run_scaling(args, case_runner=fake_case_runner, line_writer=lines.append)
            saved = json.loads(output_summary.read_text(encoding="utf-8"))

        # 最后一个点位必须能还原出“从偏移核位开始”的 sender/backend 绑核关系。
        # 这条红灯专门防止 scaling runner 再把 cpuset 偷偷写死成 0 起始，导致云机容器一启动就炸。
        self.assertEqual(6, len(case_calls))
        self.assertEqual("160", case_calls[0]["wrk_cpuset"])
        self.assertEqual("161-163", case_calls[0]["server_cpuset"])
        self.assertEqual("160-163", case_calls[-1]["wrk_cpuset"])
        self.assertEqual("164-183", case_calls[-1]["server_cpuset"])
        self.assertEqual(1, case_calls[0]["wrk_threads"])
        self.assertEqual(30, case_calls[0]["connections"])
        self.assertEqual(4, case_calls[-1]["wrk_threads"])
        self.assertEqual(120, case_calls[-1]["connections"])
        self.assertEqual(4096, case_calls[0]["worker_queue_size"])
        self.assertEqual(512, case_calls[0]["trace_active_session_limit"])
        self.assertEqual(4096, case_calls[0]["trace_buffered_span_limit"])
        self.assertEqual(2048, case_calls[-1]["trace_active_session_limit"])
        self.assertEqual(16384, case_calls[-1]["trace_buffered_span_limit"])
        self.assertTrue(all(item["disable_ai"] for item in case_calls))
        # stdout 只保留一行压缩摘要，后续 wrapper 批跑时终端不会被大块 JSON 淹掉。
        self.assertEqual(
            "[point 6/6] total=24 sender=4 backend=20 online=192.00 ratio=0.9400 drain=1040 qps=240.00",
            lines[-1],
        )
        self.assertEqual(24, summary["overall"]["best_total_cores"])
        self.assertEqual(24, saved["overall"]["best_total_cores"])
        self.assertEqual(6, len(saved["by_total_cores"]))
        self.assertIn("experiment_context", saved)
        self.assertIn("artifacts", saved)
        self.assertEqual("suite_d", saved["experiment_context"]["suite"])
        self.assertEqual(160, saved["experiment_context"]["thread_topology"]["core_base_offset"])


if __name__ == "__main__":
    unittest.main()
