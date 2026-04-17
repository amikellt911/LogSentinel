#!/usr/bin/env python3

import unittest
from pathlib import Path


BENCHMARK_DIR = Path(__file__).resolve().parent


class PaperBenchmarkCloudWrapperUnitTest(unittest.TestCase):
    def test_cloud_campaign_wrapper_pins_current_paper_entrypoints(self) -> None:
        wrapper_path = BENCHMARK_DIR / "run_paper_benchmark_cloud.sh"
        self.assertTrue(wrapper_path.exists(), "cloud paper benchmark wrapper should exist")
        content = wrapper_path.read_text(encoding="utf-8")

        # 总入口只允许串起已经冻结的正式入口。
        # 这里锁住脚本名，是为了防止后续又把调参脚本、generic wrapper 或旧 wrk 入口混进正式 campaign。
        expected_fragments = [
            "run_suite_a_main_vs_cmp_16c.sh",
            "run_suite_a_buffer_compare_16c.sh",
            "run_suite_b_campaign.py",
            "run_suite_d_connection_search_24c.sh",
            "run_suite_d_scaling_24c.sh",
            "--trace-sweep-interval-ms 20",
            "--trace-max-dispatch-per-tick 256",
        ]
        for fragment in expected_fragments:
            self.assertIn(fragment, content)

    def test_cloud_campaign_wrapper_supports_high_offset_container_cpusets(self) -> None:
        wrapper_path = BENCHMARK_DIR / "run_paper_benchmark_cloud.sh"
        content = wrapper_path.read_text(encoding="utf-8")

        # 云机容器经常只暴露 160-191 这类高位核。
        # 总入口必须能统一平移 Suite A/B/D 的 sender/backend cpuset，
        # 否则用户还是会回到手工复制并改一堆命令的老问题。
        expected_fragments = [
            "detect_core_base_offset",
            "BENCHMARK_CORE_BASE_OFFSET",
            'SUITE_A_SENDER_CPUSET="$(build_cpuset "${CORE_BASE_OFFSET}" 4)"',
            'SUITE_B_SERVER_CPUSET="$(build_cpuset "$((CORE_BASE_OFFSET + 3))" 13)"',
            'SUITE_D_SERVER_CPUSET_20C="$(build_cpuset "$((CORE_BASE_OFFSET + 4))" 20)"',
            'SUITE_D_CORE_BASE_OFFSET="${CORE_BASE_OFFSET}"',
        ]
        for fragment in expected_fragments:
            self.assertIn(fragment, content)

    def test_cloud_campaign_wrapper_exposes_binary_overrides(self) -> None:
        wrapper_path = BENCHMARK_DIR / "run_paper_benchmark_cloud.sh"
        content = wrapper_path.read_text(encoding="utf-8")

        # 云机上 main/cmp 可能来自两个不同 checkout 或手工上传目录。
        # 所以总入口必须有明确二进制 override，不能继续假设 build/LogSentinel。
        self.assertIn("--main-server-bin", content)
        self.assertIn("--cmp-server-bin", content)
        self.assertIn("BENCHMARK_MAIN_SERVER_BIN", content)
        self.assertIn("BENCHMARK_CMP_SERVER_BIN", content)


if __name__ == "__main__":
    unittest.main()
