#!/usr/bin/env python3
"""
最小 Trace 冒烟脚本（requests 版）

目标：仅验证 `/logs/spans` 的最短主链路是否打通：
1) 服务可启动并可接收请求
2) 连续发送 2 个 span（第 2 个 trace_end=true 触发分发）
3) trace_summary 与 trace_span 落库成功
4) 子 span 的 parent_id 关系正确

说明：
- 该脚本默认不启动 AI proxy，也不验证 webhook；用于“基础可用链路”快速确认。
- 为避免污染开发数据库，默认写入 /tmp 下独立 db 文件。
"""

from __future__ import annotations

import argparse
import sqlite3
import subprocess
import sys
import time
from pathlib import Path

import requests


def parse_args() -> argparse.Namespace:
    """
    解析命令行参数，并给出“安全默认值”。

    为什么单独封装成函数：
    - 把配置入口集中到一处，后续改端口/超时时不需要在业务代码里到处找常量。
    - 脚本既能本地一键跑，也能在 CI 里通过参数覆盖路径与端口。

    返回值：
    - argparse.Namespace：包含 server-bin、db、port、timeout 等配置。
    """
    server_dir = Path(__file__).resolve().parents[1]
    default_bin = server_dir / "build" / "LogSentinel"
    default_db = Path("/tmp") / f"logsentinel_trace_smoke_{int(time.time())}.db"

    parser = argparse.ArgumentParser(description="/logs/spans 最小冒烟脚本")
    parser.add_argument("--server-bin", default=str(default_bin), help="LogSentinel 可执行文件路径")
    parser.add_argument("--db", default=str(default_db), help="临时数据库路径")
    parser.add_argument("--port", type=int, default=18080, help="服务端口")
    parser.add_argument("--ready-timeout", type=float, default=10.0, help="服务就绪等待超时（秒）")
    parser.add_argument("--db-timeout", type=float, default=10.0, help="数据库落库等待超时（秒）")
    return parser.parse_args()


def start_server(server_bin: Path, db_path: Path, port: int) -> subprocess.Popen:
    """
    启动被测后端进程，并返回进程句柄。

    为什么返回 Popen：
    - 后续需要在 finally 中统一 terminate/kill，避免脚本异常时残留僵尸进程。
    - 冒烟失败时可以读取 stdout 作为第一手排障线索。
    """
    if not server_bin.exists():
        raise FileNotFoundError(f"未找到服务可执行文件: {server_bin}")

    # 使用独立 db + 端口启动，目的是把冒烟测试与本地开发环境隔离，避免相互污染。
    cmd = [str(server_bin), "--db", str(db_path), "--port", str(port)]
    print(f"[smoke] 启动服务: {' '.join(cmd)}")
    # subprocess.Popen：创建子进程但不阻塞当前脚本，便于后续继续做探活与发请求。
    proc = subprocess.Popen(
        cmd,
        # stdout/stderr 合并到同一管道，失败时只需要读一处日志，排障更直接。
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        # text=True：把输出按文本处理，避免后续 decode 逻辑分散在异常路径里。
        text=True,
    )
    return proc


def wait_server_ready(base_url: str, timeout_sec: float) -> None:
    """
    轮询服务就绪状态，直到可响应 HTTP 或超时。

    为什么不用固定 sleep：
    - 固定 sleep 过短会误报失败，过长会拖慢测试。
    - 轮询能兼顾稳定性和速度：服务一就绪就继续执行。
    """
    deadline = time.time() + timeout_sec
    last_error = None

    # 用不存在的路径探活：只要能收到任意 HTTP 响应（哪怕 404）就说明服务已经监听成功。
    while time.time() < deadline:
        try:
            # requests.get：发送最轻量探活请求，失败抛异常，成功返回 Response。
            resp = requests.get(f"{base_url}/__smoke_ready__", timeout=0.8)
            if resp.status_code:
                return
        except requests.RequestException as exc:
            # 记录最后一次异常，用于超时后给出更可定位的信息。
            last_error = exc
        time.sleep(0.2)

    raise RuntimeError(f"服务未在 {timeout_sec}s 内就绪，最后错误: {last_error}")


def post_span(base_url: str, payload: dict) -> dict:
    """
    发送单条 span 到 /logs/spans，并校验最小成功条件。

    为什么这里就做状态码校验：
    - 冒烟要尽早失败，减少“后面查库失败但根因其实是请求没收下”的排障时间。
    """
    # requests.post(..., json=payload)：自动序列化 JSON 并加 Content-Type: application/json。
    resp = requests.post(f"{base_url}/logs/spans", json=payload, timeout=2.0)
    if resp.status_code != 202:
        raise RuntimeError(f"POST /logs/spans 失败: status={resp.status_code}, body={resp.text}")
    try:
        # resp.json()：把响应体反序列化为 dict，便于后续断言和打印。
        return resp.json()
    except ValueError:
        raise RuntimeError(f"响应不是合法 JSON: {resp.text}")


def query_db_counts(db_path: Path, trace_id: str) -> tuple[int, int]:
    """
    查询目标 trace 在 summary/span 两张表的记录数。

    返回值：
    - (summary_count, span_count)
    """
    # sqlite3.connect：打开数据库连接。这里每次短连接是为了让函数无副作用，调用更安全。
    conn = sqlite3.connect(str(db_path))
    try:
        cur = conn.cursor()
        # cur.execute：执行参数化 SQL，避免字符串拼接带来的转义与安全问题。
        cur.execute("SELECT COUNT(*) FROM trace_summary WHERE trace_id = ?", (trace_id,))
        # fetchone：读取单行结果；COUNT(*) 一定返回一行。
        summary_count = int(cur.fetchone()[0])
        cur.execute("SELECT COUNT(*) FROM trace_span WHERE trace_id = ?", (trace_id,))
        span_count = int(cur.fetchone()[0])
        return summary_count, span_count
    finally:
        # 无论成功或异常都关闭连接，避免文件句柄泄漏。
        conn.close()


