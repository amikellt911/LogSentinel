#!/usr/bin/env python3
"""
Settings 第三层黑盒联调脚本。

这层测试不再盯类级别行为，而是走真实后端进程：
1. 先通过 `/settings/config` 写入冷启动配置；
2. 再重启后端；
3. 最后通过端口变化、真实 `/logs/spans` 请求、AI proxy 实收 prompt 和 webhook 实际外发，证明配置确实被消费。
"""

from __future__ import annotations

import argparse
import json
import os
import select
import sqlite3
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Optional

import requests


class ProbeState:
    """
    本地探针状态。

    这里把“假 AI proxy”和“假 webhook server”合在同一个进程里，不是为了偷懒，
    而是为了把第三层黑盒真正缺的两个观测点放到同一份共享状态里：
    1. 后端到底把什么 prompt 下发给了 AI；
    2. 后端到底有没有按 channel 配置真的把告警发出去。
    """

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.trace_requests: list[dict[str, Any]] = []
        self.webhook_payloads: list[dict[str, Any]] = []

    def record_trace_request(self, payload: dict[str, Any]) -> None:
        with self._lock:
            self.trace_requests.append(payload)

    def record_webhook_payload(self, payload: dict[str, Any]) -> None:
        with self._lock:
            self.webhook_payloads.append(payload)

    def snapshot_trace_requests(self) -> list[dict[str, Any]]:
        with self._lock:
            return list(self.trace_requests)

    def snapshot_webhook_payloads(self) -> list[dict[str, Any]]:
        with self._lock:
            return list(self.webhook_payloads)

    def clear_webhook_payloads(self) -> None:
        with self._lock:
            self.webhook_payloads.clear()


class ProbeRequestHandler(BaseHTTPRequestHandler):
    server: "ProbeServer"

    def do_POST(self) -> None:  # noqa: N802
        content_length = int(self.headers.get("Content-Length", "0"))
        raw_body = self.rfile.read(content_length).decode("utf-8")

        try:
            payload = json.loads(raw_body) if raw_body else {}
        except json.JSONDecodeError:
            payload = {"raw_body": raw_body}

        if self.path == "/analyze/trace/mock":
            self.server.state.record_trace_request(payload)
            trace_text = str(payload.get("trace_text", ""))
            risk_level = "critical"
            if "webhook-warning-sentinel" in trace_text:
                risk_level = "warning"

            response = {
                "ok": True,
                "analysis": {
                    "summary": f"probe-{risk_level}-summary",
                    "risk_level": risk_level,
                    "root_cause": f"probe-{risk_level}-root-cause",
                    "solution": f"probe-{risk_level}-solution",
                    "confidence": 0.91,
                },
                "usage": {
                    "input_tokens": 11,
                    "output_tokens": 7,
                    "total_tokens": 18,
                },
            }
            self._send_json(200, response)
            return

        if self.path == "/webhook":
            self.server.state.record_webhook_payload(payload)
            self._send_json(200, {"errcode": 0, "errmsg": "ok"})
            return

        self._send_json(404, {"error": f"unsupported path: {self.path}"})

    def log_message(self, format: str, *args: object) -> None:  # noqa: A003
        # 黑盒脚本失败时我们只关心结构化探针状态，不需要 http.server 的访问日志刷屏。
        return

    def _send_json(self, status_code: int, payload: dict[str, Any]) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class ProbeServer(ThreadingHTTPServer):
    def __init__(self, server_address: tuple[str, int], state: ProbeState) -> None:
        super().__init__(server_address, ProbeRequestHandler)
        self.state = state


class LocalProbeService:
    def __init__(self) -> None:
        self.state = ProbeState()
        self.server = ProbeServer(("127.0.0.1", 0), self.state)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    @property
    def port(self) -> int:
        return int(self.server.server_address[1])

    @property
    def base_url(self) -> str:
        return f"http://127.0.0.1:{self.port}"

    def start(self) -> None:
        self.thread.start()

    def stop(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=3)


