#!/usr/bin/env bash
set -euo pipefail

# 这个脚本只服务“验收现场演示”，不替代正式 Suite D benchmark wrapper。
# 现场演示的目标是：先把前端地址亮出来，再跑一轮 mock AI 性能压测，最后保留后端进程方便老师继续点页面。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_BIN="${SERVER_BIN:-${ROOT_DIR}/server/build/LogSentinel}"
WRK_SCRIPT="${WRK_SCRIPT:-${ROOT_DIR}/server/tests/benchmark/suite_d/trace_model_suite_d.lua}"

RUN_ROOT="${DEMO_RUN_ROOT:-/tmp/logsentinel-suite-d-demo}"
RUN_DIR="${RUN_DIR:-${RUN_ROOT}-$(date +%Y%m%d-%H%M%S)}"
DB_PATH="${DB_PATH:-${RUN_DIR}/suite_d_demo.db}"
BACKEND_LOG="${BACKEND_LOG:-${RUN_DIR}/backend.log}"
WRK_LOG="${WRK_LOG:-${RUN_DIR}/wrk.log}"
RESULT_JSON="${RESULT_JSON:-${RUN_DIR}/result.json}"

BACKEND_PORT_START="${BACKEND_PORT:-18080}"
PORT_SEARCH_LIMIT="${PORT_SEARCH_LIMIT:-80}"
SERVER_CPUSET="${SERVER_CPUSET:-1-3}"
WRK_CPUSET="${WRK_CPUSET:-0}"
TRACE_AI_PROVIDER="${TRACE_AI_PROVIDER:-mock}"
PROXY_SCRIPT="${PROXY_SCRIPT:-${ROOT_DIR}/server/ai/proxy/main.py}"
PROXY_PORT_START="${PROXY_PORT:-19001}"
PROXY_PORT_SEARCH_LIMIT="${PROXY_PORT_SEARCH_LIMIT:-80}"
PROXY_MAX_WORKERS="${PROXY_MAX_WORKERS:-192}"
PROXY_LOG="${PROXY_LOG:-${RUN_DIR}/proxy.log}"

# 默认参数采用 worker/proxy 搜索中更适合现场展示 AI 分析吞吐的 192 组：
# trace_summary 主链可见吞吐仍保持稳定，同时 trace_analysis 完成量明显高于 12/32/64 这类保守配置。
# 这里同步手动拉起 proxy --max-workers=192，不能只改 C++ worker，否则会被默认 128 proxy worker 截断。
SERVER_IO_THREADS="${SERVER_IO_THREADS:-1}"
WORKER_THREADS="${WORKER_THREADS:-192}"
DISPATCH_WORKER_THREADS="${DISPATCH_WORKER_THREADS:-2}"
WORKER_QUEUE_SIZE="${WORKER_QUEUE_SIZE:-8192}"
TRACE_ACTIVE_SESSION_LIMIT="${TRACE_ACTIVE_SESSION_LIMIT:-2048}"
TRACE_BUFFERED_SPAN_LIMIT="${TRACE_BUFFERED_SPAN_LIMIT:-16384}"
TRACE_MAX_DISPATCH_PER_TICK="${TRACE_MAX_DISPATCH_PER_TICK:-128}"
TRACE_SEALED_GRACE_WINDOW_MS="${TRACE_SEALED_GRACE_WINDOW_MS:-100}"
TRACE_SWEEP_INTERVAL_MS="${TRACE_SWEEP_INTERVAL_MS:-200}"
TRACE_PRIMARY_FLUSH_SPAN_THRESHOLD="${TRACE_PRIMARY_FLUSH_SPAN_THRESHOLD:-512}"
TRACE_PRIMARY_FLUSH_INTERVAL_MS="${TRACE_PRIMARY_FLUSH_INTERVAL_MS:-5}"

WRK_THREADS="${WRK_THREADS:-1}"
CONNECTIONS="${CONNECTIONS:-30}"
WARMUP_DURATION="${WARMUP_DURATION:-2s}"
DURATION="${DURATION:-10s}"
SPANS_PER_TRACE="${SPANS_PER_TRACE:-8}"

