#!/usr/bin/env python3

import argparse
import json
import os
import socket
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Dict, List


SCRIPT_PATH = Path(__file__).with_name("trace_model_active_pool.lua")


class CaptureState:
    def __init__(self) -> None:
        # 这里把所有请求体按收到顺序收集起来，后面直接按时间线断言脚本是否真的在交替推进多条 trace。
        self.lock = threading.Lock()
        self.requests: List[Dict[str, object]] = []

    def append(self, body: Dict[str, object]) -> None:
        with self.lock:
            self.requests.append(body)

    def snapshot(self) -> List[Dict[str, object]]:
        with self.lock:
            return list(self.requests)


def make_handler(state: CaptureState):
    class Handler(BaseHTTPRequestHandler):
        def do_POST(self) -> None:  # noqa: N802
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length)
            body = json.loads(raw.decode("utf-8"))
            state.append(body)

            self.send_response(202)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            try:
                self.wfile.write(b'{"accepted":true}')
            except (BrokenPipeError, ConnectionResetError):
                # wrk 在压测结束瞬间可能会先关连接，再让服务端线程晚一步写响应。
                # 这不影响“请求体已经被收到并记录”的事实，所以这里直接吞掉即可。
                return

        def log_message(self, format: str, *args) -> None:  # noqa: A003
            # 校验脚本时不需要 http server 噪声日志，否则 wrk 输出和断言信息会混在一起。
            return

    return Handler


def pick_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def start_capture_server() -> tuple[ThreadingHTTPServer, CaptureState, threading.Thread]:
    state = CaptureState()
    port = pick_free_port()
    server = ThreadingHTTPServer(("127.0.0.1", port), make_handler(state))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    # wrk 启动很快，如果这里不留一个极短的 ready 缓冲，第一批请求偶尔会在监听线程真正进入循环前丢掉。
    time.sleep(0.1)
    # 这里额外把监听端口挂到 server 对象上，调用方后面拼 wrk URL 时更直接。
    server.capture_port = port  # type: ignore[attr-defined]
    return server, state, thread


def run_wrk_capture(role_plan: str,
                    threads: int,
                    connections: int,
                    duration: str,
                    active_pool_size: int) -> List[Dict[str, object]]:
    server, state, thread = start_capture_server()
    env = os.environ.copy()
    env["TRACE_WRK_ROLE_PLAN"] = role_plan

    try:
        cmd = [
            "wrk",
            f"-t{threads}",
            f"-c{connections}",
            f"-d{duration}",
            "-s",
            str(SCRIPT_PATH),
            f"http://127.0.0.1:{server.capture_port}",
            "--",
            "mixed",
            "8",
            "4",
            "2048",
            str(active_pool_size),
        ]
        subprocess.run(cmd, env=env, check=True, capture_output=True, text=True)
        time.sleep(0.2)
        return state.snapshot()
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


def verify_active_pool_round_robin() -> None:
    requests = run_wrk_capture(
        role_plan="end",
        threads=1,
        connections=1,
        duration="1s",
        active_pool_size=4,
    )
    if len(requests) < 8:
        raise AssertionError(f"expected at least 8 requests, got {len(requests)}")

    first_twelve = requests[:12]
    slots = []
    trace_key_by_slot: Dict[int, int] = {}
    last_span_by_slot: Dict[int, int] = {}

    for item in first_twelve:
        attrs = item.get("attributes")
        if not isinstance(attrs, dict):
            raise AssertionError(f"missing attributes in captured request: {item}")

        slot = int(attrs["active_slot"])
        span_id = int(item["span_id"])
        trace_key = int(item["trace_key"])
        slots.append(slot)

        # 同一个 slot 在一条 trace 生命周期内应该始终绑定同一个 trace_key。
        if slot in trace_key_by_slot and trace_key_by_slot[slot] != trace_key:
            raise AssertionError(
                f"expected slot {slot} to keep the same trace key before rollover, "
                f"got old={trace_key_by_slot[slot]}, new={trace_key}"
            )
        trace_key_by_slot.setdefault(slot, trace_key)

        # 同一个 slot 每次再次出现时，span_id 必须递增 1，说明它确实在“继续推进同一条活跃 trace”。
        if slot in last_span_by_slot and span_id != last_span_by_slot[slot] + 1:
            raise AssertionError(
                f"expected slot {slot} span_id to increase by 1, got last={last_span_by_slot[slot]}, new={span_id}"
            )
        last_span_by_slot[slot] = span_id

    if len(trace_key_by_slot) != 4:
        raise AssertionError(f"expected 4 active trace slots, got {trace_key_by_slot}")

    for index in range(1, len(slots)):
        expected_slot = (slots[index - 1] % 4) + 1
        if slots[index] != expected_slot:
            raise AssertionError(f"expected round-robin slots, got sequence {slots}")


def verify_thread_role_plan() -> None:
    requests = run_wrk_capture(
        role_plan="end,capacity,timeout",
        threads=3,
        connections=3,
        duration="1s",
        active_pool_size=1,
    )
    if not requests:
        raise AssertionError("expected captured requests, got none")

    observed: Dict[str, str] = {}
    for item in requests:
        attrs = item.get("attributes")
        if not isinstance(attrs, dict):
            continue
        thread_id = str(attrs.get("thread_id"))
        bench_mode = str(attrs.get("bench_mode"))
        if thread_id == "None" or bench_mode == "None":
            continue
        observed.setdefault(thread_id, bench_mode)

    # 这里不要求每个线程发出的请求数量完全一样，只验证 setup 分配下去的角色确实各自稳定。
    expected = {"1": "end", "2": "capacity", "3": "timeout"}
    for thread_id, role in expected.items():
        if observed.get(thread_id) != role:
            raise AssertionError(f"expected thread {thread_id} role {role}, got {observed}")


def main() -> None:
    parser = argparse.ArgumentParser(description="验证升级版 wrk active trace 池脚本的关键行为")
    parser.add_argument(
        "--case",
        choices=["active-pool", "role-plan", "all"],
        default="all",
        help="选择要验证的行为子集",
    )
    args = parser.parse_args()

    if args.case in {"active-pool", "all"}:
        verify_active_pool_round_robin()
    if args.case in {"role-plan", "all"}:
        verify_thread_role_plan()

    print("verify_trace_model_active_pool.py: all checks passed")


if __name__ == "__main__":
    main()
