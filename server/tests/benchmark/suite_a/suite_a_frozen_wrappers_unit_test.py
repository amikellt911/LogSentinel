#!/usr/bin/env python3

import unittest
from pathlib import Path


SUITE_A_DIR = Path(__file__).resolve().parent


class SuiteAFrozenWrappersUnitTest(unittest.TestCase):
    def test_story_wrappers_exist_and_pin_expected_defaults(self) -> None:
        wrapper_expectations = {
            "run_suite_a_main_vs_cmp_4c.sh": [
                "run_suite_a_main_vs_cmp.py",
                'SERVER_CPUSET="${SUITE_A_SERVER_CPUSET:-2-3}"',
                "--server-io-threads 1",
                "--worker-threads 32",
                "--dispatch-worker-threads 1",
                'LOAD_POINTS="${SUITE_A_LOAD_POINTS:-light:800:20:1,mid:1600:10:2,heavy:3200:5:2}"',
            ],
            "run_suite_a_main_vs_cmp_16c.sh": [
                "run_suite_a_main_vs_cmp.py",
                'SERVER_CPUSET="${SUITE_A_SERVER_CPUSET:-4-15}"',
                "--server-io-threads 4",
                "--worker-threads 96",
                "--dispatch-worker-threads 3",
                'LOAD_POINTS="${SUITE_A_LOAD_POINTS:-light:1600:20:2,mid:3200:10:2,heavy:6400:5:2}"',
            ],
            "run_suite_a_buffer_compare_4c.sh": [
                "run_suite_a_buffer_compare.py",
                'SERVER_CPUSET="${SUITE_A_SERVER_CPUSET:-2-3}"',
                "--server-io-threads 1",
                "--worker-threads 32",
                "--dispatch-worker-threads 1",
                "--gap-ms 10",
                "--trace-count 1600",
                "--send-workers 2",
                "--buffered-span-thresholds 512",
            ],
            "run_suite_a_buffer_compare_16c.sh": [
                "run_suite_a_buffer_compare.py",
                'SERVER_CPUSET="${SUITE_A_SERVER_CPUSET:-4-15}"',
                "--server-io-threads 4",
                "--worker-threads 96",
                "--dispatch-worker-threads 3",
                "--gap-ms 5",
                "--trace-count 6400",
                "--send-workers 2",
                "--buffered-span-thresholds 512",
            ],
            "run_suite_a_search_stage1_4c.sh": [
                "run_suite_a_search_stage1.py",
                'SERVER_CPUSET="${SUITE_A_SERVER_CPUSET:-2-3}"',
                "--server-io-threads 1",
                "--worker-threads 32",
                "--dispatch-worker-threads 1",
                "--trace-count 800",
                "--send-workers 1",
            ],
        }

        # 这条测试锁的是“论文最终命令不再靠手抄”。
        # 如果这些 wrapper 名称或默认参数被改掉，结果口径就会再次漂移。
        for filename, expected_fragments in wrapper_expectations.items():
            wrapper_path = SUITE_A_DIR / filename
            self.assertTrue(wrapper_path.exists(), f"{filename} should exist")
            content = wrapper_path.read_text(encoding="utf-8")
            for fragment in expected_fragments:
                self.assertIn(fragment, content, f"{filename} should contain {fragment}")


if __name__ == "__main__":
    unittest.main()