def parse_args() -> argparse.Namespace:
    """
    解析命令行参数。

    这里把 bootstrap_port 和 configured_port 分开：
    - 第一阶段必须显式指定 bootstrap_port，保证我们总能连上配置接口；
    - 第二阶段故意不再传 `--port`，让后端只能从 SQLite 里读 configured_port，
      这样才能证明 `http_port` 真的是冷启动生效，而不是 CLI 参数把它盖掉了。
    """
    server_dir = Path(__file__).resolve().parents[1]
    default_bin = server_dir / "build" / "LogSentinel"
    default_db = Path("/tmp") / f"logsentinel_settings_blackbox_{int(time.time())}.db"

    parser = argparse.ArgumentParser(description="Settings 第三层黑盒联调脚本")
    parser.add_argument("--server-bin", default=str(default_bin), help="LogSentinel 可执行文件路径")
    parser.add_argument("--db", default=str(default_db), help="临时数据库路径")
    parser.add_argument("--bootstrap-port", type=int, default=18180, help="首次启动时显式传入的端口")
    parser.add_argument("--configured-port", type=int, default=18181, help="通过设置写入数据库的目标端口")
    parser.add_argument("--ready-timeout", type=float, default=10.0, help="服务就绪等待超时（秒）")
    parser.add_argument("--dispatch-timeout", type=float, default=8.0, help="trace_summary 等待超时（秒）")
    parser.add_argument("--proxy-timeout-ms", type=int, default=5000, help="后端调用本地假 AI proxy 的超时")
    parser.add_argument("--keep-artifacts", action="store_true", help="失败后保留临时数据库文件")
    return parser.parse_args()


def base_url(port: int) -> str:
    return f"http://127.0.0.1:{port}"


def start_server(server_bin: Path,
                 db_path: Path,
                 port: Optional[int],
                 trace_ai_provider: Optional[str] = None,
                 trace_ai_base_url: Optional[str] = None,
                 trace_ai_timeout_ms: Optional[int] = None) -> subprocess.Popen:
    """
    启动后端进程。

    这里固定追加 `--no-auto-start-proxy`：
    - 当前黑盒只验证 Settings 冷启动配置消费，不验证真实 AI provider；
    - 把 proxy 拉起来只会增加环境噪音，不能提升这三项配置的证明力度。
    """
    if not server_bin.exists():
        raise FileNotFoundError(f"未找到服务可执行文件: {server_bin}")

    cmd = [str(server_bin), "--db", str(db_path), "--no-auto-start-proxy"]
    if port is not None:
        cmd.extend(["--port", str(port)])
    if trace_ai_provider:
        cmd.extend(["--trace-ai-provider", trace_ai_provider])
    if trace_ai_base_url:
        cmd.extend(["--trace-ai-base-url", trace_ai_base_url])
    if trace_ai_timeout_ms and trace_ai_timeout_ms > 0:
        cmd.extend(["--trace-ai-timeout-ms", str(trace_ai_timeout_ms)])

    print(f"[settings-blackbox] 启动服务: {' '.join(cmd)}")
    return subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )


def wait_server_ready(url: str, timeout_sec: float, proc: subprocess.Popen) -> None:
    """
    轮询等待服务监听成功。

    这里沿用冒烟脚本的弱健康检查策略：
    - 只要 `GET /__smoke_ready__` 能收到响应，并且返回体里带该路径，
      就说明当前请求确实打到了本进程，而不是碰巧连到别的同端口服务。
    """
    deadline = time.time() + timeout_sec
    last_error = None

    while time.time() < deadline:
        if proc.poll() is not None:
            raise RuntimeError("服务进程在就绪前已退出")
        try:
            resp = requests.get(f"{url}/__smoke_ready__", timeout=0.8)
            if resp.status_code and "/__smoke_ready__" in resp.text:
                return
        except requests.RequestException as exc:
            last_error = exc
        time.sleep(0.2)

    raise RuntimeError(f"服务未在 {timeout_sec}s 内就绪，最后错误: {last_error}")


