#!/usr/bin/env python3
"""
Settings 第三层黑盒联调脚本。

这层测试不再盯类级别行为，而是走真实后端进程：
1. 先通过 `/settings/config` 写入冷启动配置；
2. 再重启后端；
3. 最后通过端口变化、真实 `/logs/spans` 请求、AI proxy 实收 prompt 和 webhook 实际外发，证明配置确实被消费。
"""

from __future__ import annotations

import asyncio
import argparse
import importlib.util
import json
import os
import select
import shutil
import sqlite3
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Optional

import requests


def load_ai_proxy_main_module():
    """
    黑盒这里不再自己手搓一套“像 retry 的逻辑”，而是直接复用真实 Python proxy 里的执行器。
    这样新增的 AI retry 场景验证的是生产代码本身，不是另写一份测试专用分支。
    """
    script_path = Path(__file__).resolve().parents[1] / "ai" / "proxy" / "main.py"
    spec = importlib.util.spec_from_file_location("settings_blackbox_ai_proxy_main", script_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load ai proxy main module from {script_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


AI_PROXY_MAIN = load_ai_proxy_main_module()


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
        self.provider_behaviors: dict[str, dict[str, Any]] = {}
        self.provider_behavior_sequences: dict[str, list[dict[str, Any]]] = {}

    def record_trace_request(self, provider: str, payload: dict[str, Any]) -> None:
        with self._lock:
            flattened = dict(payload)
            flattened["provider"] = provider
            self.trace_requests.append(flattened)

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

    def clear_trace_requests(self) -> None:
        with self._lock:
            self.trace_requests.clear()

    def set_provider_behavior(self, provider: str, behavior: dict[str, Any]) -> None:
        with self._lock:
            self.provider_behaviors[provider] = dict(behavior)
            # 静态行为一旦被重新设置，就把旧的顺序队列清掉。
            # 否则上一个场景残留的“第二枪响应”会串到下一个场景，黑盒就会变成随机失败。
            self.provider_behavior_sequences.pop(provider, None)

    def set_provider_behavior_sequence(self, provider: str, behaviors: list[dict[str, Any]]) -> None:
        with self._lock:
            # 顺序行为专门服务“同一个 provider 连续被调用多次”的黑盒场景，
            # 比如主路重试或主路先失败再成功。
            # 这里故意做成队列优先、静态行为兜底：
            # - 有队列时，每次请求都消耗下一个响应；
            # - 队列耗尽后就回退到静态行为或 None，避免测试脚本偷偷复用上一枪结果。
            self.provider_behavior_sequences[provider] = [dict(item) for item in behaviors]

    def get_provider_behavior(self, provider: str) -> Optional[dict[str, Any]]:
        with self._lock:
            sequence = self.provider_behavior_sequences.get(provider)
            if sequence:
                behavior = dict(sequence.pop(0))
                if not sequence:
                    self.provider_behavior_sequences.pop(provider, None)
                return behavior
            behavior = self.provider_behaviors.get(provider)
            if behavior is None:
                return None
            return dict(behavior)


def build_probe_success_payload(provider: str,
                                *,
                                risk_level: str = "warning",
                                summary: Optional[str] = None,
                                root_cause: Optional[str] = None,
                                solution: Optional[str] = None) -> dict[str, Any]:
    """
    本地假 proxy 对双 provider 黑盒统一返回这套结构。
    这样后端走的仍然是“真实 HTTP + 真实协议解析 + 真实状态流转”，
    只是把云侧的不稳定性替换成了本地可控的响应。
    """
    normalized_provider = provider.lower()
    normalized_risk = risk_level.lower()
    return {
        "ok": True,
        "provider": normalized_provider,
        "analysis": {
            "summary": summary or f"{normalized_provider}-{normalized_risk}-summary",
            "risk_level": normalized_risk,
            "root_cause": root_cause or f"{normalized_provider}-{normalized_risk}-root-cause",
            "solution": solution or f"{normalized_provider}-{normalized_risk}-solution",
        },
        "usage": {
            "input_tokens": 17,
            "output_tokens": 9,
            "total_tokens": 26,
        },
    }


def build_probe_failure_payload(provider: str,
                                *,
                                error_status: str,
                                error_message: str,
                                error_code: Optional[int] = None) -> dict[str, Any]:
    return {
        "ok": False,
        "provider": provider.lower(),
        "error_code": error_code,
        "error_status": error_status,
        "error_message": error_message,
    }


class ProbeTraceProvider:
    """
    本地探针里的“假 provider”。

    对 C++ 来说，它看到的仍然是一个会说 Python proxy 协议的 HTTP 服务；
    对黑盒来说，这个对象负责把每次 provider attempt 都记下来，再按预设队列吐出失败或成功。
    这样我们既能验证重试有没有发生，也不会把测试绑到真实云 API。
    """

    def __init__(self,
                 state: ProbeState,
                 provider: str,
                 *,
                 retry_enabled: bool,
                 retry_max_attempts: int) -> None:
        self.state = state
        self.provider = provider
        self.retry_enabled = retry_enabled
        self.retry_max_attempts = retry_max_attempts

    def analyze_trace(self,
                      trace_text: str,
                      prompt: str,
                      api_key: Optional[str] = None,
                      model: Optional[str] = None,
                      timeout_ms: Optional[int] = None) -> dict[str, Any]:
        self.state.record_trace_request(
            self.provider,
            {
                "trace_text": trace_text,
                "prompt": prompt,
                "api_key": api_key,
                "model": model,
                "timeout_ms": timeout_ms,
                "retry_enabled": self.retry_enabled,
                "retry_max_attempts": self.retry_max_attempts,
            },
        )
        behavior = self.state.get_provider_behavior(self.provider)
        if behavior is None:
            return build_probe_failure_payload(
                self.provider,
                error_status="UNCONFIGURED_PROVIDER",
                error_message=f"no fake behavior for {self.provider}",
            )
        return behavior


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
            self.server.state.record_trace_request("mock", payload)
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

        if self.path.startswith("/analyze/trace/"):
            provider = self.path.rsplit("/", 1)[-1].lower()
            retry_enabled = bool(payload.get("retry_enabled", False))
            retry_max_attempts = int(payload.get("retry_max_attempts", 1) or 1)
            trace_provider = ProbeTraceProvider(
                self.server.state,
                provider,
                retry_enabled=retry_enabled,
                retry_max_attempts=retry_max_attempts,
            )

            async def direct_call_provider(func, *args, **kwargs):
                return func(*args, **kwargs)

            result = asyncio.run(
                AI_PROXY_MAIN.execute_trace_provider_with_retry(
                    trace_provider,
                    trace_text=str(payload.get("trace_text", "")),
                    prompt=str(payload.get("prompt", "")),
                    api_key=payload.get("api_key"),
                    model=payload.get("model"),
                    timeout_ms=int(payload.get("timeout_ms", 0) or 0) or None,
                    retry_enabled=retry_enabled,
                    retry_max_attempts=retry_max_attempts,
                    provider_call_fn=direct_call_provider,
                    sleep_fn=lambda _: None,
                )
            )
            # 这里继续返回 Python proxy 的统一 envelope。
            # C++ 黑盒真正关心的是“最终落到 completed/failed_primary/failed_both 的业务结果”，
            # 不关心本地探针内部到底用了多少次 attempt。
            if isinstance(result, dict) and result.get("ok") is False and str(result.get("error_status", "")) == "UNCONFIGURED_PROVIDER":
                self._send_json(404, result)
                return
            self._send_json(200, result)
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
                 frontend_dist: Optional[Path] = None,
                 extra_args: Optional[list[str]] = None,
                 trace_ai_provider: Optional[str] = None,
                 trace_ai_base_url: Optional[str] = None,
                 trace_ai_timeout_ms: Optional[int] = None) -> subprocess.Popen:
    """
    启动后端进程。

    这里固定追加 `--no-auto-start-proxy`：
    - 黑盒主线验证的是“后端到底消费了哪些冷启动配置”，不是去测真实云 API；
    - 当我们显式传入 `trace_ai_base_url` 时，后端会去打本地 fake proxy，继续验证双 provider/fallback；
    - 不管哪种场景，都没必要再拉起真实 Python proxy，否则只会增加环境噪音。
    """
    if not server_bin.exists():
        raise FileNotFoundError(f"未找到服务可执行文件: {server_bin}")

    cmd = [str(server_bin), "--db", str(db_path), "--no-auto-start-proxy"]
    if port is not None:
        cmd.extend(["--port", str(port)])
    if frontend_dist is not None:
        # 单入口黑盒始终显式传 frontend_dist：
        # 这样验证的是“后端静态资源托管逻辑”，不是碰巧复用了开发机本地某份现成的 client/dist。
        cmd.extend(["--frontend-dist", str(frontend_dist)])
    if extra_args:
        # benchmark 专用 CLI 开关不进 Settings，所以黑盒需要能把额外启动参数原样透传给后端。
        # 这里故意做成列表直拼，避免每加一个新开关都继续改测试辅助函数签名。
        cmd.extend(extra_args)
    if trace_ai_provider:
        cmd.extend(["--trace-ai-provider", trace_ai_provider])
    if trace_ai_base_url:
        cmd.extend(["--trace-ai-base-url", trace_ai_base_url])
    if trace_ai_timeout_ms and trace_ai_timeout_ms > 0:
        cmd.extend(["--trace-ai-timeout-ms", str(trace_ai_timeout_ms)])

    print(f"[settings-blackbox] 启动服务: {' '.join(cmd)}")
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    # 黑盒后面会多次等待不同的启动日志关键字。
    # 既然 stdout 管道是“读一次就前进一次”，那么这里就给每个进程挂一个累计缓冲，
    # 避免前一次断言把日志消费掉之后，后一次再去等另一个关键字时误以为后端没打印。
    setattr(proc, "_log_buffer", "")
    return proc


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

    newly_read = "".join(chunks)
    if newly_read:
        # 这里保留“历史 + 增量”的累计日志，而不是只把本次 read 的结果往外返回。
        # 原因很直接：黑盒经常先等 `Thread Model`，接着又等 `Trace AI disabled` 之类的后续关键字，
        # 如果不自己做累计，前一次读取就会把整段启动日志从管道里永久吃掉。
        log_buffer = getattr(proc, "_log_buffer", "")
        setattr(proc, "_log_buffer", log_buffer + newly_read)
    return newly_read


def wait_process_log_contains(proc: subprocess.Popen,
                              expected_text: str,
                              timeout_sec: float,
                              max_bytes_per_poll: int = 4096) -> str:
    """
    轮询进程输出，直到出现目标文本。

    `kernel_worker_threads / kernel_io_threads` 这种线程模型配置真正生效的位置都在启动期。
    它们不是运行时指标，单靠 `/settings/all` 只能证明“库里存的是 2”，
    证明不了后端这次启动到底有没有真的按对应线程数把 worker 池和 MiniMuduo I/O 线程建起来。
    所以这里直接盯启动日志，是这条黑盒最短也最硬的证据。
    """
    deadline = time.time() + timeout_sec
    # 这里先看累计缓冲，再补读当前增量。
    # 既然 `subprocess.PIPE` 是破坏性读取，那么黑盒不能假设“上一次没断言这个关键字，日志就还在”。
    # 用累计缓冲后，多个场景可以复用同一个进程对象反复查不同关键字，不会再把日志断言顺序绑死。
    collected = getattr(proc, "_log_buffer", "")

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


def fetch_all_settings_via_api_prefix(url: str) -> dict:
    resp = requests.get(f"{url}/api/settings/all", timeout=3.0)
    if resp.status_code != 200:
        raise RuntimeError(f"GET /api/settings/all 失败: status={resp.status_code}, body={resp.text}")
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


def query_trace_summary_row(db_path: Path, trace_id: str) -> Optional[dict[str, Any]]:
    """
    读取 trace_summary 的关键字段。

    这里把 `span_count` 一起带出来，是因为第三刀黑盒要锁生命周期档位差异：
    - `protected` 会在 sealed grace 里继续吸收晚到 span；
    - `minimal` 不会。
    这种差异最终会直接反映到 summary 的聚合 span 数，而不是只体现在启动日志里。
    """
    conn = sqlite3.connect(str(db_path))
    try:
        cur = conn.cursor()
        cur.execute(
            """
            SELECT trace_id, ai_status, ai_error, risk_level, span_count
            FROM trace_summary
            WHERE trace_id = ?
            ORDER BY rowid DESC
            LIMIT 1
            """,
            (trace_id,),
        )
        row = cur.fetchone()
        if not row:
            return None
        return {
            "trace_id": str(row[0]),
            "ai_status": str(row[1]),
            "ai_error": str(row[2] or ""),
            "risk_level": str(row[3] or ""),
            "span_count": int(row[4] or 0),
        }
    finally:
        conn.close()


def build_demo_trace_pair(trace_key: int, child_name: str, *, trace_end_field: str = "trace_end") -> tuple[dict[str, Any], dict[str, Any]]:
    """
    双 provider 黑盒这里继续复用“两条 span 组成一条 trace”的最小模型。
    这样每个场景都还是走真实 Trace 聚合、真实 worker、真实 SQLite 落库，
    不会因为测试 provider 切换就偷偷绕开主链状态机。
    """
    root = {
        "trace_key": trace_key,
        "span_id": trace_key + 1,
        "start_time_ms": 1700000010000,
        "end_time_ms": 1700000010100,
        "name": f"{child_name}-root",
        "service_name": "settings-blackbox-service",
        "status": "OK",
    }
    child = {
        "trace_key": trace_key,
        "span_id": trace_key + 2,
        "parent_span_id": trace_key + 1,
        "start_time_ms": 1700000010110,
        "end_time_ms": 1700000010200,
        "name": child_name,
        "service_name": "settings-blackbox-service",
        "status": "ERROR",
        trace_end_field: True,
    }
    return root, child


def send_trace_pair(url: str, trace_key: int, child_name: str, *, trace_end_field: str = "trace_end") -> str:
    root, child = build_demo_trace_pair(trace_key, child_name, trace_end_field=trace_end_field)
    post_span(url, root)
    post_span(url, child)
    return str(trace_key)


def restart_server_with_fake_proxy(proc: Optional[subprocess.Popen],
                                   server_bin: Path,
                                   db_path: Path,
                                   frontend_dist: Path,
                                   new_url: str,
                                   ready_timeout: float,
                                   probe_service: LocalProbeService,
                                   proxy_timeout_ms: int,
                                   extra_args: Optional[list[str]] = None) -> subprocess.Popen:
    stop_server(proc)
    wait_old_port_closed(new_url, 3.0)
    restarted = start_server(server_bin,
                             db_path,
                             None,
                             frontend_dist=frontend_dist,
                             extra_args=extra_args,
                             trace_ai_base_url=probe_service.base_url,
                             trace_ai_timeout_ms=proxy_timeout_ms)
    wait_server_ready(new_url, ready_timeout, restarted)
    wait_process_log_contains(restarted, "Thread Model:", timeout_sec=3.0)
    return restarted


def assert_last_provider_request(state: ProbeState,
                                 expected_order: list[str],
                                 *,
                                 expected_model_by_provider: Optional[dict[str, str]] = None,
                                 expected_api_key_by_provider: Optional[dict[str, str]] = None) -> None:
    requests_snapshot = state.snapshot_trace_requests()
    actual_order = [str(item.get("provider", "")) for item in requests_snapshot]
    if actual_order != expected_order:
        raise RuntimeError(f"provider 请求顺序不正确: expected={expected_order}, actual={actual_order}")

    if expected_model_by_provider:
        for item in requests_snapshot:
            provider = str(item.get("provider", ""))
            if provider in expected_model_by_provider and str(item.get("model", "")) != expected_model_by_provider[provider]:
                raise RuntimeError(
                    f"{provider} 实收 model 不正确: expected={expected_model_by_provider[provider]}, actual={item.get('model')}"
                )
    if expected_api_key_by_provider:
        for item in requests_snapshot:
            provider = str(item.get("provider", ""))
            if provider in expected_api_key_by_provider and str(item.get("api_key", "")) != expected_api_key_by_provider[provider]:
                raise RuntimeError(
                    f"{provider} 实收 api_key 不正确: expected={expected_api_key_by_provider[provider]}, actual={item.get('api_key')}"
                )


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


def create_temp_frontend_dist() -> Path:
    root = Path(tempfile.mkdtemp(prefix="logsentinel-frontend-dist-"))
    # 这里直接造一个极小的前端壳：
    # 我们只需要证明后端会按路径去找 index/assets，并不会依赖真实 Vite 构建产物里的复杂文件树。
    (root / "assets").mkdir(parents=True, exist_ok=True)
    (root / "index.html").write_text(
        "<!doctype html><html><body>single-entry-shell</body></html>",
        encoding="utf-8",
    )
    (root / "assets" / "app.js").write_text(
        "console.log('single-entry-js');",
        encoding="utf-8",
    )
    (root / "assets" / "app.css").write_text(
        "body{background:#fff;}",
        encoding="utf-8",
    )
    return root


def assert_single_entry_frontend_routes(url: str) -> None:
    # 这 5 条就是这轮单入口部署的最低交付面：
    # `/` 和 `/settings` 证明页面白名单 fallback；
    # `/fdasxz` 证明未知路径不会误回 index.html；
    # `/assets/app.js` 证明真实静态文件能直出且 MIME 正确；
    # `/api/settings/all` 证明同源入口下 API 前缀剥离已经接通。
    root_resp = requests.get(f"{url}/", timeout=2.0)
    if root_resp.status_code != 200 or "single-entry-shell" not in root_resp.text:
        raise RuntimeError(f"GET / 没有返回前端壳页面: status={root_resp.status_code}, body={root_resp.text}")
    if "text/html" not in root_resp.headers.get("Content-Type", ""):
        raise RuntimeError(f"GET / Content-Type 不正确: {root_resp.headers.get('Content-Type')}")

    settings_resp = requests.get(f"{url}/settings", timeout=2.0)
    if settings_resp.status_code != 200 or "single-entry-shell" not in settings_resp.text:
        raise RuntimeError(
            f"GET /settings 没有命中白名单 fallback: status={settings_resp.status_code}, body={settings_resp.text}"
        )
    if "text/html" not in settings_resp.headers.get("Content-Type", ""):
        raise RuntimeError(f"GET /settings Content-Type 不正确: {settings_resp.headers.get('Content-Type')}")

    unknown_resp = requests.get(f"{url}/fdasxz", timeout=2.0)
    if unknown_resp.status_code != 404:
        raise RuntimeError(f"GET /fdasxz 应该返回 404，实际 status={unknown_resp.status_code}")

    asset_resp = requests.get(f"{url}/assets/app.js", timeout=2.0)
    if asset_resp.status_code != 200 or "single-entry-js" not in asset_resp.text:
        raise RuntimeError(
            f"GET /assets/app.js 没有返回真实静态文件: status={asset_resp.status_code}, body={asset_resp.text}"
        )
    if "application/javascript" not in asset_resp.headers.get("Content-Type", ""):
        raise RuntimeError(f"GET /assets/app.js Content-Type 不正确: {asset_resp.headers.get('Content-Type')}")

    fetch_all_settings_via_api_prefix(url)


def run_flow(args: argparse.Namespace) -> int:
    server_bin = Path(args.server_bin).resolve()
    db_path = Path(args.db).resolve()
    frontend_dist = create_temp_frontend_dist()
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
        {"key": "ai_timeout_ms", "value": "30000"},
        {"key": "kernel_io_threads", "value": "2"},
        {"key": "kernel_worker_threads", "value": "2"},
        {"key": "log_retention_days", "value": "1"},
        {"key": "collecting_idle_timeout_ms", "value": "30000"},
        {"key": "sealed_grace_window_ms", "value": "400"},
        {"key": "sweep_tick_ms", "value": "100"},
        {"key": "span_capacity", "value": "128"},
        {"key": "token_limit", "value": "100000"},
    ]

    try:
        proc = start_server(server_bin, db_path, bootstrap, frontend_dist=frontend_dist)
        wait_server_ready(old_url, args.ready_timeout, proc)
        post_config_patch(old_url, config_items)
        print("[settings-blackbox] 已写入冷启动配置，准备重启后验证真实生效")

        stop_server(proc)
        proc = None
        wait_old_port_closed(old_url, 3.0)

        # 第二次启动故意不传 --port，迫使 main.cpp 从配置快照里取 http_port。
        proc = start_server(server_bin, db_path, None, frontend_dist=frontend_dist)
        wait_server_ready(new_url, args.ready_timeout, proc)
        startup_logs = wait_process_log_contains(proc, "2 I/O threads, 2 worker threads", timeout_sec=3.0)
        assert_single_entry_frontend_routes(new_url)

        settings = fetch_all_settings(new_url)
        app_config = settings.get("config", {})
        if int(app_config.get("http_port", 0)) != configured:
            raise RuntimeError(f"http_port 回填不正确: expected={configured}, actual={app_config.get('http_port')}")
        if app_config.get("trace_end_aliases") != ["end"]:
            raise RuntimeError(f"trace_end_aliases 回填不正确: {app_config.get('trace_end_aliases')}")
        if bool(app_config.get("ai_analysis_enabled", True)) is not False:
            raise RuntimeError(f"ai_analysis_enabled 回填不正确: {app_config.get('ai_analysis_enabled')}")
        if int(app_config.get("ai_timeout_ms", 0)) != 30000:
            raise RuntimeError(f"ai_timeout_ms 回填不正确: {app_config.get('ai_timeout_ms')}")
        if int(app_config.get("kernel_io_threads", 0)) != 2:
            raise RuntimeError(f"kernel_io_threads 回填不正确: {app_config.get('kernel_io_threads')}")
        if int(app_config.get("kernel_worker_threads", 0)) != 2:
            raise RuntimeError(f"kernel_worker_threads 回填不正确: {app_config.get('kernel_worker_threads')}")
        if int(app_config.get("log_retention_days", 0)) != 1:
            raise RuntimeError(f"log_retention_days 回填不正确: {app_config.get('log_retention_days')}")
        if "Thread Model:" not in startup_logs:
            raise RuntimeError("未读到线程模型启动日志，无法证明 kernel_io_threads / kernel_worker_threads 被消费")
        # timeout_ms 直接出现在启动日志里，最适合作为冷启动消费的黑盒证据。
        # 这里只看日志，是因为真正值钱的问题是“后端到底有没有把 Settings 的超时拿来构造 TraceProxyAi”，
        # 而不是 SQLite 里有没有这行 key。
        if "timeout_ms=30000" not in startup_logs:
            raise RuntimeError("启动日志没有打印 timeout_ms=30000，无法证明 ai_timeout_ms 被真实消费")

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
                            frontend_dist=frontend_dist,
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

        # 这条黑盒故意不重启后端：
        # 前一条只证明冷启动时 model/api_key 能进请求体，这一条才证明运行中改设置后，
        # 下一次真正发 AI 时会重新吃到新值，而不是一直卡在 TraceProxyAi 构造时的旧缓存。
        probe_service.state.clear_trace_requests()
        post_config_patch(
            new_url,
            [
                {"key": "ai_model", "value": "hot-reload-model"},
                {"key": "ai_api_key", "value": "hot-reload-key"},
            ],
        )
        hot_reload_trace_id = send_trace_pair(
            new_url,
            int(time.time() * 1000) + 150,
            "webhook-warning-sentinel-hot-reload-model-api-key",
        )
        wait_trace_summary_status(db_path, hot_reload_trace_id, "completed", args.dispatch_timeout)
        assert_last_provider_request(
            probe_service.state,
            ["mock"],
            expected_model_by_provider={"mock": "hot-reload-model"},
            expected_api_key_by_provider={"mock": "hot-reload-key"},
        )
        # 热更新这条 trace 只负责锁 model/api_key，不负责复用下面的 webhook 阈值断言。
        # 这里把探针外发状态清干净，避免上一条场景残留把“warning 不该告警”的断言串脏。
        probe_service.state.clear_webhook_payloads()

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

        # 下面这 5 条是双 provider 黑盒的核心：
        # 我们不连真实云 API，而是让后端去打同一个本地 fake proxy。
        # 这样既能锁住“设置里的主/备 provider 到底有没有真实消费”，
        # 也不会把 CI 绑到外网、配额、真实模型耗时这些不稳定因素上。
        provider_model_map = {
            "gemini": "gemini-fake-model",
            "glm": "glm-fake-model",
        }
        provider_api_key_map = {
            "gemini": "gemini-fake-key",
            "glm": "glm-fake-key",
        }

        def configure_provider_pair(primary: str, fallback: str) -> None:
            post_config_patch(
                new_url,
                [
                    {"key": "ai_analysis_enabled", "value": "1"},
                    {"key": "ai_auto_degrade", "value": "1"},
                    {"key": "ai_provider", "value": primary},
                    {"key": "ai_model", "value": provider_model_map[primary]},
                    {"key": "ai_api_key", "value": provider_api_key_map[primary]},
                    {"key": "ai_fallback_provider", "value": fallback},
                    {"key": "ai_fallback_model", "value": provider_model_map[fallback]},
                    {"key": "ai_fallback_api_key", "value": provider_api_key_map[fallback]},
                ],
            )

        def configure_retry_only_provider(primary: str, *, retry_enabled: bool, retry_max_attempts: int) -> None:
            # 这两个场景故意把 auto_degrade 关掉。
            # 否则一旦主路失败，后端可能直接走 fallback，黑盒就没法证明“同一个 provider 自己到底有没有先重试”。
            post_config_patch(
                new_url,
                [
                    {"key": "ai_analysis_enabled", "value": "1"},
                    {"key": "ai_auto_degrade", "value": "0"},
                    {"key": "ai_provider", "value": primary},
                    {"key": "ai_model", "value": provider_model_map[primary]},
                    {"key": "ai_api_key", "value": provider_api_key_map[primary]},
                    {"key": "ai_retry_enabled", "value": "1" if retry_enabled else "0"},
                    {"key": "ai_retry_max_attempts", "value": str(retry_max_attempts)},
                ],
            )

        # 场景 1：gemini 主路成功，glm 不应该被调用。
        configure_provider_pair("gemini", "glm")
        proc = restart_server_with_fake_proxy(proc, server_bin, db_path, frontend_dist, new_url, args.ready_timeout, probe_service, args.proxy_timeout_ms)
        probe_service.state.clear_trace_requests()
        probe_service.state.set_provider_behavior("gemini", build_probe_success_payload("gemini", risk_level="warning"))
        probe_service.state.set_provider_behavior("glm", build_probe_success_payload("glm", risk_level="warning"))
        gemini_primary_trace_id = send_trace_pair(new_url, int(time.time() * 1000) + 300, "gemini-primary-success")
        wait_trace_summary_status(db_path, gemini_primary_trace_id, "completed", args.dispatch_timeout)
        if query_trace_analysis_count(db_path, gemini_primary_trace_id) != 1:
            raise RuntimeError("gemini 主路成功场景应该产出 1 条 trace_analysis")
        assert_last_provider_request(
            probe_service.state,
            ["gemini"],
            expected_model_by_provider=provider_model_map,
            expected_api_key_by_provider=provider_api_key_map,
        )

        # 场景 2：glm 主路成功，gemini 不应该被调用。
        configure_provider_pair("glm", "gemini")
        proc = restart_server_with_fake_proxy(proc, server_bin, db_path, frontend_dist, new_url, args.ready_timeout, probe_service, args.proxy_timeout_ms)
        probe_service.state.clear_trace_requests()
        probe_service.state.set_provider_behavior("glm", build_probe_success_payload("glm", risk_level="warning"))
        probe_service.state.set_provider_behavior("gemini", build_probe_success_payload("gemini", risk_level="warning"))
        glm_primary_trace_id = send_trace_pair(new_url, int(time.time() * 1000) + 400, "glm-primary-success")
        wait_trace_summary_status(db_path, glm_primary_trace_id, "completed", args.dispatch_timeout)
        if query_trace_analysis_count(db_path, glm_primary_trace_id) != 1:
            raise RuntimeError("glm 主路成功场景应该产出 1 条 trace_analysis")
        assert_last_provider_request(
            probe_service.state,
            ["glm"],
            expected_model_by_provider=provider_model_map,
            expected_api_key_by_provider=provider_api_key_map,
        )

        # 场景 3：gemini 主路失败，glm fallback 成功。
        configure_provider_pair("gemini", "glm")
        proc = restart_server_with_fake_proxy(proc, server_bin, db_path, frontend_dist, new_url, args.ready_timeout, probe_service, args.proxy_timeout_ms)
        probe_service.state.clear_trace_requests()
        probe_service.state.set_provider_behavior(
            "gemini",
            build_probe_failure_payload("gemini", error_status="PRIMARY_TIMEOUT", error_message="gemini primary failed"),
        )
        probe_service.state.set_provider_behavior("glm", build_probe_success_payload("glm", risk_level="warning"))
        gemini_fallback_trace_id = send_trace_pair(new_url, int(time.time() * 1000) + 500, "gemini-fallback-to-glm")
        wait_trace_summary_status(db_path, gemini_fallback_trace_id, "completed", args.dispatch_timeout)
        if query_trace_analysis_count(db_path, gemini_fallback_trace_id) != 1:
            raise RuntimeError("gemini->glm 自动降级场景应该产出 1 条 trace_analysis")
        assert_last_provider_request(
            probe_service.state,
            ["gemini", "glm"],
            expected_model_by_provider=provider_model_map,
            expected_api_key_by_provider=provider_api_key_map,
        )

        # 场景 3.5：provider 路由仍然冷启动，但 fallback 的 model/api_key 应该支持运行中热更新。
        # 所以这里先用重启把 gemini->glm 这条主备路由钉死，然后不再重启服务，只改 fallback 凭证。
        hot_reload_fallback_model = "glm-hot-fallback-model"
        hot_reload_fallback_api_key = "glm-hot-fallback-key"
        post_config_patch(
            new_url,
            [
                {"key": "ai_fallback_model", "value": hot_reload_fallback_model},
                {"key": "ai_fallback_api_key", "value": hot_reload_fallback_api_key},
            ],
        )
        probe_service.state.clear_trace_requests()
        probe_service.state.set_provider_behavior(
            "gemini",
            build_probe_failure_payload("gemini", error_status="PRIMARY_TIMEOUT", error_message="gemini primary failed"),
        )
        probe_service.state.set_provider_behavior("glm", build_probe_success_payload("glm", risk_level="warning"))
        gemini_hot_reload_fallback_trace_id = send_trace_pair(
            new_url,
            int(time.time() * 1000) + 550,
            "gemini-fallback-hot-reload-to-glm",
        )
        wait_trace_summary_status(db_path, gemini_hot_reload_fallback_trace_id, "completed", args.dispatch_timeout)
        if query_trace_analysis_count(db_path, gemini_hot_reload_fallback_trace_id) != 1:
            raise RuntimeError("fallback 热更新场景应该产出 1 条 trace_analysis")
        assert_last_provider_request(
            probe_service.state,
            ["gemini", "glm"],
            expected_model_by_provider={
                "gemini": provider_model_map["gemini"],
                "glm": hot_reload_fallback_model,
            },
            expected_api_key_by_provider={
                "gemini": provider_api_key_map["gemini"],
                "glm": hot_reload_fallback_api_key,
            },
        )

        # 场景 4：glm 主路失败，gemini fallback 成功。
        configure_provider_pair("glm", "gemini")
        proc = restart_server_with_fake_proxy(proc, server_bin, db_path, frontend_dist, new_url, args.ready_timeout, probe_service, args.proxy_timeout_ms)
        probe_service.state.clear_trace_requests()
        probe_service.state.set_provider_behavior(
            "glm",
            build_probe_failure_payload("glm", error_status="PRIMARY_TIMEOUT", error_message="glm primary failed"),
        )
        probe_service.state.set_provider_behavior("gemini", build_probe_success_payload("gemini", risk_level="warning"))
        glm_fallback_trace_id = send_trace_pair(new_url, int(time.time() * 1000) + 600, "glm-fallback-to-gemini")
        wait_trace_summary_status(db_path, glm_fallback_trace_id, "completed", args.dispatch_timeout)
        if query_trace_analysis_count(db_path, glm_fallback_trace_id) != 1:
            raise RuntimeError("glm->gemini 自动降级场景应该产出 1 条 trace_analysis")
        assert_last_provider_request(
            probe_service.state,
            ["glm", "gemini"],
            expected_model_by_provider=provider_model_map,
            expected_api_key_by_provider=provider_api_key_map,
        )

        # 场景 5：主备都失败，最终状态必须是 failed_both，且 ai_error 要带上双边失败信息。
        configure_provider_pair("glm", "gemini")
        proc = restart_server_with_fake_proxy(proc, server_bin, db_path, frontend_dist, new_url, args.ready_timeout, probe_service, args.proxy_timeout_ms)
        probe_service.state.clear_trace_requests()
        probe_service.state.set_provider_behavior(
            "glm",
            build_probe_failure_payload("glm", error_status="PRIMARY_TIMEOUT", error_message="glm primary failed"),
        )
        probe_service.state.set_provider_behavior(
            "gemini",
            build_probe_failure_payload("gemini", error_status="FALLBACK_TIMEOUT", error_message="gemini fallback failed"),
        )
        failed_both_trace_id = send_trace_pair(new_url, int(time.time() * 1000) + 700, "glm-gemini-both-failed")
        wait_trace_summary_status(db_path, failed_both_trace_id, "failed_both", args.dispatch_timeout)
        if query_trace_analysis_count(db_path, failed_both_trace_id) != 0:
            raise RuntimeError("主备都失败场景不应该产出 trace_analysis")
        failed_both_summary = query_trace_summary_row(db_path, failed_both_trace_id)
        if not failed_both_summary:
            raise RuntimeError("主备都失败场景没有查到 trace_summary")
        if "primary:" not in failed_both_summary["ai_error"] or "fallback:" not in failed_both_summary["ai_error"]:
            raise RuntimeError(f"failed_both 的 ai_error 没带上双边失败信息: {failed_both_summary['ai_error']}")
        assert_last_provider_request(
            probe_service.state,
            ["glm", "gemini"],
            expected_model_by_provider=provider_model_map,
            expected_api_key_by_provider=provider_api_key_map,
        )

        # 场景 6：同一个 provider 的第一次 429 失败后，应该先在主路内部重试，再成功完成。
        configure_retry_only_provider("glm", retry_enabled=True, retry_max_attempts=3)
        proc = restart_server_with_fake_proxy(proc, server_bin, db_path, frontend_dist, new_url, args.ready_timeout, probe_service, args.proxy_timeout_ms)
        probe_service.state.clear_trace_requests()
        probe_service.state.set_provider_behavior_sequence(
            "glm",
            [
                build_probe_failure_payload("glm", error_status="HTTP_ERROR", error_message="glm rate limited", error_code=429),
                build_probe_success_payload("glm", risk_level="warning"),
            ],
        )
        retry_success_trace_id = send_trace_pair(new_url, int(time.time() * 1000) + 800, "glm-retry-success")
        wait_trace_summary_status(db_path, retry_success_trace_id, "completed", args.dispatch_timeout)
        if query_trace_analysis_count(db_path, retry_success_trace_id) != 1:
            raise RuntimeError("429 -> success 重试场景应该产出 1 条 trace_analysis")
        assert_last_provider_request(
            probe_service.state,
            ["glm", "glm"],
            expected_model_by_provider=provider_model_map,
            expected_api_key_by_provider=provider_api_key_map,
        )
        retry_success_requests = probe_service.state.snapshot_trace_requests()
        if any(bool(item.get("retry_enabled")) is not True for item in retry_success_requests):
            raise RuntimeError("429 -> success 场景里 proxy 实收 retry_enabled 不为 true")
        if any(int(item.get("retry_max_attempts", 0)) != 3 for item in retry_success_requests):
            raise RuntimeError("429 -> success 场景里 proxy 实收 retry_max_attempts 不为 3")

        # 场景 7：401 属于确定性鉴权失败，不允许重试。
        # 这里把第二个响应故意配成 success，就是为了证明“如果真的偷偷重试了，这条用例会被误判成 completed”。
        configure_retry_only_provider("glm", retry_enabled=True, retry_max_attempts=3)
        proc = restart_server_with_fake_proxy(proc, server_bin, db_path, frontend_dist, new_url, args.ready_timeout, probe_service, args.proxy_timeout_ms)
        probe_service.state.clear_trace_requests()
        probe_service.state.set_provider_behavior_sequence(
            "glm",
            [
                build_probe_failure_payload("glm", error_status="HTTP_ERROR", error_message="glm bad key", error_code=401),
                build_probe_success_payload("glm", risk_level="warning"),
            ],
        )
        retry_reject_trace_id = send_trace_pair(new_url, int(time.time() * 1000) + 900, "glm-no-retry-401")
        wait_trace_summary_status(db_path, retry_reject_trace_id, "failed_primary", args.dispatch_timeout)
        if query_trace_analysis_count(db_path, retry_reject_trace_id) != 0:
            raise RuntimeError("401 不重试场景不应该产出 trace_analysis")
        retry_reject_summary = query_trace_summary_row(db_path, retry_reject_trace_id)
        if not retry_reject_summary:
            raise RuntimeError("401 不重试场景没有查到 trace_summary")
        if "401" not in retry_reject_summary["ai_error"]:
            raise RuntimeError(f"401 不重试场景的 ai_error 没带上 401 线索: {retry_reject_summary['ai_error']}")
        assert_last_provider_request(
            probe_service.state,
            ["glm"],
            expected_model_by_provider=provider_model_map,
            expected_api_key_by_provider=provider_api_key_map,
        )

        # 场景 8：CLI `--disable-ai` 必须盖过 SQLite 里已经保存好的 ai_analysis_enabled=1。
        # 这条黑盒锁的是 benchmark 开关优先级，不是普通 Settings 冷启动消费。
        proc = restart_server_with_fake_proxy(
            proc,
            server_bin,
            db_path,
            frontend_dist,
            new_url,
            args.ready_timeout,
            probe_service,
            args.proxy_timeout_ms,
            extra_args=["--disable-ai"],
        )
        probe_service.state.clear_trace_requests()
        probe_service.state.clear_webhook_payloads()
        disabled_ai_trace_id = send_trace_pair(new_url, int(time.time() * 1000) + 1000, "cli-disable-ai")
        wait_trace_summary_status(db_path, disabled_ai_trace_id, "skipped_manual", args.dispatch_timeout)
        if query_trace_analysis_count(db_path, disabled_ai_trace_id) != 0:
            raise RuntimeError("CLI --disable-ai 场景不应该产出 trace_analysis")
        if probe_service.state.snapshot_trace_requests():
            raise RuntimeError("CLI --disable-ai 场景不应该向 fake proxy 发任何 AI 请求")
        disabled_ai_logs = wait_process_log_contains(proc, "Trace AI disabled.", timeout_sec=3.0)
        if "--disable-ai" not in disabled_ai_logs and "disabled_by_cli" not in disabled_ai_logs:
            raise RuntimeError("CLI --disable-ai 场景启动日志没有明确标出 CLI 覆盖")

        # 场景 9：CLI `--disable-webhook` 必须盖过已经配置好的飞书 channel。
        # 这里仍然让 AI 正常生成 critical，目的是证明被关掉的是“通知外发”，不是整条分析链。
        probe_service.state.set_provider_behavior("mock", build_probe_success_payload("mock", risk_level="critical"))
        proc = restart_server_with_fake_proxy(
            proc,
            server_bin,
            db_path,
            frontend_dist,
            new_url,
            args.ready_timeout,
            probe_service,
            args.proxy_timeout_ms,
            extra_args=["--disable-webhook"],
        )
        probe_service.state.clear_trace_requests()
        probe_service.state.clear_webhook_payloads()
        disabled_webhook_trace_id = send_trace_pair(new_url, int(time.time() * 1000) + 1100, "cli-disable-webhook")
        wait_trace_summary_status(db_path, disabled_webhook_trace_id, "completed", args.dispatch_timeout)
        if query_trace_analysis_count(db_path, disabled_webhook_trace_id) != 1:
            raise RuntimeError("CLI --disable-webhook 场景应该仍然产出 trace_analysis")
        wait_until(lambda: len(probe_service.state.snapshot_trace_requests()) >= 1,
                   timeout_sec=args.dispatch_timeout)
        time.sleep(1.0)
        if probe_service.state.snapshot_webhook_payloads():
            raise RuntimeError("CLI --disable-webhook 场景不应该再有任何 webhook 外发")
        disabled_webhook_logs = wait_process_log_contains(proc, "Webhook notifier", timeout_sec=3.0)
        if "disabled" not in disabled_webhook_logs.lower():
            raise RuntimeError("CLI --disable-webhook 场景启动日志没有明确标出 webhook 被 CLI 关闭")

        # 场景 10：CLI `--disable-buffered-trace-repo` 必须切到 no-buffer 写入口。
        # 这条黑盒锁的是“主数据/analysis 不再先进后台 flush 线程”，不是 AI、告警或别的冷启动配置。
        # 这里显式把 provider 再钉回 glm，避免复用上一条场景残留的配置状态。
        # 否则测试表面上是在验证 no-buffer，实际却可能因为 provider 行为没配对而串成假失败。
        configure_retry_only_provider("glm", retry_enabled=True, retry_max_attempts=3)
        probe_service.state.set_provider_behavior("glm", build_probe_success_payload("glm", risk_level="warning"))
        proc = restart_server_with_fake_proxy(
            proc,
            server_bin,
            db_path,
            frontend_dist,
            new_url,
            args.ready_timeout,
            probe_service,
            args.proxy_timeout_ms,
            extra_args=["--disable-buffered-trace-repo"],
        )
        probe_service.state.clear_trace_requests()
        probe_service.state.clear_webhook_payloads()
        no_buffer_trace_id = send_trace_pair(new_url, int(time.time() * 1000) + 1200, "cli-disable-buffered-trace-repo")
        wait_trace_summary_status(db_path, no_buffer_trace_id, "completed", args.dispatch_timeout)
        if query_trace_analysis_count(db_path, no_buffer_trace_id) != 1:
            raise RuntimeError("CLI --disable-buffered-trace-repo 场景应该仍然产出 trace_analysis")
        no_buffer_logs = wait_process_log_contains(proc, "Trace persistence mode:", timeout_sec=3.0)
        if "no-buffer" not in no_buffer_logs.lower() and "direct" not in no_buffer_logs.lower():
            raise RuntimeError("CLI --disable-buffered-trace-repo 场景启动日志没有明确标出 no-buffer 写入口")

        # 场景 11：CLI `--trace-lifecycle-profile minimal` 必须盖过 SQLite 里已经保存好的 `protected`。
        # 这条黑盒不能只看启动日志，因为第三刀真正要证明的是“状态机分支被改了”，不是 main.cpp 打了个假字符串。
        # 所以这里专门构造一条最短时间线：
        # 1. 先发一个自带 trace_end 的根 span，让 session 立刻命中结束条件；
        # 2. 再在下一次 sweep 前补一条晚到 span；
        # 3. 如果运行时还是 `protected`，晚到 span 会在 sealed grace 里被吸收，summary.span_count=2；
        # 4. 如果 CLI 真把它盖成了 `minimal`，session 会直接变成 ready，晚到 span 不再并入，summary.span_count=1。
        post_config_patch(
            new_url,
            [
                {"key": "trace_lifecycle_profile", "value": "protected"},
                {"key": "ai_analysis_enabled", "value": "0"},
                {"key": "collecting_idle_timeout_ms", "value": "30000"},
                {"key": "sealed_grace_window_ms", "value": "1500"},
                {"key": "sweep_tick_ms", "value": "1500"},
            ],
        )
        proc = restart_server_with_fake_proxy(
            proc,
            server_bin,
            db_path,
            frontend_dist,
            new_url,
            args.ready_timeout,
            probe_service,
            args.proxy_timeout_ms,
            extra_args=["--trace-lifecycle-profile", "minimal"],
        )
        lifecycle_settings = fetch_all_settings(new_url)
        if lifecycle_settings.get("config", {}).get("trace_lifecycle_profile") != "protected":
            raise RuntimeError("第三刀黑盒前置条件失败：SQLite 中的 trace_lifecycle_profile 没有保持为 protected")
        lifecycle_logs = wait_process_log_contains(proc, "trace_lifecycle_profile=", timeout_sec=3.0)
        if "trace_lifecycle_profile=minimal" not in lifecycle_logs:
            raise RuntimeError("CLI --trace-lifecycle-profile 场景启动日志没有明确标出 minimal 覆盖")

        lifecycle_trace_key = int(time.time() * 1000) + 1300
        lifecycle_trace_id = str(lifecycle_trace_key)
        lifecycle_root = {
            "trace_key": lifecycle_trace_key,
            "span_id": lifecycle_trace_key + 1,
            "start_time_ms": 1700000013000,
            "end_time_ms": 1700000013100,
            "name": "cli-lifecycle-root-end",
            "service_name": "settings-blackbox-service",
            "status": "OK",
            "trace_end": True,
        }
        lifecycle_late_span = {
            "trace_key": lifecycle_trace_key,
            "span_id": lifecycle_trace_key + 2,
            "parent_span_id": lifecycle_trace_key + 1,
            "start_time_ms": 1700000013110,
            "end_time_ms": 1700000013200,
            "name": "cli-lifecycle-late-span",
            "service_name": "settings-blackbox-service",
            "status": "ERROR",
        }
        post_span(new_url, lifecycle_root)
        time.sleep(0.05)
        post_span(new_url, lifecycle_late_span)
        wait_trace_summary_status(db_path, lifecycle_trace_id, "skipped_manual", args.dispatch_timeout)
        lifecycle_summary = query_trace_summary_row(db_path, lifecycle_trace_id)
        if not lifecycle_summary:
            raise RuntimeError("CLI --trace-lifecycle-profile 场景没有查到 trace_summary")
        if lifecycle_summary["span_count"] != 1:
            raise RuntimeError(
                "CLI --trace-lifecycle-profile 场景没有跑出 minimal 行为："
                f"expected span_count=1, actual={lifecycle_summary['span_count']}"
            )
        if query_trace_analysis_count(db_path, lifecycle_trace_id) != 0:
            raise RuntimeError("CLI --trace-lifecycle-profile + ai_analysis_enabled=0 场景不应该产出 trace_analysis")

        print(
            "[settings-blackbox] 黑盒联调通过：端口切换、trace_end_aliases、"
            "ai_analysis_enabled、ai_timeout_ms、主路与 fallback 的 model/api_key 热更新、prompt/active_prompt_id、webhook channel、"
            "kernel_io_threads、kernel_worker_threads、log_retention_days、双 provider/fallback、AI retry、"
            "trace_lifecycle_profile CLI 覆盖、"
            "benchmark CLI 开关都已验证"
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
        if args.keep_artifacts:
            print(f"[settings-blackbox] 保留前端临时目录用于排障: {frontend_dist}")
        else:
            shutil.rmtree(frontend_dist, ignore_errors=True)
        if probe_service is not None:
            probe_service.stop()


def main() -> int:
    return run_flow(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
