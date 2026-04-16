#!/usr/bin/env python3

from __future__ import annotations

import argparse
import heapq
import json
import queue
import random
import threading
import time
import urllib.error
import urllib.request
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

from profiles import SenderProfile, get_sender_profile


@dataclass(frozen=True)
class SpanTemplate:
    logical_trace_id: int
    span_id: int
    parent_span_id: Optional[int]
    role: str
    base_emit_at_ms: int
    service_name: str


@dataclass(order=True)
class ScheduledSpanEvent:
    # heapq 是 Python 标准库里的小顶堆。
    # 这里让 planned_emit_at_ms 参与比较，其他字段只做载荷，等价于 C++ priority_queue 里只按时间排序。
    planned_emit_at_ms: int
    logical_trace_id: int = field(compare=False)
    span_id: int = field(compare=False)
    parent_span_id: Optional[int] = field(compare=False)
    role: str = field(compare=False)
    delay_bucket: str = field(compare=False)
    event_kind: str = field(compare=False)
    expected_final_action: str = field(compare=False)
    base_emit_at_ms: int = field(compare=False)
    service_name: str = field(compare=False)
    # actual_* 和 http_status 是发送阶段填的观测值。
    # 后续 evaluator 要靠它判断 sender 自己有没有被阻塞到严重偏离 planned 时间。
    actual_send_start_ms: int = field(default=0, compare=False)
    actual_send_done_ms: int = field(default=0, compare=False)
    http_status: int = field(default=0, compare=False)


class TraceTemplateGenerator:
    def __init__(self, spans_per_trace: int, base_gap_ms: int, service_name: str):
        if spans_per_trace < 2:
            raise ValueError("spans_per_trace must be >= 2")
        if base_gap_ms <= 0:
            raise ValueError("base_gap_ms must be > 0")
        self.spans_per_trace = spans_per_trace
        self.base_gap_ms = base_gap_ms
        self.service_name = service_name

    def build_trace(self, logical_trace_id: int, start_ms: int) -> List[SpanTemplate]:
        spans: List[SpanTemplate] = []
        for span_id in range(1, self.spans_per_trace + 1):
            role = "body"
            if span_id == 1:
                role = "head"
            elif span_id == self.spans_per_trace:
                role = "tail"

            # 先生成一条理想顺序的链式 trace：1 -> 2 -> 3 -> ... -> N。
            # 脏时序不在这里制造，后续统一交给 DelaySampler，职责更清楚。
            spans.append(
                SpanTemplate(
                    logical_trace_id=logical_trace_id,
                    span_id=span_id,
                    parent_span_id=None if span_id == 1 else span_id - 1,
                    role=role,
                    base_emit_at_ms=start_ms + span_id * self.base_gap_ms,
                    service_name=self.service_name,
                )
            )
        return spans


def weighted_pick(rng: random.Random, weights: Dict[str, int]) -> str:
    total = sum(weights.values())
    if total <= 0:
        raise ValueError("weights must contain at least one positive value")

    point = rng.randint(1, total)
    acc = 0
    for name, weight in weights.items():
        if weight <= 0:
            continue
        acc += weight
        if point <= acc:
            return name

    # 正常情况下不会走到这里；保留兜底是为了避免后续手改权重时出现空返回。
    return next(iter(weights))


def _safe_range(min_ms: int, max_ms: int) -> Tuple[int, int]:
    if max_ms < min_ms:
        return min_ms, min_ms
    return min_ms, max_ms


def compute_delay_range_ms(
    bucket: str,
    tick_ms: int,
    grace_ms: int,
    tombstone_window_ms: int,
    base_gap_ms: int,
) -> Tuple[int, int]:
    if bucket == "clean_jitter":
        return _safe_range(0, int(min(0.2 * tick_ms, 40)))
    if bucket == "reorder_in_grace":
        return _safe_range(int(max(2 * base_gap_ms, 0.5 * tick_ms)), int(grace_ms - 0.25 * tick_ms))
    if bucket == "late_after_dispatch":
        return _safe_range(int(grace_ms + tick_ms), int(min(grace_ms + 3 * tick_ms, 0.35 * tombstone_window_ms)))
    if bucket == "replay_after_dispatch":
        return _safe_range(int(grace_ms + 1.5 * tick_ms), int(min(grace_ms + 4 * tick_ms, 0.5 * tombstone_window_ms)))
    raise ValueError(f"unsupported delay bucket: {bucket}")