# 前端和 wrk 共用同一个 LogSentinel 端口。
# 如果 ready 后立刻压测，wrk 会抢占连接和后端工作线程，浏览器加载静态资源/API 时就容易卡住。
# 默认先留 20 秒给现场打开页面；想跳过可设置 DEMO_OBSERVE_BEFORE_WRK_SEC=0。
# 这个等待窗口只影响演示体验，不进入 benchmark 计时口径。
DEMO_OBSERVE_BEFORE_WRK_SEC="${DEMO_OBSERVE_BEFORE_WRK_SEC:-20}"
WAIT_RETRY="${WAIT_RETRY:-80}"
WAIT_SLEEP_SEC="${WAIT_SLEEP_SEC:-0.25}"
STOP_RETRY="${STOP_RETRY:-40}"
STOP_SLEEP_SEC="${STOP_SLEEP_SEC:-0.25}"

BACKEND_PID=""
PROXY_PID=""
BACKEND_PORT=""
PROXY_PORT=""
BASE_URL=""
TRACE_AI_BASE_URL=""

log() {
    local now
    now="$(date '+%F %T')"
    echo "[${now}] $*"
}

require_command() {
    local cmd="$1"
    if ! command -v "${cmd}" >/dev/null 2>&1; then
        echo "fatal: missing command: ${cmd}" >&2
        exit 1
    fi
}

require_file() {
    local path="$1"
    if [[ ! -f "${path}" ]]; then
        echo "fatal: missing file: ${path}" >&2
        exit 1
    fi
}

find_free_port() {
    local start_port="$1"
    local search_limit="$2"

    # 端口选择放在脚本里做，而不是让 LogSentinel 启动失败后再人工排查。
    # 这样验收现场即使 18080 被旧进程占用，也会自动顺延到下一段可用端口并打印出来。
    python3 - "${start_port}" "${search_limit}" <<'PY'
import socket
import sys

start = int(sys.argv[1])
limit = int(sys.argv[2])

for port in range(start, start + limit):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.2)
        if sock.connect_ex(("127.0.0.1", port)) != 0:
            print(port)
            raise SystemExit(0)

raise SystemExit(f"no free port found from {start} to {start + limit - 1}")
PY
}

taskset_prefix() {
    local cpuset="$1"
    if [[ -n "${cpuset}" ]]; then
        printf 'taskset -c %q ' "${cpuset}"
    fi
}

stop_backend() {
    if [[ -z "${BACKEND_PID}" ]] || ! kill -0 "${BACKEND_PID}" >/dev/null 2>&1; then
        return 0
    fi

    # Ctrl+C 时才清理后端。压测结束不主动杀进程，是为了让前端页面还能继续访问本轮 SQLite 数据。
    log "stopping LogSentinel pid=${BACKEND_PID}"
    kill -TERM "${BACKEND_PID}" >/dev/null 2>&1 || true
    local retry=0
    while (( retry < STOP_RETRY )); do
        if ! kill -0 "${BACKEND_PID}" >/dev/null 2>&1; then
            wait "${BACKEND_PID}" >/dev/null 2>&1 || true
            return 0
        fi
        sleep "${STOP_SLEEP_SEC}"
        retry=$((retry + 1))
    done

    log "LogSentinel pid=${BACKEND_PID} still alive, sending SIGKILL"
    kill -KILL "${BACKEND_PID}" >/dev/null 2>&1 || true
    wait "${BACKEND_PID}" >/dev/null 2>&1 || true
}

