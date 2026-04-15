#!/usr/bin/env python3

import heapq
import json
import random
import tempfile
import unittest
from pathlib import Path

from profiles import get_sender_profile
from sender import (
    DelaySampler,
    ManifestWriter,
    Scheduler,
    ScheduledSpanEvent,
    TraceTemplateGenerator,
    compute_delay_range_ms,
    weighted_pick,
)


class SuiteBSenderUnitTest(unittest.TestCase):
    def test_profiles_keep_clean_and_dirty_modes_separate(self) -> None:
        clean = get_sender_profile("clean_baseline")
        mixed = get_sender_profile("mixed_realistic")
        stress = get_sender_profile("late_replay_stress")

        self.assertEqual({"clean_jitter"}, set(clean.body_weights))
        self.assertIn("late_after_dispatch", mixed.body_weights)
        self.assertGreater(stress.body_weights["late_after_dispatch"], mixed.body_weights["late_after_dispatch"])
        self.assertGreater(stress.body_weights["replay_clone"], mixed.body_weights["replay_clone"])

    def test_weighted_pick_is_deterministic_with_fixed_seed(self) -> None:
        rng_a = random.Random(42)
        rng_b = random.Random(42)
        weights = {"clean_jitter": 84, "reorder_in_grace": 10, "late_after_dispatch": 5, "replay_clone": 1}

        picks_a = [weighted_pick(rng_a, weights) for _ in range(20)]
        picks_b = [weighted_pick(rng_b, weights) for _ in range(20)]

        self.assertEqual(picks_a, picks_b)

    def test_delay_ranges_follow_lifecycle_windows(self) -> None:
        tick_ms = 500
        grace_ms = 1000
        tombstone_window_ms = 12500
        base_gap_ms = 20

        self.assertEqual((0, 40), compute_delay_range_ms("clean_jitter", tick_ms, grace_ms, tombstone_window_ms, base_gap_ms))
        self.assertEqual((250, 875), compute_delay_range_ms("reorder_in_grace", tick_ms, grace_ms, tombstone_window_ms, base_gap_ms))
        self.assertEqual((1500, 2500), compute_delay_range_ms("late_after_dispatch", tick_ms, grace_ms, tombstone_window_ms, base_gap_ms))
        self.assertEqual((1750, 3000), compute_delay_range_ms("replay_after_dispatch", tick_ms, grace_ms, tombstone_window_ms, base_gap_ms))

    def test_trace_template_builds_chain_with_roles(self) -> None:
        generator = TraceTemplateGenerator(spans_per_trace=4, base_gap_ms=10, service_name="svc-suite-b")

        spans = generator.build_trace(logical_trace_id=1001, start_ms=100000)

        self.assertEqual(["head", "body", "body", "tail"], [span.role for span in spans])
        self.assertEqual([None, 1, 2, 3], [span.parent_span_id for span in spans])
        self.assertEqual([100010, 100020, 100030, 100040], [span.base_emit_at_ms for span in spans])

    def test_sampler_marks_expected_actions_and_replay_clone(self) -> None:
        profile = get_sender_profile("mixed_realistic")
        sampler = DelaySampler(
            rng=random.Random(7),
            profile=profile,
            tick_ms=500,
            grace_ms=1000,
            tombstone_window_ms=12500,
            base_gap_ms=20,
        )
        generator = TraceTemplateGenerator(spans_per_trace=3, base_gap_ms=20, service_name="svc-suite-b")
        body_span = generator.build_trace(logical_trace_id=77, start_ms=100000)[1]

        original, replay = sampler.build_replay_pair(body_span)

        self.assertEqual("original", original.event_kind)
        self.assertEqual("clean_jitter", original.delay_bucket)
        self.assertEqual("merge_into_final_trace", original.expected_final_action)
        self.assertEqual("replay_clone", replay.event_kind)
        self.assertEqual("replay_after_dispatch", replay.delay_bucket)
        self.assertEqual("ignore_after_cutoff", replay.expected_final_action)
        self.assertGreater(replay.planned_emit_at_ms, original.planned_emit_at_ms)

    def test_scheduler_uses_min_heap_order(self) -> None:
        events = [
            ScheduledSpanEvent(300, 1, 3, 2, "body", "clean_jitter", "original", "merge_into_final_trace", 30, "svc", 0, 0, 0),
            ScheduledSpanEvent(100, 1, 1, None, "head", "clean_jitter", "original", "merge_into_final_trace", 10, "svc", 0, 0, 0),
            ScheduledSpanEvent(200, 1, 2, 1, "body", "clean_jitter", "original", "merge_into_final_trace", 20, "svc", 0, 0, 0),
        ]
        scheduler = Scheduler()
        for event in events:
            scheduler.push(event)

        ordered = [scheduler.pop_ready(now_ms=1000).span_id for _ in range(3)]

        self.assertEqual([1, 2, 3], ordered)
        self.assertIsNone(scheduler.pop_ready(now_ms=1000))

    def test_manifest_writer_outputs_jsonl_truth_tags(self) -> None:
        event = ScheduledSpanEvent(
            100,
            42,
            2,
            1,
            "body",
            "reorder_in_grace",
            "original",
            "merge_into_final_trace",
            80,
            "svc-suite-b",
            90,
            101,
            202,
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "manifest.jsonl"
            writer = ManifestWriter(path)
            writer.write_event(event)
            writer.close()

            rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]

        self.assertEqual(1, len(rows))
        self.assertEqual(42, rows[0]["logical_trace_id"])
        self.assertEqual("reorder_in_grace", rows[0]["delay_bucket"])
        self.assertEqual("merge_into_final_trace", rows[0]["expected_final_action"])
        self.assertEqual(202, rows[0]["http_status"])


if __name__ == "__main__":
    unittest.main()