class DelaySampler:
    def __init__(
        self,
        rng: random.Random,
        profile: SenderProfile,
        tick_ms: int,
        grace_ms: int,
        tombstone_window_ms: int,
        base_gap_ms: int,
    ):
        self.rng = rng
        self.profile = profile
        self.tick_ms = tick_ms
        self.grace_ms = grace_ms
        self.tombstone_window_ms = tombstone_window_ms
        self.base_gap_ms = base_gap_ms

    def choose_bucket(self, role: str) -> str:
        if role == "head":
            return weighted_pick(self.rng, self.profile.head_weights)
        if role == "tail":
            return weighted_pick(self.rng, self.profile.tail_weights)
        return weighted_pick(self.rng, self.profile.body_weights)

    def sample_delay_ms(self, bucket: str) -> int:
        min_ms, max_ms = compute_delay_range_ms(
            bucket,
            tick_ms=self.tick_ms,
            grace_ms=self.grace_ms,
            tombstone_window_ms=self.tombstone_window_ms,
            base_gap_ms=self.base_gap_ms,
        )
        return self.rng.randint(min_ms, max_ms)

    def build_event(self, span: SpanTemplate, bucket: str) -> ScheduledSpanEvent:
        if bucket == "replay_clone":
            raise ValueError("replay_clone must be expanded by build_replay_pair")

        expected_action = "ignore_after_cutoff" if bucket == "late_after_dispatch" else "merge_into_final_trace"
        delay_ms = self.sample_delay_ms(bucket)
        return ScheduledSpanEvent(
            planned_emit_at_ms=span.base_emit_at_ms + delay_ms,
            logical_trace_id=span.logical_trace_id,
            span_id=span.span_id,
            parent_span_id=span.parent_span_id,
            role=span.role,
            delay_bucket=bucket,
            event_kind="original",
            expected_final_action=expected_action,
            base_emit_at_ms=span.base_emit_at_ms,
            service_name=span.service_name,
        )

    def build_replay_pair(self, span: SpanTemplate) -> Tuple[ScheduledSpanEvent, ScheduledSpanEvent]:
        # replay 不是把原始 span 改成 replay，而是“原始 span 正常发 + 复制品晚到”。
        # 这样后续 evaluator 才能区分：该保住的原始 span，和本该被 tombstone 拦下来的复制品。
        original = self.build_event(span, "clean_jitter")
        replay_delay_ms = self.sample_delay_ms("replay_after_dispatch")
        replay = ScheduledSpanEvent(
            planned_emit_at_ms=span.base_emit_at_ms + replay_delay_ms,
            logical_trace_id=span.logical_trace_id,
            span_id=span.span_id,
            parent_span_id=span.parent_span_id,
            role=span.role,
            delay_bucket="replay_after_dispatch",
            event_kind="replay_clone",
            expected_final_action="ignore_after_cutoff",
            base_emit_at_ms=span.base_emit_at_ms,
            service_name=span.service_name,
        )
        return original, replay

    def build_events_for_span(self, span: SpanTemplate) -> List[ScheduledSpanEvent]:
        bucket = self.choose_bucket(span.role)
        if bucket == "replay_clone":
            return list(self.build_replay_pair(span))
        return [self.build_event(span, bucket)]


def finalize_expected_actions_for_trace(events: List[ScheduledSpanEvent], grace_ms: int) -> List[ScheduledSpanEvent]:
    seal_event_time_ms: Optional[int] = None
    for event in events:
        if (
            event.event_kind == "original"
            and event.role == "tail"
            and event.delay_bucket != "late_after_dispatch"
        ):
            # Suite B 的 protected 真值要以“有效 trace_end 到达后还能吸收多久”为基准。
            # 如果 tail 自己已经被刻意打成 late_after_dispatch，就不能反过来拿它给其它 late span 开 grace 窗口；
            # 因为真实系统在 tail 到达前根本没有 trace_end seal，只会走 collecting idle timeout。
            seal_event_time_ms = event.planned_emit_at_ms
            break

    protected_merge_cutoff_ms = None if seal_event_time_ms is None else seal_event_time_ms + max(0, grace_ms)
    for event in events:
        if event.event_kind == "replay_clone":
            # replay clone 表达的是“同一个 span 被重复晚发一份”，不管它落在哪个时间桶，期望都应该被去重/拦截。
            event.expected_final_action = "ignore_after_cutoff"
            continue
        if event.delay_bucket != "late_after_dispatch":
            event.expected_final_action = "merge_into_final_trace"
            continue
        if protected_merge_cutoff_ms is not None and event.planned_emit_at_ms <= protected_merge_cutoff_ms:
            # 这类事件虽然抽样桶名叫 late_after_dispatch，但如果 tail/trace_end 本身也晚到，
            # 它在真实生命周期里仍可能落进 sealed grace，所以 manifest 不能静态把它判成污染。
            event.expected_final_action = "merge_into_final_trace"
        else:
            event.expected_final_action = "ignore_after_cutoff"
    return events