def read_available_process_logs(proc: Optional[subprocess.Popen], max_bytes: int = 12000) -> str:
    """
    非阻塞读取当前可获得的服务日志片段。

    黑盒脚本最大的排障成本就是“进程已经挂了，但你不知道它死前说了什么”。
    这里故意只读当前缓冲区，不阻塞主流程，失败时能把第一手日志直接带出来。
    """
    if proc is None or proc.stdout is None:
        return ""

    fd = proc.stdout.fileno()
    os.set_blocking(fd, False)
    chunks = []
    total = 0

    while total < max_bytes:
        ready, _, _ = select.select([proc.stdout], [], [], 0)
        if not ready:
            break
        data = proc.stdout.read(min(1024, max_bytes - total))
        if not data:
            break
        chunks.append(data)
        total += len(data)

    return "".join(chunks)


def wait_process_log_contains(proc: subprocess.Popen,
                              expected_text: str,
                              timeout_sec: float,
                              max_bytes_per_poll: int = 4096) -> str:
    """
    轮询进程输出，直到出现目标文本。

    `kernel_worker_threads` 这种设置真正生效的位置只在启动期建线程池那一瞬间。
    它不是运行时指标，单靠 `/settings/all` 只能证明“库里存的是 2”，
    证明不了后端这次启动到底有没有真的按 2 条 worker 线程建起来。
    所以这里直接盯启动日志，是这条黑盒最短也最硬的证据。
    """
    deadline = time.time() + timeout_sec
    collected = ""

    while time.time() < deadline:
        collected += read_available_process_logs(proc, max_bytes=max_bytes_per_poll)
        if expected_text in collected:
            return collected
        if proc.poll() is not None:
            break
        time.sleep(0.2)

    raise RuntimeError(
        f"服务日志未在 {timeout_sec}s 内出现目标文本: {expected_text}\n已收集日志:\n{collected}"
    )


def stop_server(proc: Optional[subprocess.Popen]) -> None:
    if proc is None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=3)


def cleanup(proc: Optional[subprocess.Popen], db_path: Path, keep_artifacts: bool) -> None:
    stop_server(proc)

    if keep_artifacts:
        print(f"[settings-blackbox] 保留临时文件用于排障: {db_path}")
        return

    for suffix in ["", "-wal", "-shm"]:
        candidate = Path(str(db_path) + suffix)
        if candidate.exists():
            candidate.unlink()


def post_config_patch(url: str, items: list[dict]) -> None:
    """
    调用 `/settings/config` 写入标量配置。

    这里故意一次性写入端口、alias、AI 开关和测试辅助参数：
    - 端口变化负责证明“冷启动读库”；
    - alias 负责证明 `LogHandler` 真读到了新配置；
    - AI 关闭负责证明 `TraceSessionManager` 真读到了新配置；
    - grace/sweep/idle 只是为了把 alias 这条黑盒验证窗口压短，同时排掉 idle timeout 的歧义。
    """
    resp = requests.post(
        f"{url}/settings/config",
        headers={"Content-Type": "application/json"},
        json={"items": items},
        timeout=3.0,
    )
    if resp.status_code != 200:
        raise RuntimeError(f"POST /settings/config 失败: status={resp.status_code}, body={resp.text}")


def post_prompts(url: str, prompts: list[dict]) -> None:
    resp = requests.post(
        f"{url}/settings/prompts",
        headers={"Content-Type": "application/json"},
        json=prompts,
        timeout=3.0,
    )
    if resp.status_code != 200:
        raise RuntimeError(f"POST /settings/prompts 失败: status={resp.status_code}, body={resp.text}")