stop_proxy() {
    if [[ -z "${PROXY_PID}" ]] || ! kill -0 "${PROXY_PID}" >/dev/null 2>&1; then
        return 0
    fi

    # 手动 proxy 是为了让演示脚本能控制 --max-workers。
    # 它和后端属于同一轮现场演示生命周期，所以 Ctrl+C 时一起清掉，避免旧 proxy 占端口污染下一轮。
    log "stopping AI proxy pid=${PROXY_PID}"
    kill -TERM "${PROXY_PID}" >/dev/null 2>&1 || true
    local retry=0
    while (( retry < STOP_RETRY )); do
        if ! kill -0 "${PROXY_PID}" >/dev/null 2>&1; then
            wait "${PROXY_PID}" >/dev/null 2>&1 || true
            return 0
        fi
        sleep "${STOP_SLEEP_SEC}"
        retry=$((retry + 1))
    done

    log "AI proxy pid=${PROXY_PID} still alive, sending SIGKILL"
    kill -KILL "${PROXY_PID}" >/dev/null 2>&1 || true
    wait "${PROXY_PID}" >/dev/null 2>&1 || true
}

cleanup() {
    set +e
    stop_backend
    stop_proxy
}

trap cleanup EXIT INT TERM

wait_for_backend_ready() {
    local retry=0
    while (( retry < WAIT_RETRY )); do
        if curl -fsS --max-time 1 "${BASE_URL}/api/settings/all" >/dev/null 2>&1; then
            return 0
        fi
        if ! kill -0 "${BACKEND_PID}" >/dev/null 2>&1; then
            echo "fatal: LogSentinel exited before ready, backend log: ${BACKEND_LOG}" >&2
            tail -n 80 "${BACKEND_LOG}" >&2 || true
            exit 1
        fi
        sleep "${WAIT_SLEEP_SEC}"
        retry=$((retry + 1))
    done

    echo "fatal: LogSentinel did not become ready, backend log: ${BACKEND_LOG}" >&2
    tail -n 80 "${BACKEND_LOG}" >&2 || true
    exit 1
}

wait_for_proxy_ready() {
    local retry=0
    while (( retry < WAIT_RETRY )); do
        if curl -fsS --max-time 1 "${TRACE_AI_BASE_URL}/" >/dev/null 2>&1; then
            return 0
        fi
        if ! kill -0 "${PROXY_PID}" >/dev/null 2>&1; then
            echo "fatal: AI proxy exited before ready, proxy log: ${PROXY_LOG}" >&2
            tail -n 80 "${PROXY_LOG}" >&2 || true
            exit 1
        fi
        sleep "${WAIT_SLEEP_SEC}"
        retry=$((retry + 1))
    done

    echo "fatal: AI proxy did not become ready, proxy log: ${PROXY_LOG}" >&2
    tail -n 80 "${PROXY_LOG}" >&2 || true
    exit 1
}

start_proxy() {
    PROXY_PORT="$(find_free_port "${PROXY_PORT_START}" "${PROXY_PORT_SEARCH_LIMIT}")"
    TRACE_AI_BASE_URL="http://127.0.0.1:${PROXY_PORT}"

    mkdir -p "${RUN_DIR}"

    local proxy_cmd=(
        python3
        "${PROXY_SCRIPT}"
        --host 127.0.0.1
        --port "${PROXY_PORT}"
        --max-workers "${PROXY_MAX_WORKERS}"
    )

    # 后端自带 --auto-start-proxy 只能启动默认 proxy 并发。
    # 演示脚本手动启动 proxy，才能保证 worker=192/proxy=192 和搜索脚本的实验口径一致。
    log "starting AI proxy for demo: port=${PROXY_PORT}, max_workers=${PROXY_MAX_WORKERS}"
    "${proxy_cmd[@]}" >"${PROXY_LOG}" 2>&1 &
    PROXY_PID="$!"
    wait_for_proxy_ready
}