class Scheduler:
    def __init__(self) -> None:
        self._heap: List[ScheduledSpanEvent] = []

    def push(self, event: ScheduledSpanEvent) -> None:
        heapq.heappush(self._heap, event)

    def pop_ready(self, now_ms: int) -> Optional[ScheduledSpanEvent]:
        if not self._heap:
            return None
        if self._heap[0].planned_emit_at_ms > now_ms:
            return None
        return heapq.heappop(self._heap)

    def next_wait_seconds(self, now_ms: int) -> float:
        if not self._heap:
            return 0.01
        delta_ms = max(0, self._heap[0].planned_emit_at_ms - now_ms)
        return min(delta_ms / 1000.0, 0.01)


class ManifestWriter:
    def __init__(self, path: Path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self._file = path.open("w", encoding="utf-8")

    def write_event(self, event: ScheduledSpanEvent) -> None:
        # manifest 是 Suite B 的“真值账本”。
        # 后续 evaluator 不猜 sender 当时想干什么，而是直接按这里的标签算正确性。
        self._file.write(json.dumps(asdict(event), ensure_ascii=True) + "\n")

    def close(self) -> None:
        self._file.close()


def current_time_ms() -> int:
    return int(time.time() * 1000)


def build_http_payload(event: ScheduledSpanEvent) -> Dict[str, object]:
    payload: Dict[str, object] = {
        "trace_key": event.logical_trace_id,
        "span_id": event.span_id,
        "start_time_ms": event.base_emit_at_ms,
        "name": f"suite-b-span-{event.span_id}",
        "service_name": event.service_name,
        "trace_end": event.role == "tail",
        "attributes": {
            "suite": "suite_b",
            "delay_bucket": event.delay_bucket,
            "event_kind": event.event_kind,
            "expected_final_action": event.expected_final_action,
        },
    }
    if event.parent_span_id is not None:
        payload["parent_span_id"] = event.parent_span_id
    return payload


def post_span(url: str, event: ScheduledSpanEvent, timeout_sec: float) -> int:
    body = json.dumps(build_http_payload(event), ensure_ascii=True).encode("utf-8")
    request = urllib.request.Request(
        url=url,
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout_sec) as response:
            return int(response.status)
    except urllib.error.HTTPError as exc:
        return int(exc.code)
    except urllib.error.URLError:
        # 这里把“根本没拿到 HTTP 响应”的情况收敛成 0。
        # 这样 sender 在 benchmark 场景下不会因为单个网络错误直接把整个多 worker 调度线程打死。
        return 0


def generate_scheduled_events(
    profile_name: str,
    seed: int,
    trace_count: int,
    spans_per_trace: int,
    base_gap_ms: int,
    trace_gap_ms: int,
    tick_ms: int,
    grace_ms: int,
    tombstone_window_ms: int,
    service_name: str,
    start_ms: int,
) -> List[ScheduledSpanEvent]:
    rng = random.Random(seed)
    profile = get_sender_profile(profile_name)
    generator = TraceTemplateGenerator(spans_per_trace=spans_per_trace, base_gap_ms=base_gap_ms, service_name=service_name)
    sampler = DelaySampler(
        rng=rng,
        profile=profile,
        tick_ms=tick_ms,
        grace_ms=grace_ms,
        tombstone_window_ms=tombstone_window_ms,
        base_gap_ms=base_gap_ms,
    )

    events: List[ScheduledSpanEvent] = []
    for offset in range(trace_count):
        trace_id = 1000000000 + offset
        trace_start_ms = start_ms + offset * trace_gap_ms
        trace_events: List[ScheduledSpanEvent] = []
        for span in generator.build_trace(logical_trace_id=trace_id, start_ms=trace_start_ms):
            trace_events.extend(sampler.build_events_for_span(span))
        events.extend(finalize_expected_actions_for_trace(trace_events, grace_ms=grace_ms))
    return events


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Suite B 生命周期鲁棒性 sender")
    parser.add_argument("--url", default="http://127.0.0.1:8080/logs/spans")
    parser.add_argument("--profile", default="mixed_realistic", choices=["clean_baseline", "mixed_realistic", "late_replay_stress"])
    parser.add_argument("--seed", type=int, default=20260415)
    parser.add_argument("--trace-count", type=int, default=10)
    parser.add_argument("--spans-per-trace", type=int, default=8)
    parser.add_argument("--base-gap-ms", type=int, default=20)
    parser.add_argument("--trace-gap-ms", type=int, default=80)
    parser.add_argument("--tick-ms", type=int, default=500)
    parser.add_argument("--grace-ms", type=int, default=1000)
    parser.add_argument("--tombstone-window-ms", type=int, default=12500)
    parser.add_argument("--service-name", default="svc-suite-b")
    parser.add_argument("--manifest", default="server/tests/benchmark/results/suite_b/manifest.jsonl")
    parser.add_argument("--timeout-sec", type=float, default=1.0)
    parser.add_argument("--send-workers", type=int, default=1)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args(argv)


def send_one_event(
    event: ScheduledSpanEvent,
    url: str,
    timeout_sec: float,
    dry_run: bool,
    post_func: Callable[[str, ScheduledSpanEvent, float], int],
) -> ScheduledSpanEvent:
    # 发送阶段直接在 event 自身上回填观测值。
    # 这样主线程最终写 manifest 时，不需要再维护第二份结果对象，字段口径也不会分裂。
    event.actual_send_start_ms = current_time_ms()
    event.http_status = 0 if dry_run else post_func(url, event, timeout_sec)
    event.actual_send_done_ms = current_time_ms()
    return event


def sender_worker_main(
    send_queue: "queue.Queue[Optional[ScheduledSpanEvent]]",
    result_queue: "queue.Queue[ScheduledSpanEvent]",
    url: str,
    timeout_sec: float,
    dry_run: bool,
    post_func: Callable[[str, ScheduledSpanEvent, float], int],
) -> None:
    while True:
        event = send_queue.get()
        try:
            if event is None:
                # None 是退出哨兵。
                # 主线程在所有事件都提交完后，再按 worker 数量塞同样多的哨兵，保证每个线程都能自然退出。
                return
            result_queue.put(send_one_event(event, url, timeout_sec, dry_run, post_func))
        finally:
            send_queue.task_done()


def run_scheduled_events(
    events: List[ScheduledSpanEvent],
    manifest_path: Path,
    url: str,
    timeout_sec: float,
    dry_run: bool,
    send_workers: int,
    post_func: Callable[[str, ScheduledSpanEvent, float], int] = post_span,
) -> int:
    if send_workers <= 0:
        raise ValueError("send_workers must be > 0")

    scheduler = Scheduler()
    for event in events:
        scheduler.push(event)

    # manifest 只允许主线程写。
    # 否则多个 worker 一边阻塞 HTTP、一边争抢文件句柄，最后很容易把“发送并发”和“账本写入顺序”搅在一起。
    writer = ManifestWriter(manifest_path)
    send_queue: "queue.Queue[Optional[ScheduledSpanEvent]]" = queue.Queue()
    result_queue: "queue.Queue[ScheduledSpanEvent]" = queue.Queue()
    workers: List[threading.Thread] = []

    for index in range(send_workers):
        thread = threading.Thread(
            target=sender_worker_main,
            args=(send_queue, result_queue, url, timeout_sec, dry_run, post_func),
            name=f"suite-b-sender-{index}",
            daemon=True,
        )
        thread.start()
        workers.append(thread)

    submitted = 0
    completed = 0
    total = len(events)
    try:
        while completed < total:
            now_ms = current_time_ms()
            while True:
                event = scheduler.pop_ready(now_ms)
                if event is None:
                    break
                send_queue.put(event)
                submitted += 1

            if completed >= total:
                break

            wait_seconds = 0.01 if submitted >= total else scheduler.next_wait_seconds(now_ms)
            try:
                result = result_queue.get(timeout=wait_seconds)
            except queue.Empty:
                continue

            writer.write_event(result)
            completed += 1

            # 主线程已经被唤醒了，就顺手把结果队列里这批完成事件一次性捞干净，
            # 避免 manifest 写入被拆成很多次短促唤醒。
            while True:
                try:
                    result = result_queue.get_nowait()
                except queue.Empty:
                    break
                writer.write_event(result)
                completed += 1
    finally:
        for _ in workers:
            send_queue.put(None)
        send_queue.join()
        for thread in workers:
            thread.join()
        writer.close()
    return completed


def run_sender(args: argparse.Namespace) -> int:
    start_ms = current_time_ms() + 100
    events = generate_scheduled_events(
        profile_name=args.profile,
        seed=args.seed,
        trace_count=args.trace_count,
        spans_per_trace=args.spans_per_trace,
        base_gap_ms=args.base_gap_ms,
        trace_gap_ms=args.trace_gap_ms,
        tick_ms=args.tick_ms,
        grace_ms=args.grace_ms,
        tombstone_window_ms=args.tombstone_window_ms,
        service_name=args.service_name,
        start_ms=start_ms,
    )
    sent = run_scheduled_events(
        events=events,
        manifest_path=Path(args.manifest),
        url=args.url,
        timeout_sec=args.timeout_sec,
        dry_run=args.dry_run,
        send_workers=args.send_workers,
    )

    print(
        "suite_b_sender done: "
        f"profile={args.profile}, seed={args.seed}, events={len(events)}, sent={sent}, "
        f"manifest={args.manifest}, dry_run={args.dry_run}, send_workers={args.send_workers}"
    )
    return 0


def main() -> int:
    return run_sender(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