def query_parent_id(db_path: Path, span_id: str) -> str | None:
    """
    查询某个子 span 的 parent_id，用于验证父子关系是否拼接正确。

    返回值：
    - str：找到 parent_id
    - None：没查到该 span 或 parent_id 为空
    """
    conn = sqlite3.connect(str(db_path))
    try:
        cur = conn.cursor()
        cur.execute("SELECT parent_id FROM trace_span WHERE span_id = ?", (span_id,))
        row = cur.fetchone()
        if not row:
            return None
        return row[0]
    finally:
        conn.close()


def wait_db_ready(db_path: Path, trace_id: str, timeout_sec: float) -> None:
    """
    轮询数据库直到目标 trace 达到“最小落库完成”条件。

    最小条件：
    - trace_summary 至少 1 条
    - trace_span 至少 2 条

    为什么要轮询：
    - TraceSessionManager 的分发与落库是异步线程执行，请求返回后数据库不一定立刻可见。
    """
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        if db_path.exists():
            try:
                summary_count, span_count = query_db_counts(db_path, trace_id)
                if summary_count >= 1 and span_count >= 2:
                    return
            except sqlite3.Error:
                # 数据库可能还在初始化或写锁阶段，短暂重试即可。
                pass
        time.sleep(0.25)
    raise RuntimeError(f"数据库在 {timeout_sec}s 内未达到预期记录数")


def cleanup(proc: subprocess.Popen | None, db_path: Path) -> None:
    """
    清理测试现场：停止服务进程并删除临时 SQLite 文件。

    为什么做强制清理：
    - 保证脚本可重复执行，避免历史残留影响下一次冒烟结果。
    """
    if proc is not None:
        # terminate：先发温和终止信号，给服务机会做正常收尾。
        proc.terminate()
        try:
            # wait：等待进程退出，防止脚本先结束但子进程仍存活。
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            # kill：温和退出失败后强制结束，避免卡死占端口。
            proc.kill()
            proc.wait(timeout=3)

    # SQLite 可能同时生成 -wal/-shm，清理这些文件是为了避免重复执行时受历史状态干扰。
    for suffix in ["", "-wal", "-shm"]:
        p = Path(str(db_path) + suffix)
        if p.exists():
            p.unlink()


def main() -> int:
    """
    冒烟测试主流程编排函数。

    流程：
    1) 解析参数
    2) 启动服务并等待就绪
    3) 发送 root/child 两个 span（child 带 trace_end=true）
    4) 轮询并验证数据库结果
    5) 清理资源并返回退出码
    """
    args = parse_args()
    server_bin = Path(args.server_bin).resolve()
    db_path = Path(args.db).resolve()
    base_url = f"http://127.0.0.1:{args.port}"

    trace_key = int(time.time() * 1000) % 1000000000
    root_span_id = trace_key + 1
    child_span_id = trace_key + 2

    # 两个 span 属于同一 trace，第 2 个带 trace_end=true 用于触发分发落库。
    span_root = {
        "trace_key": trace_key,
        "span_id": root_span_id,
        "start_time_ms": 1700000000000,
        "end_time_ms": 1700000000100,
        "name": "root-span",
        "service_name": "smoke-service",
        "status": "OK",
    }
    span_child = {
        "trace_key": trace_key,
        "span_id": child_span_id,
        "parent_span_id": root_span_id,
        "start_time_ms": 1700000000110,
        "end_time_ms": 1700000000200,
        "name": "child-span",
        "service_name": "smoke-service",
        "status": "ERROR",
        "trace_end": True,
    }

    proc = None
    try:
        proc = start_server(server_bin, db_path, args.port)
        wait_server_ready(base_url, args.ready_timeout)
        print("[smoke] 服务已就绪")

        resp1 = post_span(base_url, span_root)
        resp2 = post_span(base_url, span_child)
        print(f"[smoke] span1 accepted: {resp1}")
        print(f"[smoke] span2 accepted: {resp2}")

        trace_id = str(trace_key)
        wait_db_ready(db_path, trace_id, args.db_timeout)

        summary_count, span_count = query_db_counts(db_path, trace_id)
        if summary_count != 1:
            raise RuntimeError(f"trace_summary 条数异常: {summary_count}（期望 1）")
        if span_count != 2:
            raise RuntimeError(f"trace_span 条数异常: {span_count}（期望 2）")

        parent_id = query_parent_id(db_path, str(child_span_id))
        if parent_id != str(root_span_id):
            raise RuntimeError(
                f"父子关系异常: child={child_span_id}, parent_id={parent_id}, 期望={root_span_id}"
            )

        print("[smoke] ✅ 最小链路通过：接收 -> 聚合触发 -> 落库 -> 父子关系")
        return 0
    except Exception as exc:
        print(f"[smoke] ❌ 失败: {exc}")
        if proc and proc.poll() is not None and proc.stdout:
            # 失败时输出部分服务日志，便于第一时间定位原因。
            try:
                logs = proc.stdout.read()
                if logs:
                    print("[smoke] 服务输出:\n" + logs)
            except Exception:
                pass
        return 1
    finally:
        cleanup(proc, db_path)


if __name__ == "__main__":
    sys.exit(main())
