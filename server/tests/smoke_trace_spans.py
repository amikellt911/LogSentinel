#!/usr/bin/env python3
"""
Trace 冒烟脚本（requests 版，双模式）

模式说明：
1) basic：基础冒烟，仅验证 接收 -> 聚合触发 -> 落库 -> 父子关系
2) advanced：增强冒烟，在 basic 基础上验证 AI 分析落库（trace_analysis）

设计原则：
- 同一个脚本支持两层测试，减少重复代码，方便 CI 以两条命令分开执行。
- 默认使用独立端口 + 临时数据库，避免污染本地开发环境。
"""

from __future__ import annotations

import argparse
import os
import select
import sqlite3
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional, Tuple

import requests


def parse_args() -> argparse.Namespace:
    """
    解析命令行参数并返回配置对象。

    为什么要做 mode 参数：
    - basic 与 advanced 的依赖强度不同（advanced 依赖 AI/Webhook 子进程）。
    - 在 CI 里可以把两层冒烟拆成两个独立 job，失败定位更快。
    """
    server_dir = Path(__file__).resolve().parents[1]
    default_bin = server_dir / "build" / "LogSentinel"
    default_db = Path("/tmp") / f"logsentinel_trace_smoke_{int(time.time())}.db"

    parser = argparse.ArgumentParser(description="Trace 冒烟脚本（basic/advanced）")
    parser.add_argument("--mode",
                        choices=["basic", "advanced"],
                        default="basic",
                        help="冒烟模式：basic=基础链路，advanced=基础+AI分析")
    parser.add_argument("--server-bin", default=str(default_bin), help="LogSentinel 可执行文件路径")
    parser.add_argument("--db", default=str(default_db), help="临时数据库路径")
    parser.add_argument("--port", type=int, default=18080, help="服务端口")
    parser.add_argument("--ready-timeout", type=float, default=10.0, help="服务就绪等待超时（秒）")
    parser.add_argument("--db-timeout", type=float, default=10.0, help="基础落库等待超时（秒）")
    parser.add_argument("--analysis-timeout", type=float, default=12.0, help="analysis 落库等待超时（秒，仅 advanced）")
    parser.add_argument("--keep-artifacts",
                        action="store_true",
                        help="失败后保留临时数据库文件，便于排障")
    return parser.parse_args()


def start_server(server_bin: Path, db_path: Path, port: int, enable_dev_deps: bool) -> subprocess.Popen:
    """
    启动 LogSentinel 进程并返回句柄。

    enable_dev_deps=True 时会追加 --auto-start-deps：
    - 让服务自动拉起 AI proxy 与 webhook mock，
    - 避免 advanced 冒烟依赖人工先开多个终端。
    """
    if not server_bin.exists():
        raise FileNotFoundError(f"未找到服务可执行文件: {server_bin}")

    cmd = [str(server_bin), "--db", str(db_path), "--port", str(port)]
    if enable_dev_deps:
        cmd.append("--auto-start-deps")

    print(f"[smoke] 启动服务: {' '.join(cmd)}")
    # 用 PIPE 捕获输出是为了失败时能拿到第一手日志，减少“黑盒失败”的排障成本。
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return proc


def wait_server_ready(base_url: str, timeout_sec: float, proc: subprocess.Popen) -> None:
    """
    轮询等待服务监听成功。

    这里不依赖健康检查端点，只要返回任意 HTTP 状态（含 404）就认为网络层已就绪。
    """
    deadline = time.time() + timeout_sec
    last_error = None

    while time.time() < deadline:
        # 先检查被测进程是否已退出：一旦退出，继续探活只会掩盖真实根因。
        if proc.poll() is not None:
            raise RuntimeError("服务进程在就绪前已退出")
        try:
            resp = requests.get(f"{base_url}/__smoke_ready__", timeout=0.8)
            # 这里额外检查返回体包含探活路径，避免误连到机器上其他同端口服务造成假阳性。
            if resp.status_code and "/__smoke_ready__" in resp.text:
                return
        except requests.RequestException as exc:
            last_error = exc
        time.sleep(0.2)

    raise RuntimeError(f"服务未在 {timeout_sec}s 内就绪，最后错误: {last_error}")


