#!/usr/bin/env python3

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import run_suite_d_scaling_fixed_load as fixed_scaling_module
except ModuleNotFoundError:
    fixed_scaling_module = None


class SuiteDRunSuiteDScalingFixedLoadUnitTest(unittest.TestCase):
    def test_fixed_load_defaults_pin_sender_and_backend_points(self) -> None:
        if fixed_scaling_module is None:
            self.fail("run_suite_d_scaling_fixed_load module should exist")

        # fixed-load 曲线的核心是“输入负载不跟后端核数一起涨”。
        # 这里先锁默认值，避免后续又退化成旧的 sender/backend 联动 scaling。
        self.assertEqual([4, 8, 12, 16, 20, 24], fixed_scaling_module.DEFAULT_BACKEND_CORE_POINTS)
        self.assertEqual("0-3", fixed_scaling_module.DEFAULT_WRK_CPUSET)
        self.assertEqual(4, fixed_scaling_module.DEFAULT_WRK_THREADS)
        self.assertEqual(90, fixed_scaling_module.DEFAULT_CONNECTIONS)
        self.assertEqual(4, fixed_scaling_module.DEFAULT_BACKEND_CORE_OFFSET)

    def test_run_fixed_load_scaling_keeps_wrk_load_constant(self) -> None:
        if fixed_scaling_module is None or not hasattr(fixed_scaling_module, "run_fixed_load_scaling"):
            self.fail("run_fixed_load_scaling should exist for Suite D fixed-load runner")

        with tempfile.TemporaryDirectory() as temp_dir:
            output_summary = Path(temp_dir) / "summary.json"
            args = fixed_scaling_module.parse_args(
                [
                    "--scaling-root",
                    str(Path(temp_dir) / "suite_d_fixed"),
                    "--output-summary",
                    str(output_summary),
                    "--server-bin",
                    "./server/build/LogSentinel",
                    "--backend-core-points",
                    "4,8,12,16,20,24",
                    "--wrk-cpuset",
                    "0-3",
                    "--wrk-threads",
                    "4",
                    "--connections",
                    "90",
                    "--backend-core-offset",
                    "4",
                ]
            )
            args.actual_scaling_root = str(Path(temp_dir) / "suite_d_fixed-actual")
            lines = []
            case_calls = []

            def fake_case_runner(case_args):
                run_root = Path(case_args.run_root)
                backend_cores = int(run_root.parts[-2].split("_", 1)[1])
                # 这里记录 runner 派生出的真实运行参数。
                # 如果 wrk 负载或 sender 绑核随 backend_cores 改变，这条测试会直接红灯。
                case_calls.append(
                    {
                        "backend_cores": backend_cores,
                        "wrk_cpuset": case_args.wrk_cpuset,
                        "server_cpuset": case_args.server_cpuset,
                        "wrk_threads": case_args.wrk_threads,
                        "connections": case_args.connections,
                        "server_io_threads": case_args.server_io_threads,
                        "dispatch_worker_threads": case_args.dispatch_worker_threads,
                        "worker_threads": case_args.worker_threads,
                        "disable_ai": case_args.disable_ai,
                    }
                )
                return {
                    "requested_run_root": str(run_root),
                    "actual_run_root": f"{run_root}-actual",
                    "wrk_metrics": {
                        "requests": backend_cores * 1000,
                        "requests_per_sec": float(100000 + backend_cores),
                        "offered_traces": 200000,
                        "latency_p95_ms": 0.7,
                        "latency_p99_ms": 1.2,
                    },
                    "online_completed_traces_per_sec": float(8000 + backend_cores * 10),
                    "online_completion_ratio": 0.50 + backend_cores * 0.001,
                    "drain_tail_ms": 9000 - backend_cores * 100,
                    "drain_timeout": False,
                }

            summary = fixed_scaling_module.run_fixed_load_scaling(
                args,
                case_runner=fake_case_runner,
                line_writer=lines.append,
            )
            saved = json.loads(output_summary.read_text(encoding="utf-8"))

        self.assertEqual(6, len(case_calls))
        self.assertEqual("0-3", case_calls[0]["wrk_cpuset"])
        self.assertEqual("0-3", case_calls[-1]["wrk_cpuset"])
        self.assertEqual("4-7", case_calls[0]["server_cpuset"])
        self.assertEqual("4-27", case_calls[-1]["server_cpuset"])
        self.assertEqual({4}, {item["wrk_threads"] for item in case_calls})
        self.assertEqual({90}, {item["connections"] for item in case_calls})
        self.assertTrue(all(item["disable_ai"] for item in case_calls))
        self.assertEqual(
            "[point 6/6] backend=24 wrk=0-3 server=4-27 online=8240.00 ratio=0.5240 drain=6600 qps=100024.00",
            lines[-1],
        )
        self.assertEqual(24, summary["overall"]["best_backend_cores"])
        self.assertEqual(24, saved["overall"]["best_backend_cores"])
        self.assertEqual(6, len(saved["by_backend_cores"]))
        self.assertEqual("0-3", saved["experiment_context"]["cpu_allocation"]["wrk_cpuset"])
        self.assertEqual("4-27", saved["by_backend_cores"][-1]["server_cpuset"])
        self.assertEqual(90, saved["experiment_context"]["workload"]["connections"])


if __name__ == "__main__":
    unittest.main()