start_backend() {
    BACKEND_PORT="$(find_free_port "${BACKEND_PORT_START}" "${PORT_SEARCH_LIMIT}")"
    BASE_URL="http://127.0.0.1:${BACKEND_PORT}"

    mkdir -p "${RUN_DIR}"

    local server_cmd=(
        "${SERVER_BIN}"
        --db "${DB_PATH}"
        --port "${BACKEND_PORT}"
        --no-auto-start-proxy
        --trace-ai-provider "${TRACE_AI_PROVIDER}"
        --trace-ai-base-url "${TRACE_AI_BASE_URL}"
        --server-io-threads "${SERVER_IO_THREADS}"
        --worker-threads "${WORKER_THREADS}"
        --dispatch-worker-threads "${DISPATCH_WORKER_THREADS}"
        --worker-queue-size "${WORKER_QUEUE_SIZE}"
        --trace-active-session-limit "${TRACE_ACTIVE_SESSION_LIMIT}"
        --trace-buffered-span-limit "${TRACE_BUFFERED_SPAN_LIMIT}"
        --trace-max-dispatch-per-tick "${TRACE_MAX_DISPATCH_PER_TICK}"
        --trace-lifecycle-profile protected
        --trace-sealed-grace-window-ms "${TRACE_SEALED_GRACE_WINDOW_MS}"
        --trace-sweep-interval-ms "${TRACE_SWEEP_INTERVAL_MS}"
        --trace-primary-flush-span-threshold "${TRACE_PRIMARY_FLUSH_SPAN_THRESHOLD}"
        --trace-primary-flush-interval-ms "${TRACE_PRIMARY_FLUSH_INTERVAL_MS}"
        --disable-webhook
    )

    log "starting LogSentinel for acceptance demo"
    if [[ -n "${SERVER_CPUSET}" ]]; then
        taskset -c "${SERVER_CPUSET}" "${server_cmd[@]}" >"${BACKEND_LOG}" 2>&1 &
    else
        "${server_cmd[@]}" >"${BACKEND_LOG}" 2>&1 &
    fi
    BACKEND_PID="$!"
    wait_for_backend_ready
}

print_frontend_addresses() {
    echo
    echo "========== LogSentinel Demo Ready =========="
    echo "前端首页:       ${BASE_URL}/"
    echo "Trace 查询:     ${BASE_URL}/traces"
    echo "服务监控:       ${BASE_URL}/service-prototype"
    echo "系统设置:       ${BASE_URL}/settings"
    echo "后端接口:       ${BASE_URL}"
    echo "监听端口:       ${BACKEND_PORT}"
    echo "SQLite DB:      ${DB_PATH}"
    echo "后端日志:       ${BACKEND_LOG}"
    echo "AI Proxy:       ${TRACE_AI_BASE_URL}  (max_workers=${PROXY_MAX_WORKERS})"
    echo "Proxy 日志:     ${PROXY_LOG}"
    echo "wrk 日志:       ${WRK_LOG}"
    echo "结果 JSON:      ${RESULT_JSON}"
    echo "============================================"
    echo
}

wait_before_wrk() {
    local seconds="$1"
    if [[ "${seconds}" == "0" ]]; then
        return 0
    fi

    # 这里只等待，不发请求、不跑 wrk。
    # 目的就是把“打开前端页面”这件事从“高并发压测”里拆出来，避免同源入口互相抢资源。
    log "waiting ${seconds}s before wrk; open the frontend now if you want to demo the UI first"
    sleep "${seconds}"
}

run_wrk_once() {
    local duration="$1"
    local label="$2"
    if [[ "${duration}" == "0" || "${duration}" == "0s" ]]; then
        # wrk 不接受 -d0s。
        # 演示 smoke 或现场快速验证会把 warmup 设为 0，这里直接跳过该阶段，避免为了“无预热”反而让脚本失败。
        log "skipping wrk ${label}: duration=${duration}"
        return 0
    fi

    local wrk_cmd=(
        wrk
        -t"${WRK_THREADS}"
        -c"${CONNECTIONS}"
        -d"${duration}"
        --latency
        -s "${WRK_SCRIPT}"
        "${BASE_URL}"
        --
        end
        "${SPANS_PER_TRACE}"
        4
        2048
    )

    log "running wrk ${label}: duration=${duration}, connections=${CONNECTIONS}, wrk_threads=${WRK_THREADS}"
    if [[ -n "${WRK_CPUSET}" ]]; then
        TRACE_WRK_MODE=end TRACE_WRK_THREADS="${WRK_THREADS}" taskset -c "${WRK_CPUSET}" "${wrk_cmd[@]}"
    else
        TRACE_WRK_MODE=end TRACE_WRK_THREADS="${WRK_THREADS}" "${wrk_cmd[@]}"
    fi
}

