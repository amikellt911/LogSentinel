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
                # scaling wrapper 现在必须暴露 core base offset，
                # 否则云机容器只给高位核区间时，主曲线入口会重新退化成 0 起始 cpuset。
                'CORE_BASE_OFFSET="${SUITE_D_CORE_BASE_OFFSET:-0}"',
                "--core-base-offset",
                "--trace-lifecycle-profile protected",
                "--trace-primary-flush-span-threshold 512",
            ],
            "run_suite_d_connection_search_24c.sh": [
                "run_suite_d_case.py",
                'CONNECTION_SET="${SUITE_D_CONNECTION_SET:-90,108,120}"',
                'REPEATS="${SUITE_D_REPEATS:-3}"',
                'CORE_BASE_OFFSET="${SUITE_D_CORE_BASE_OFFSET:-0}"',
                'TRACE_MAX_DISPATCH_PER_TICK="${SUITE_D_TRACE_MAX_DISPATCH_PER_TICK:-256}"',
                'TRACE_SWEEP_INTERVAL_MS="${SUITE_D_TRACE_SWEEP_INTERVAL_MS:-20}"',
                '--trace-max-dispatch-per-tick "${TRACE_MAX_DISPATCH_PER_TICK}"',
                '--trace-sweep-interval-ms "${TRACE_SWEEP_INTERVAL_MS}"',
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

    def test_flamegraph_wrappers_and_common_runner_pin_suite_d_ai_off_knobs(self) -> None:
        # 这组断言专门锁 Suite D 火焰图口径：
        # 24 核 flamegraph 只是主曲线的结构解释图，所以必须和主曲线共用同一套 AI-off 与 24 核拓扑参数。
        flamegraph_wrapper = (SUITE_D_DIR / "run_suite_d_flamegraph_24c.sh").read_text(encoding="utf-8")
        self.assertIn("run_flamegraph.sh", flamegraph_wrapper)
        self.assertIn("trace_model_suite_d.lua", flamegraph_wrapper)
        self.assertIn('SERVER_CPUSET="${SUITE_D_SERVER_CPUSET:-4-23}"', flamegraph_wrapper)
        self.assertIn('WRK_CPUSET="${SUITE_D_WRK_CPUSET:-0-3}"', flamegraph_wrapper)
        self.assertIn('SERVER_IO_THREADS="${SUITE_D_SERVER_IO_THREADS:-6}"', flamegraph_wrapper)
        self.assertIn("DISABLE_AI=1", flamegraph_wrapper)
        self.assertIn("DISABLE_WEBHOOK=1", flamegraph_wrapper)
        self.assertIn("NO_AUTO_START_PROXY=1", flamegraph_wrapper)

        common_flamegraph = (
            SUITE_D_DIR.parent / "common" / "run_flamegraph_case.sh"
        ).read_text(encoding="utf-8")
        self.assertIn('SERVER_IO_THREADS="${SERVER_IO_THREADS:-1}"', common_flamegraph)
        self.assertIn('DISABLE_AI="${DISABLE_AI:-0}"', common_flamegraph)
        self.assertIn('DISABLE_WEBHOOK="${DISABLE_WEBHOOK:-0}"', common_flamegraph)
        self.assertIn('NO_AUTO_START_PROXY="${NO_AUTO_START_PROXY:-0}"', common_flamegraph)
        self.assertIn('--server-io-threads "${SERVER_IO_THREADS}"', common_flamegraph)
        self.assertIn('--disable-ai', common_flamegraph)
        self.assertIn('--disable-webhook', common_flamegraph)
        self.assertIn('--no-auto-start-proxy', common_flamegraph)

        generic_flamegraph_wrapper = (SUITE_D_DIR / "run_flamegraph.sh").read_text(encoding="utf-8")
        self.assertIn("generic wrapper", generic_flamegraph_wrapper)
        self.assertIn("run_flamegraph_case.sh", generic_flamegraph_wrapper)


if __name__ == "__main__":
    unittest.main()