def post_channels(url: str, channels: list[dict]) -> None:
    resp = requests.post(
        f"{url}/settings/channels",
        headers={"Content-Type": "application/json"},
        json=channels,
        timeout=3.0,
    )
    if resp.status_code != 200:
        raise RuntimeError(f"POST /settings/channels 失败: status={resp.status_code}, body={resp.text}")


def fetch_all_settings(url: str) -> dict:
    resp = requests.get(f"{url}/settings/all", timeout=3.0)
    if resp.status_code != 200:
        raise RuntimeError(f"GET /settings/all 失败: status={resp.status_code}, body={resp.text}")
    return resp.json()


def wait_old_port_closed(url: str, timeout_sec: float) -> None:
    """
    等待旧端口彻底失效。

    这个检查不是为了证明“进程退出”本身，而是为了避免端口切换验证出现假阳性：
    如果旧端口还在响应，我们就不能说系统入口真的已经迁到新端口了。
    """
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        try:
            requests.get(f"{url}/__smoke_ready__", timeout=0.6)
        except requests.RequestException:
            return
        time.sleep(0.2)
    raise RuntimeError(f"旧端口在 {timeout_sec}s 内仍可访问: {url}")


def post_span(url: str, payload: dict) -> None:
    resp = requests.post(f"{url}/logs/spans", json=payload, timeout=2.0)
    if resp.status_code != 202:
        raise RuntimeError(f"POST /logs/spans 失败: status={resp.status_code}, body={resp.text}")


def wait_trace_summary_status(db_path: Path, trace_id: str, expected_status: str, timeout_sec: float) -> None:
    """
    轮询等待 trace_summary.ai_status 达到期望值。

    为什么只查 trace_summary：
    - alias 生效后，summary 一定先落库；
    - `ai_analysis_enabled=false` 时不会产出 analysis，真正能稳定证明消费生效的是 `ai_status=skipped_manual`。
    """
    deadline = time.time() + timeout_sec

    while time.time() < deadline:
        if db_path.exists():
            try:
                conn = sqlite3.connect(str(db_path))
                try:
                    cur = conn.cursor()
                    cur.execute(
                        """
                        SELECT ai_status
                        FROM trace_summary
                        WHERE trace_id = ?
                        ORDER BY rowid DESC
                        LIMIT 1
                        """,
                        (trace_id,),
                    )
                    row = cur.fetchone()
                    if row and str(row[0]) == expected_status:
                        return
                finally:
                    conn.close()
            except sqlite3.Error:
                pass
        time.sleep(0.2)

    raise RuntimeError(
        f"trace_summary.ai_status 未在 {timeout_sec}s 内达到期望值: trace_id={trace_id}, expected={expected_status}"
    )


def query_trace_analysis_count(db_path: Path, trace_id: str) -> int:
    conn = sqlite3.connect(str(db_path))
    try:
        cur = conn.cursor()
        cur.execute("SELECT COUNT(*) FROM trace_analysis WHERE trace_id = ?", (trace_id,))
        return int(cur.fetchone()[0])
    finally:
        conn.close()