write_result_summary() {
    # 这里用 Python 只做结果摘录：读取 wrk 输出和 SQLite 当前计数，生成一个现场可复制的 result.json。
    # 正式论文口径仍然看 Suite D frozen wrapper；这个 JSON 只是验收现场的轻量摘要。
    python3 - "${DB_PATH}" "${WRK_LOG}" "${RESULT_JSON}" "${DURATION}" "${BASE_URL}" "${BACKEND_PORT}" <<'PY'
import json
import re
import sqlite3
import sys
from pathlib import Path

db_path = Path(sys.argv[1])
wrk_log = Path(sys.argv[2])
result_json = Path(sys.argv[3])
duration = sys.argv[4]
base_url = sys.argv[5]
port = int(sys.argv[6])

def parse_duration_seconds(value: str) -> float:
    match = re.fullmatch(r"(?P<number>\d+(?:\.\d+)?)(?P<unit>[smh])", value.strip())
    if not match:
        return 0.0
    number = float(match.group("number"))
    unit = match.group("unit")
    return number if unit == "s" else number * 60.0 if unit == "m" else number * 3600.0

text = wrk_log.read_text(encoding="utf-8", errors="replace") if wrk_log.exists() else ""

# wrk_log 同时包含 warmup 和 measurement。
# 现场摘要必须取最后一段正式测量结果，否则会把预热阶段的 QPS 当成验收指标。
requests_matches = list(re.finditer(r"(?P<requests>\d+)\s+requests in\s+(?P<seconds>\d+(?:\.\d+)?)s", text))
qps_matches = list(re.finditer(r"Requests/sec:\s+(?P<qps>\d+(?:\.\d+)?)", text))
suite_matches = list(re.finditer(
    r"trace_model_suite_d metrics:\s+offered_traces=(?P<offered>\d+)\s+"
    r"spans_per_trace=(?P<spans>\d+)\s+"
    r"latency_p95_ms=(?P<p95>\d+(?:\.\d+)?)\s+"
    r"latency_p99_ms=(?P<p99>\d+(?:\.\d+)?)",
    text,
))
requests_match = requests_matches[-1] if requests_matches else None
qps_match = qps_matches[-1] if qps_matches else None
suite_match = suite_matches[-1] if suite_matches else None

trace_summary = 0
trace_span = 0
trace_analysis = 0
if db_path.exists():
    conn = sqlite3.connect(str(db_path))
    try:
        trace_summary = int(conn.execute("SELECT COUNT(*) FROM trace_summary").fetchone()[0])
        trace_span = int(conn.execute("SELECT COUNT(*) FROM trace_span").fetchone()[0])
        # trace_summary 只说明主链 Trace 已经可查询，trace_analysis 才说明 AI 分析结果已经落库。
        # 现场演示要同时展示这两个数，避免把“存储完成”误说成“AI 已经分析完成”。
        trace_analysis = int(conn.execute("SELECT COUNT(*) FROM trace_analysis").fetchone()[0])
    except sqlite3.Error:
        trace_summary = 0
        trace_span = 0
        trace_analysis = 0
    finally:
        conn.close()

measurement_seconds = parse_duration_seconds(duration)
offered_traces = int(suite_match.group("offered")) if suite_match else 0
result = {
    "base_url": base_url,
    "frontend_urls": {
        "home": f"{base_url}/",
        "traces": f"{base_url}/traces",
        "service_monitor": f"{base_url}/service-prototype",
        "settings": f"{base_url}/settings",
    },
    "port": port,
    "sqlite_db": str(db_path),
    "wrk_metrics": {
        "requests": int(requests_match.group("requests")) if requests_match else 0,
        "duration_seconds": float(requests_match.group("seconds")) if requests_match else measurement_seconds,
        "requests_per_sec": float(qps_match.group("qps")) if qps_match else 0.0,
        "offered_traces": offered_traces,
        "spans_per_trace": int(suite_match.group("spans")) if suite_match else 0,
        "latency_p95_ms": float(suite_match.group("p95")) if suite_match else 0.0,
        "latency_p99_ms": float(suite_match.group("p99")) if suite_match else 0.0,
    },
    "sqlite_counts_after_wrk": {
        "trace_summary": trace_summary,
        "trace_span": trace_span,
        "trace_analysis": trace_analysis,
    },
    "online_completed_traces_per_sec": (trace_summary / measurement_seconds) if measurement_seconds > 0 else 0.0,
    "online_completion_ratio_after_wrk": (trace_summary / offered_traces) if offered_traces > 0 else 0.0,
    "ai_completed_traces_after_wrk": trace_analysis,
    "ai_completed_traces_per_sec_after_wrk": (trace_analysis / measurement_seconds) if measurement_seconds > 0 else 0.0,
    "ai_completion_ratio_after_wrk": (trace_analysis / trace_summary) if trace_summary > 0 else 0.0,
}

result_json.parent.mkdir(parents=True, exist_ok=True)
result_json.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

print()
print("[suite-d-demo] core metrics")
print(f"[suite-d-demo] result_json={result_json}")
print(f"[suite-d-demo] frontend={base_url}/")
print(f"[suite-d-demo] qps={result['wrk_metrics']['requests_per_sec']:.2f}")
print(f"[suite-d-demo] offered_traces={offered_traces}")
print(f"[suite-d-demo] sqlite_trace_summary_after_wrk={trace_summary}")
print(f"[suite-d-demo] sqlite_trace_analysis_after_wrk={trace_analysis}")
print(f"[suite-d-demo] online_completed_traces_per_sec={result['online_completed_traces_per_sec']:.2f}")
print(f"[suite-d-demo] online_completion_ratio_after_wrk={result['online_completion_ratio_after_wrk']:.4f}")
print(f"[suite-d-demo] ai_completed_traces_per_sec_after_wrk={result['ai_completed_traces_per_sec_after_wrk']:.2f}")
print(f"[suite-d-demo] ai_completion_ratio_after_wrk={result['ai_completion_ratio_after_wrk']:.4f}")
print(f"[suite-d-demo] latency_p95_ms={result['wrk_metrics']['latency_p95_ms']:.2f}")
print(f"[suite-d-demo] latency_p99_ms={result['wrk_metrics']['latency_p99_ms']:.2f}")
PY
}