def wait_webhook_mock_ready(timeout_sec: float) -> None:
    """
    advanced 模式下，确认 webhook mock 已可访问（弱校验）。

    为什么做这个检查：
    - advanced 依赖 --auto-start-deps 拉起子进程，先确认端口可用能更快定位环境问题。
    - 当前 webhook 无独立持久化表，先把“通道可达”作为最小保障。
    """
    deadline = time.time() + timeout_sec
    last_error = None
    url = "http://127.0.0.1:9999/webhook"

    while time.time() < deadline:
        try:
            resp = requests.post(url, json={"probe": "ready-check"}, timeout=0.8)
            if 200 <= resp.status_code < 300:
                return
        except requests.RequestException as exc:
            last_error = exc
        time.sleep(0.2)

    raise RuntimeError(f"webhook mock 未在 {timeout_sec}s 内就绪，最后错误: {last_error}")


def build_span_payloads(mode: str,
                        trace_key: int,
                        root_span_id: int,
                        child_span_id: int) -> Tuple[dict, dict, str]:
    """
    构造 root/child 两条 span 以及期望风险等级。

    设计原因：
    - basic 要求稳定验证聚合落库，不强依赖 AI 语义判断。
    - advanced 需要可控地触发 critical，便于稳定断言 trace_analysis.risk_level。
    """
    if mode == "advanced":
        root_name = "critical-root-span"
        child_name = "critical-child-span"
        expected_risk = "critical"
    else:
        root_name = "root-span"
        child_name = "child-span"
        expected_risk = "unknown"

    root = {
        "trace_key": trace_key,
        "span_id": root_span_id,
        "start_time_ms": 1700000000000,
        "end_time_ms": 1700000000100,
        "name": root_name,
        "service_name": "smoke-service",
        "status": "OK",
    }
    child = {
        "trace_key": trace_key,
        "span_id": child_span_id,
        "parent_span_id": root_span_id,
        "start_time_ms": 1700000000110,
        "end_time_ms": 1700000000200,
        "name": child_name,
        "service_name": "smoke-service",
        "status": "ERROR",
        # trace_end=true 是当前触发分发的明确条件，保证冒烟可重复且稳定。
        "trace_end": True,
    }
    return root, child, expected_risk


def post_span(base_url: str, payload: dict) -> dict:
    """
    发送单条 span 并验证接口返回 202。
    """
    resp = requests.post(f"{base_url}/logs/spans", json=payload, timeout=2.0)
    if resp.status_code != 202:
        raise RuntimeError(f"POST /logs/spans 失败: status={resp.status_code}, body={resp.text}")
    try:
        return resp.json()
    except ValueError:
        raise RuntimeError(f"响应不是合法 JSON: {resp.text}")


def query_db_counts(db_path: Path, trace_id: str) -> Tuple[int, int]:
    """
    查询 trace_summary 与 trace_span 的条数。
    """
    conn = sqlite3.connect(str(db_path))
    try:
        cur = conn.cursor()
        cur.execute("SELECT COUNT(*) FROM trace_summary WHERE trace_id = ?", (trace_id,))
        summary_count = int(cur.fetchone()[0])
        cur.execute("SELECT COUNT(*) FROM trace_span WHERE trace_id = ?", (trace_id,))
        span_count = int(cur.fetchone()[0])
        return summary_count, span_count
    finally:
        conn.close()


def query_parent_id(db_path: Path, span_id: str) -> Optional[str]:
    """
    查询某个 span 的 parent_id。
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
    轮询等待基础落库完成（summary>=1 且 span>=2）。
    """
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        if db_path.exists():
            try:
                summary_count, span_count = query_db_counts(db_path, trace_id)
                if summary_count >= 1 and span_count >= 2:
                    return
            except sqlite3.Error:
                pass
        time.sleep(0.25)
    raise RuntimeError(f"数据库在 {timeout_sec}s 内未达到基础落库条件")


