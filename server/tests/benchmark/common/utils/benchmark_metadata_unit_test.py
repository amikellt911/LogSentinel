#!/usr/bin/env python3

import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import benchmark_metadata as metadata_module
except ModuleNotFoundError:
    metadata_module = None


class BenchmarkMetadataUnitTest(unittest.TestCase):
    def test_parse_cpuset_counts_ranges_and_lists(self) -> None:
        if metadata_module is None or not hasattr(metadata_module, "parse_cpuset"):
            self.fail("parse_cpuset should exist for benchmark metadata helper")

        # benchmark wrapper 里大量使用 0-3,4-23 这种 cpuset 写法。
        # 这里先把 range/list 解析锁住，避免后面 total_cores_used 统计直接算错。
        parsed = metadata_module.parse_cpuset("0-2,4,6-7")

        self.assertEqual({0, 1, 2, 4, 6, 7}, parsed)
        self.assertEqual(6, metadata_module.count_cpuset_cores("0-2,4,6-7"))

    def test_collect_machine_info_uses_runtime_commands_and_lscpu_fallbacks(self) -> None:
        if metadata_module is None or not hasattr(metadata_module, "collect_machine_info"):
            self.fail("collect_machine_info should exist for benchmark metadata helper")

        command_outputs = {
            "hostname": "bench-host\n",
            "uname -a": "Linux bench-host 6.8.0 test x86_64 GNU/Linux\n",
            # 故意让 lscpu 失败，锁住 helper 必须继续走 nproc 和 /proc/cpuinfo 回退。
            "nproc --all": "24\n",
            "cat /proc/cpuinfo": "model name\t: Fancy CPU 9000\n",
        }

        def fake_runner(command):
            command_text = " ".join(command)
            if command_text == "lscpu":
                raise RuntimeError("lscpu missing")
            return command_outputs[command_text]

        info = metadata_module.collect_machine_info(command_runner=fake_runner)

        self.assertEqual("bench-host", info["hostname"])
        self.assertEqual("x86_64", info["arch"])
        self.assertEqual("Fancy CPU 9000", info["cpu_model"])
        self.assertEqual(24, info["logical_cpus_total"])
        self.assertEqual("bench-host\n", info["raw_hostname"])
        self.assertEqual("Linux bench-host 6.8.0 test x86_64 GNU/Linux\n", info["raw_uname"])
        self.assertEqual("", info["raw_lscpu"])

    def test_attach_benchmark_metadata_keeps_original_payload(self) -> None:
        if metadata_module is None or not hasattr(metadata_module, "attach_benchmark_metadata"):
            self.fail("attach_benchmark_metadata should exist for benchmark metadata helper")

        payload = {
            "trace_count": 800,
            "visible_completion_rate_at_stop": 0.99625,
        }
        original = copy.deepcopy(payload)

        annotated = metadata_module.attach_benchmark_metadata(
            payload=payload,
            suite_name="suite_a",
            entry_script="/tmp/run_suite_a_case.py",
            workload={"trace_count": 800},
            cpu_allocation={"server_cpuset": "1-3"},
            thread_topology={"worker_threads": 32},
            effective_flags={"disable_ai": True},
            commands={"server_command": "./server/build/LogSentinel --disable-ai"},
            artifacts={"result_json": "/tmp/result.json"},
            machine_info={"hostname": "bench-host"},
            captured_at="2026-04-17T11:22:33+08:00",
        )

        self.assertEqual(original, payload)
        self.assertEqual(800, annotated["trace_count"])
        self.assertEqual("suite_a", annotated["experiment_context"]["suite"])
        self.assertEqual("bench-host", annotated["experiment_context"]["machine"]["hostname"])
        self.assertEqual(32, annotated["experiment_context"]["thread_topology"]["worker_threads"])
        self.assertTrue(annotated["experiment_context"]["effective_flags"]["disable_ai"])
        self.assertEqual("/tmp/result.json", annotated["artifacts"]["result_json"])


if __name__ == "__main__":
    unittest.main()
