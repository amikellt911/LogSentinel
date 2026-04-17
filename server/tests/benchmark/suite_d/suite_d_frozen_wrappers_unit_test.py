#!/usr/bin/env python3

import unittest
from pathlib import Path


SUITE_D_DIR = Path(__file__).resolve().parent


class SuiteDFrozenWrappersUnitTest(unittest.TestCase):
    def test_frozen_wrappers_exist_and_pin_expected_defaults(self) -> None:
        # 这些断言锁的是论文和复跑要用的默认命令片段。
        # 一旦 wrapper 被人顺手改掉，这里就会第一时间红灯，而不是等到后面跑实验才发现口径漂了。
        wrapper_expectations = {
            "run_suite_d_local4_ai_on.sh": [
                "run_suite_d_case.py",
                'SERVER_CPUSET="${SUITE_D_SERVER_CPUSET:-1-3}"',
                'WRK_CPUSET="${SUITE_D_WRK_CPUSET:-0}"',
                "--server-io-threads 1",
                "--dispatch-worker-threads 1",
                "--worker-threads 8",
                "--trace-ai-provider",
                "--disable-webhook",
            ],
            "run_suite_d_topology_search_24c.sh": [
                "run_suite_d_topology_search.py",
                'SERVER_CPUSET="${SUITE_D_SERVER_CPUSET:-4-23}"',
                'WRK_CPUSET="${SUITE_D_WRK_CPUSET:-0-3}"',
                "--sender-cores 4",
                "--backend-cores 20",
            ],
            "run_suite_d_scaling_24c.sh": [
                "run_suite_d_scaling.py",
                'TOTAL_CORE_POINTS="${SUITE_D_TOTAL_CORE_POINTS:-4,8,12,16,20,24}"',
                'CONNECTIONS_PER_SENDER_CORE="${SUITE_D_CONNECTIONS_PER_SENDER_CORE:-30}"',
                "--trace-lifecycle-profile protected",
                "--trace-primary-flush-span-threshold 512",
            ],
        }

        for filename, expected_fragments in wrapper_expectations.items():
            wrapper_path = SUITE_D_DIR / filename
            self.assertTrue(wrapper_path.exists(), f"{filename} should exist")
            content = wrapper_path.read_text(encoding="utf-8")
            for fragment in expected_fragments:
                self.assertIn(fragment, content, f"{filename} should contain {fragment}")

    def test_generic_wrapper_stays_marked_as_non_paper_entry(self) -> None:
        # generic wrapper 只给老 common runner 兜底，不能再被误当成正式论文入口。
        generic_wrapper = (SUITE_D_DIR / "run_suite_d.sh").read_text(encoding="utf-8")
        self.assertIn("generic wrapper", generic_wrapper)
        self.assertIn("run_wrk_case.sh", generic_wrapper)


if __name__ == "__main__":
    unittest.main()