hold_until_interrupt() {
    echo
    log "mock 性能演示已结束，但 LogSentinel 会继续运行，方便你打开前端给老师看。"
    log "按 Ctrl+C 结束本轮演示并自动清理后端进程。"
    while true; do
        if ! kill -0 "${BACKEND_PID}" >/dev/null 2>&1; then
            echo "fatal: LogSentinel exited unexpectedly, backend log: ${BACKEND_LOG}" >&2
            tail -n 80 "${BACKEND_LOG}" >&2 || true
            exit 1
        fi
        sleep 1
    done
}

require_command python3
require_command curl
require_command wrk
require_file "${SERVER_BIN}"
require_file "${WRK_SCRIPT}"
require_file "${PROXY_SCRIPT}"

if [[ ! -d "${ROOT_DIR}/client/dist" ]]; then
    echo "warning: client/dist 不存在，后端接口仍可压测，但前端静态页面可能无法打开；需要时先执行 cd client && npm run build" >&2
fi

start_proxy
start_backend
print_frontend_addresses
wait_before_wrk "${DEMO_OBSERVE_BEFORE_WRK_SEC}"

{
    echo "===== WRK WARMUP BEGIN ====="
    run_wrk_once "${WARMUP_DURATION}" "warmup"
    echo "===== WRK WARMUP END ====="
    echo
    echo "===== WRK MEASUREMENT BEGIN ====="
    run_wrk_once "${DURATION}" "measurement"
    echo "===== WRK MEASUREMENT END ====="
} | tee "${WRK_LOG}"

write_result_summary
hold_until_interrupt
