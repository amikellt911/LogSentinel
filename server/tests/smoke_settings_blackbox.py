#!/usr/bin/env python3
"""
Settings 第三层黑盒联调脚本。

这层测试不再盯类级别行为，而是走真实后端进程：
1. 先通过 `/settings/config` 写入冷启动配置；
2. 再重启后端；
3. 最后通过端口变化、真实 `/logs/spans` 请求和 SQLite 落库结果，证明配置确实被消费。
"""

from __future__ import annotations

import argparse
import json
import os
import select
import sqlite3
import subprocess
import time
from pathlib import Path
from typing import Optional

import requests


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
    parser.add_argument("--keep-artifacts", action="store_true", help="失败后保留临时数据库文件")
    return parser.parse_args()


def base_url(port: int) -> str:
    return f"http://127.0.0.1:{port}"


def start_server(server_bin: Path, db_path: Path, port: Optional[int]) -> subprocess.Popen:
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


def run_flow(args: argparse.Namespace) -> int:
    server_bin = Path(args.server_bin).resolve()
    db_path = Path(args.db).resolve()
    bootstrap = args.bootstrap_port
    configured = args.configured_port
    proc: Optional[subprocess.Popen] = None

    if bootstrap == configured:
        raise ValueError("bootstrap-port 与 configured-port 不能相同，否则无法证明端口切换生效")

    trace_key = int(time.time() * 1000)
    trace_id = str(trace_key)
    old_url = base_url(bootstrap)
    new_url = base_url(configured)

    # 这里把 idle timeout 拉大、sealed grace 压小：
    # - 如果 alias 没生效，这条 trace 在当前等待窗口内不应该被 idle timeout 收走；
    # - 如果 alias 生效，就会很快进入 sealed 并被 sweep 推进落库。
    config_items = [
        {"key": "http_port", "value": str(configured)},
        {"key": "trace_end_aliases", "value": json.dumps(["end"])},
        {"key": "ai_analysis_enabled", "value": "0"},
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

        settings = fetch_all_settings(new_url)
        app_config = settings.get("config", {})
        if int(app_config.get("http_port", 0)) != configured:
            raise RuntimeError(f"http_port 回填不正确: expected={configured}, actual={app_config.get('http_port')}")
        if app_config.get("trace_end_aliases") != ["end"]:
            raise RuntimeError(f"trace_end_aliases 回填不正确: {app_config.get('trace_end_aliases')}")
        if bool(app_config.get("ai_analysis_enabled", True)) is not False:
            raise RuntimeError(f"ai_analysis_enabled 回填不正确: {app_config.get('ai_analysis_enabled')}")

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

        print("[settings-blackbox] 黑盒联调通过：端口切换、trace_end_aliases、ai_analysis_enabled 都已验证")
        return 0
    except Exception as exc:
        logs = read_available_process_logs(proc)
        print(f"[settings-blackbox] 失败: {exc}")
        if logs:
            print("[settings-blackbox] 服务日志片段：")
            print(logs)
        return 1
    finally:
        cleanup(proc, db_path, args.keep_artifacts)


def main() -> int:
    return run_flow(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