def seed_retention_trace_rows(db_path: Path, expired_trace_id: str, fresh_trace_id: str) -> None:
    """
    直接往 SQLite 塞一条过期 trace 和一条新 trace。

    retention 测试不能只看配置回填，因为真正有价值的问题是：
    启动后那轮后台清理到底有没有按 cutoff 把旧数据删掉。
    所以这里在第三次启动前先人工造数据，再让启动清理去删，证据最直接。
    """
    now_ms = int(time.time() * 1000)
    two_days_ms = 2 * 24 * 60 * 60 * 1000
    expired_start_ms = now_ms - two_days_ms
    fresh_start_ms = now_ms

    conn = sqlite3.connect(str(db_path))
    try:
        cur = conn.cursor()
        cur.execute(
            """
            INSERT INTO trace_summary
            (trace_id, service_name, start_time_ms, end_time_ms, duration_ms, span_count, token_count, risk_level, ai_status, ai_error)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (expired_trace_id, "retention-service", expired_start_ms, expired_start_ms + 100, 100, 1, 0, "unknown", "completed", ""),
        )
        cur.execute(
            """
            INSERT INTO trace_span
            (trace_id, span_id, parent_id, service_name, operation, start_time_ms, duration_ms, status, attributes_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (expired_trace_id, expired_trace_id + "-span", None, "retention-service", "expired-op", expired_start_ms, 100, "OK", "{}"),
        )
        cur.execute(
            """
            INSERT INTO trace_summary
            (trace_id, service_name, start_time_ms, end_time_ms, duration_ms, span_count, token_count, risk_level, ai_status, ai_error)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (fresh_trace_id, "retention-service", fresh_start_ms, fresh_start_ms + 100, 100, 1, 0, "unknown", "completed", ""),
        )
        cur.execute(
            """
            INSERT INTO trace_span
            (trace_id, span_id, parent_id, service_name, operation, start_time_ms, duration_ms, status, attributes_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (fresh_trace_id, fresh_trace_id + "-span", None, "retention-service", "fresh-op", fresh_start_ms, 100, "OK", "{}"),
        )
        conn.commit()
    finally:
        conn.close()


def trace_exists(db_path: Path, trace_id: str) -> bool:
    conn = sqlite3.connect(str(db_path))
    try:
        cur = conn.cursor()
        cur.execute("SELECT COUNT(*) FROM trace_summary WHERE trace_id = ?", (trace_id,))
        row = cur.fetchone()
        return bool(row and int(row[0]) > 0)
    finally:
        conn.close()


def wait_until(predicate: Any, timeout_sec: float, interval_sec: float = 0.2) -> None:
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        if predicate():
            return
        time.sleep(interval_sec)
    raise RuntimeError("等待条件超时")


def run_flow(args: argparse.Namespace) -> int:
    server_bin = Path(args.server_bin).resolve()
    db_path = Path(args.db).resolve()
    bootstrap = args.bootstrap_port
    configured = args.configured_port
    proc: Optional[subprocess.Popen] = None
    probe_service: Optional[LocalProbeService] = None

    if bootstrap == configured:
        raise ValueError("bootstrap-port 与 configured-port 不能相同，否则无法证明端口切换生效")

    old_url = base_url(bootstrap)
    new_url = base_url(configured)

    # 这里把 idle timeout 拉大、sealed grace 压小：
    # - 如果 alias 没生效，这条 trace 在当前等待窗口内不应该被 idle timeout 收走；
    # - 如果 alias 生效，就会很快进入 sealed 并被 sweep 推进落库。
    config_items = [
        {"key": "http_port", "value": str(configured)},
        {"key": "trace_end_aliases", "value": json.dumps(["end"])},
        {"key": "ai_analysis_enabled", "value": "0"},
        {"key": "kernel_worker_threads", "value": "2"},
        {"key": "log_retention_days", "value": "1"},
        {"key": "collecting_idle_timeout_ms", "value": "30000"},
        {"key": "sealed_grace_window_ms", "value": "400"},
        {"key": "sweep_tick_ms", "value": "100"},
        {"key": "span_capacity", "value": "128"},
        {"key": "token_limit", "value": "100000"},
    ]

    try:
        proc = start_server(server_bin, db_path, bootstrap)
        wait_server_ready(old_url, args.ready_timeout, proc)
        post_config_patch(old_url, config_items)
        print("[settings-blackbox] 已写入冷启动配置，准备重启后验证真实生效")

        stop_server(proc)
        proc = None
        wait_old_port_closed(old_url, 3.0)

        # 第二次启动故意不传 --port，迫使 main.cpp 从配置快照里取 http_port。
        proc = start_server(server_bin, db_path, None)
        wait_server_ready(new_url, args.ready_timeout, proc)
        startup_logs = wait_process_log_contains(proc, "2 worker threads", timeout_sec=3.0)

        settings = fetch_all_settings(new_url)
        app_config = settings.get("config", {})
        if int(app_config.get("http_port", 0)) != configured:
            raise RuntimeError(f"http_port 回填不正确: expected={configured}, actual={app_config.get('http_port')}")
        if app_config.get("trace_end_aliases") != ["end"]:
            raise RuntimeError(f"trace_end_aliases 回填不正确: {app_config.get('trace_end_aliases')}")
        if bool(app_config.get("ai_analysis_enabled", True)) is not False:
            raise RuntimeError(f"ai_analysis_enabled 回填不正确: {app_config.get('ai_analysis_enabled')}")
        if int(app_config.get("kernel_worker_threads", 0)) != 2:
            raise RuntimeError(f"kernel_worker_threads 回填不正确: {app_config.get('kernel_worker_threads')}")
        if int(app_config.get("log_retention_days", 0)) != 1:
            raise RuntimeError(f"log_retention_days 回填不正确: {app_config.get('log_retention_days')}")
        if "Thread Model:" not in startup_logs:
            raise RuntimeError("未读到线程模型启动日志，无法证明 kernel_worker_threads 被消费")

        trace_key = int(time.time() * 1000)
        trace_id = str(trace_key)
        root = {
            "trace_key": trace_key,
            "span_id": trace_key + 1,
            "start_time_ms": 1700000000000,
            "end_time_ms": 1700000000100,
            "name": "settings-blackbox-root",
            "service_name": "settings-blackbox-service",
            "status": "OK",
        }
        child = {
            "trace_key": trace_key,
            "span_id": trace_key + 2,
            "parent_span_id": trace_key + 1,
            "start_time_ms": 1700000000110,
            "end_time_ms": 1700000000200,
            "name": "settings-blackbox-child",
            "service_name": "settings-blackbox-service",
            "status": "ERROR",
            # 这里故意不发默认 trace_end，只发 alias。
            # 如果这条 trace 最后还能在短窗口内落到 summary，就只能说明 trace_end_aliases 真被消费了。
            "end": True,
        }
        post_span(new_url, root)
        post_span(new_url, child)

        wait_trace_summary_status(db_path, trace_id, "skipped_manual", args.dispatch_timeout)
        analysis_count = query_trace_analysis_count(db_path, trace_id)
        if analysis_count != 0:
            raise RuntimeError(f"ai_analysis_enabled=false 时不应写 trace_analysis，实际条数: {analysis_count}")

        probe_service = LocalProbeService()
        probe_service.start()

        prompt_payload = [
            {
                "id": 0,
                "name": "prompt-inactive",
                "content": json.dumps(
                    {
                        "domain_goal": "PROMPT_INACTIVE_SENTINEL",
                        "focus_areas": ["ignore-me"],
                    }
                ),
                "is_active": 0,
            },
            {
                "id": 0,
                "name": "prompt-active",
                "content": json.dumps(
                    {
                        "domain_goal": "PROMPT_ACTIVE_SENTINEL",
                        "focus_areas": ["capture-active-prompt"],
                        "output_preference": ["return concise json fields"],
                    }
                ),
                "is_active": 1,
            },
        ]
        post_prompts(new_url, prompt_payload)

        prompt_settings = fetch_all_settings(new_url)
        prompts = prompt_settings.get("prompts", [])
        active_prompt = next((item for item in prompts if item.get("name") == "prompt-active"), None)
        if not active_prompt or not active_prompt.get("id"):
            raise RuntimeError("未能从 /settings/all 读回 active prompt 的真实 id")

        post_config_patch(
            new_url,
            [
                {"key": "ai_analysis_enabled", "value": "1"},
                {"key": "ai_provider", "value": "mock"},
                {"key": "ai_model", "value": "probe-model"},
                {"key": "ai_api_key", "value": "probe-key"},
                {"key": "ai_language", "value": "zh"},
                {"key": "active_prompt_id", "value": str(active_prompt["id"])},
            ],
        )
        post_channels(
            new_url,
            [
                {
                    "id": 0,
                    "name": "probe-feishu-channel",
                    "provider": "feishu",
                    "webhook_url": f"{probe_service.base_url}/webhook",
                    "secret": "probe-secret",
                    "alert_threshold": "critical",
                    "is_active": 1,
                }
            ],
        )

        stop_server(proc)
        proc = None
        wait_old_port_closed(new_url, 3.0)

        expired_trace_id = "retention-expired-trace"
        fresh_trace_id = "retention-fresh-trace"
        seed_retention_trace_rows(db_path, expired_trace_id, fresh_trace_id)

        # prompt / provider / webhook channel 都是冷启动消费。
        # 所以这里必须再次重启，而且要显式把 trace_ai_base_url 指向本地假 proxy，
        # 这样才能证明后端真把 prompt/model/key 下发出去了，而不是只存进 SQLite。
        proc = start_server(server_bin,
                            db_path,
                            None,
                            trace_ai_provider="mock",
                            trace_ai_base_url=probe_service.base_url,
                            trace_ai_timeout_ms=args.proxy_timeout_ms)
        wait_server_ready(new_url, args.ready_timeout, proc)
        wait_process_log_contains(proc, "2 worker threads", timeout_sec=3.0)
        wait_until(lambda: not trace_exists(db_path, expired_trace_id), timeout_sec=args.dispatch_timeout)
        if not trace_exists(db_path, fresh_trace_id):
            raise RuntimeError("启动清理不应该把未过期 trace 一起删掉")

        prompt_phase_settings = fetch_all_settings(new_url)
        prompt_phase_config = prompt_phase_settings.get("config", {})
        if bool(prompt_phase_config.get("ai_analysis_enabled", False)) is not True:
            raise RuntimeError("prompt/webhook 阶段 ai_analysis_enabled 没有在重启后生效")
        if int(prompt_phase_config.get("active_prompt_id", 0)) != int(active_prompt["id"]):
            raise RuntimeError("active_prompt_id 回填不正确")

        warning_trace_key = int(time.time() * 1000) + 100
        warning_trace_id = str(warning_trace_key)
        warning_root = {
            "trace_key": warning_trace_key,
            "span_id": warning_trace_key + 1,
            "start_time_ms": 1700000001000,
            "end_time_ms": 1700000001100,
            "name": "webhook-warning-root",
            "service_name": "settings-blackbox-service",
            "status": "OK",
        }
        warning_child = {
            "trace_key": warning_trace_key,
            "span_id": warning_trace_key + 2,
            "parent_span_id": warning_trace_key + 1,
            "start_time_ms": 1700000001110,
            "end_time_ms": 1700000001200,
            "name": "webhook-warning-sentinel",
            "service_name": "settings-blackbox-service",
            "status": "ERROR",
            "trace_end": True,
        }
        probe_service.state.clear_webhook_payloads()
        post_span(new_url, warning_root)
        post_span(new_url, warning_child)
        wait_trace_summary_status(db_path, warning_trace_id, "completed", args.dispatch_timeout)
        if query_trace_analysis_count(db_path, warning_trace_id) != 1:
            raise RuntimeError("warning trace 应该产出 1 条 trace_analysis")
        trace_requests = probe_service.state.snapshot_trace_requests()
        if not trace_requests:
            raise RuntimeError("假 AI proxy 没收到任何 trace 请求，无法证明 prompt 被消费")
        last_trace_request = trace_requests[-1]
        rendered_prompt = str(last_trace_request.get("prompt", ""))
        if "PROMPT_ACTIVE_SENTINEL" not in rendered_prompt:
            raise RuntimeError("proxy 实收 prompt 不包含 active prompt 的标记文本")
        if "PROMPT_INACTIVE_SENTINEL" in rendered_prompt:
            raise RuntimeError("proxy 实收 prompt 混入了 inactive prompt 的内容")
        if "Use Chinese for all natural-language fields" not in rendered_prompt:
            raise RuntimeError("ai_language=zh 没有进入最终 prompt 模板")
        if str(last_trace_request.get("model", "")) != "probe-model":
            raise RuntimeError("proxy 实收请求没有带上新的 model")
        if str(last_trace_request.get("api_key", "")) != "probe-key":
            raise RuntimeError("proxy 实收请求没有带上新的 api_key")

        # 先打 warning 再看 webhook，是为了证明 channel.threshold 真起作用了。
        # 如果这里直接拿 critical 去测，只能证明“会不会发”，证明不了阈值过滤这层设置真的被消费。
        time.sleep(1.0)
        if probe_service.state.snapshot_webhook_payloads():
            raise RuntimeError("warning 级别不应该命中 threshold=critical 的 webhook 渠道")

        critical_trace_key = int(time.time() * 1000) + 200
        critical_trace_id = str(critical_trace_key)
        critical_root = {
            "trace_key": critical_trace_key,
            "span_id": critical_trace_key + 1,
            "start_time_ms": 1700000002000,
            "end_time_ms": 1700000002100,
            "name": "webhook-critical-root",
            "service_name": "settings-blackbox-service",
            "status": "OK",
        }
        critical_child = {
            "trace_key": critical_trace_key,
            "span_id": critical_trace_key + 2,
            "parent_span_id": critical_trace_key + 1,
            "start_time_ms": 1700000002110,
            "end_time_ms": 1700000002200,
            "name": "webhook-critical-sentinel",
            "service_name": "settings-blackbox-service",
            "status": "ERROR",
            "trace_end": True,
        }
        post_span(new_url, critical_root)
        post_span(new_url, critical_child)
        wait_trace_summary_status(db_path, critical_trace_id, "completed", args.dispatch_timeout)
        wait_until(lambda: len(probe_service.state.snapshot_webhook_payloads()) >= 1,
                   timeout_sec=args.dispatch_timeout)

        webhook_payload = probe_service.state.snapshot_webhook_payloads()[0]
        if webhook_payload.get("msg_type") != "post":
            raise RuntimeError("feishu webhook payload 的 msg_type 不正确")
        if "timestamp" not in webhook_payload or "sign" not in webhook_payload:
            raise RuntimeError("配置了 secret 的飞书 webhook 负载必须带 timestamp/sign")
        title = (
            webhook_payload.get("content", {})
            .get("post", {})
            .get("zh_cn", {})
            .get("title", "")
        )
        if "critical" not in str(title).lower():
            raise RuntimeError("critical webhook 的标题没有带上风险等级")

        print(
            "[settings-blackbox] 黑盒联调通过：端口切换、trace_end_aliases、"
            "ai_analysis_enabled、prompt/active_prompt_id、webhook channel、"
            "kernel_worker_threads、log_retention_days 都已验证"
        )
        return 0
    except Exception as exc:
        logs = read_available_process_logs(proc)
        print(f"[settings-blackbox] 失败: {exc}")
        if logs:
            print("[settings-blackbox] 服务日志片段：")
            print(logs)
        if probe_service is not None:
            print("[settings-blackbox] 探针 trace 请求快照：")
            print(json.dumps(probe_service.state.snapshot_trace_requests(), indent=2, ensure_ascii=False))
            print("[settings-blackbox] 探针 webhook 请求快照：")
            print(json.dumps(probe_service.state.snapshot_webhook_payloads(), indent=2, ensure_ascii=False))
        return 1
    finally:
        cleanup(proc, db_path, args.keep_artifacts)
        if probe_service is not None:
            probe_service.stop()


def main() -> int:
    return run_flow(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