def query_analysis_row(db_path: Path, trace_id: str) -> Optional[Tuple[str, str]]:
    """
    查询 trace_analysis 最新一条记录的 risk_level 与 summary。
    """
    conn = sqlite3.connect(str(db_path))
    try:
        cur = conn.cursor()
        cur.execute(
            """
            SELECT risk_level, summary
            FROM trace_analysis
            WHERE trace_id = ?
            ORDER BY rowid DESC
            LIMIT 1
            """,
            (trace_id,),
        )
        row = cur.fetchone()
        if not row:
            return None
        return str(row[0]), str(row[1])
    finally:
        conn.close()


def query_trace_analysis_count(db_path: Path, trace_id: str) -> int:
    """
    查询指定 trace_id 在 trace_analysis 中的记录条数。
    """
    conn = sqlite3.connect(str(db_path))
    try:
        cur = conn.cursor()
        cur.execute("SELECT COUNT(*) FROM trace_analysis WHERE trace_id = ?", (trace_id,))
        return int(cur.fetchone()[0])
    finally:
        conn.close()


def read_available_process_logs(proc: Optional[subprocess.Popen], max_bytes: int = 12000) -> str:
    """
    非阻塞读取当前可获得的服务日志片段。

    为什么这么做：
    - 冒烟失败时服务进程可能还在运行，直接 read() 可能阻塞卡死脚本。
    - 使用 select + 非阻塞读取，只抓“当前已输出”的日志，既安全又能提升排障信息量。
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


def print_db_snapshot(db_path: Path, trace_id: str) -> None:
    """
    打印失败时的数据库快照，帮助判断是“没分发”、“没落库”还是“只落了部分表”。
    """
    if not db_path.exists():
        print(f"[smoke:debug] 数据库文件不存在: {db_path}")
        return

    try:
        summary_count, span_count = query_db_counts(db_path, trace_id)
        analysis_count = query_trace_analysis_count(db_path, trace_id)
        print("[smoke:debug] 数据库计数快照:")
        print(f"  - trace_summary(trace_id={trace_id}): {summary_count}")
        print(f"  - trace_span(trace_id={trace_id}): {span_count}")
        print(f"  - trace_analysis(trace_id={trace_id}): {analysis_count}")

        conn = sqlite3.connect(str(db_path))
        try:
            cur = conn.cursor()
            cur.execute(
                """
                SELECT trace_id, service_name, start_time_ms, duration_ms, span_count
                FROM trace_summary
                ORDER BY rowid DESC
                LIMIT 3
                """
            )
            summary_rows = cur.fetchall()
            print(f"[smoke:debug] 最近 trace_summary 记录数: {len(summary_rows)}")
            for row in summary_rows:
                print(f"  - {row}")

            cur.execute(
                """
                SELECT trace_id, span_id, parent_id, operation, status
                FROM trace_span
                ORDER BY rowid DESC
                LIMIT 5
                """
            )
            span_rows = cur.fetchall()
            print(f"[smoke:debug] 最近 trace_span 记录数: {len(span_rows)}")
            for row in span_rows:
                print(f"  - {row}")

            cur.execute(
                """
                SELECT trace_id, risk_level, summary
                FROM trace_analysis
                ORDER BY rowid DESC
                LIMIT 5
                """
            )
            analysis_rows = cur.fetchall()
            print(f"[smoke:debug] 最近 trace_analysis 记录数: {len(analysis_rows)}")
            for row in analysis_rows:
                print(f"  - {row}")
        finally:
            conn.close()
    except Exception as exc:
        print(f"[smoke:debug] 数据库快照读取失败: {exc}")


def probe_proxy_and_webhook() -> None:
    """
    在 advanced 失败时，额外探测 proxy 与 webhook 可用性。
    """
    try:
        resp = requests.get("http://127.0.0.1:8001/", timeout=1.0)
        print(f"[smoke:debug] proxy / 状态: {resp.status_code}, body: {resp.text[:200]}")
    except requests.RequestException as exc:
        print(f"[smoke:debug] proxy / 探测失败: {exc}")

    try:
        sample_payload = "{\"spans\":[{\"name\":\"critical-smoke-probe\"}]}"
        resp = requests.post(
            "http://127.0.0.1:8001/analyze/trace/mock",
            data=sample_payload,
            headers={"Content-Type": "text/plain"},
            timeout=2.0,
        )
        print(f"[smoke:debug] proxy analyze/trace/mock 状态: {resp.status_code}, body: {resp.text[:300]}")
    except requests.RequestException as exc:
        print(f"[smoke:debug] proxy analyze/trace/mock 探测失败: {exc}")

    try:
        resp = requests.post("http://127.0.0.1:9999/webhook", json={"probe": "debug-check"}, timeout=1.0)
        print(f"[smoke:debug] webhook /webhook 状态: {resp.status_code}, body: {resp.text[:200]}")
    except requests.RequestException as exc:
        print(f"[smoke:debug] webhook /webhook 探测失败: {exc}")


def wait_analysis_ready(db_path: Path, trace_id: str, timeout_sec: float) -> Tuple[str, str]:
    """
    advanced 模式轮询等待 AI 分析落库。

    返回：
    - (risk_level, summary)
    """
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        if db_path.exists():
            try:
                row = query_analysis_row(db_path, trace_id)
                if row is not None and row[0]:
                    return row
            except sqlite3.Error:
                pass
        time.sleep(0.25)
    raise RuntimeError(f"trace_analysis 在 {timeout_sec}s 内未落库")


def cleanup(proc: Optional[subprocess.Popen], db_path: Path, keep_artifacts: bool) -> None:
    """
    回收进程与临时文件。
    """
    if proc is not None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)

    if keep_artifacts:
        print(f"[smoke] 保留临时文件用于排障: {db_path}")
        return

    for suffix in ["", "-wal", "-shm"]:
        p = Path(str(db_path) + suffix)
        if p.exists():
            p.unlink()


def run_flow(args: argparse.Namespace) -> int:
    """
    根据 mode 执行 basic/advanced 冒烟主流程。
    """
    mode = args.mode
    enable_dev_deps = mode == "advanced"

    server_bin = Path(args.server_bin).resolve()
    db_path = Path(args.db).resolve()
    base_url = f"http://127.0.0.1:{args.port}"

    trace_key = int(time.time() * 1000) % 1_000_000_000
    root_span_id = trace_key + 1
    child_span_id = trace_key + 2

    span_root, span_child, expected_risk = build_span_payloads(mode, trace_key, root_span_id, child_span_id)

    proc = None
    try:
        proc = start_server(server_bin, db_path, args.port, enable_dev_deps)
        wait_server_ready(base_url, args.ready_timeout, proc)
        print(f"[smoke:{mode}] 服务已就绪")

        if mode == "advanced":
            wait_webhook_mock_ready(args.ready_timeout)
            print("[smoke:advanced] webhook mock 已就绪")

        resp1 = post_span(base_url, span_root)
        resp2 = post_span(base_url, span_child)
        print(f"[smoke:{mode}] span1 accepted: {resp1}")
        print(f"[smoke:{mode}] span2 accepted: {resp2}")

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

        if mode == "advanced":
            risk_level, summary = wait_analysis_ready(db_path, trace_id, args.analysis_timeout)
            print(f"[smoke:advanced] analysis risk={risk_level}, summary={summary}")
            if risk_level.lower() != expected_risk:
                raise RuntimeError(
                    f"analysis 风险等级异常: {risk_level}（期望 {expected_risk}）"
                )

        print(f"[smoke:{mode}] ✅ 通过")
        return 0
    except Exception as exc:
        print(f"[smoke:{mode}] ❌ 失败: {exc}")
        # 失败时主动输出更多上下文，避免“只知道失败，不知道为什么失败”的排障盲区。
        process_state = "running" if (proc and proc.poll() is None) else "exited"
        print(f"[smoke:debug] 服务进程状态: {process_state}")
        logs = read_available_process_logs(proc)
        if logs:
            print("[smoke:debug] 服务日志片段:\n" + logs)
        else:
            print("[smoke:debug] 当前未读到新的服务日志片段")

        print_db_snapshot(db_path, str(trace_key))
        if mode == "advanced":
            probe_proxy_and_webhook()
        return 1
    finally:
        cleanup(proc, db_path, args.keep_artifacts)


def main() -> int:
    """
    入口函数：
    - 读取 mode
    - 分发到统一流程函数 run_flow
    """
    args = parse_args()
    return run_flow(args)


if __name__ == "__main__":
    sys.exit(main())
