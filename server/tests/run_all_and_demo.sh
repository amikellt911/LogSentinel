#!/usr/bin/env bash
set -euo pipefail

# 这个脚本负责把“服务监控原型页联调”一次性串起来：
# 1) 先编译 C++ 后端，避免你手工切目录跑 cmake。
# 2) 再单独拉起 Python proxy，后端显式关闭 auto-start，防止重复占用 8001。
# 3) 等两个服务都 ready 之后，复用现成的 manual_service_monitor_demo.sh 灌数并拉 JSON。
# 4) demo 跑完后不立刻退出，继续把服务挂着，方便你去浏览器里看前端页面。
# 5) 无论正常退出还是 Ctrl+C，统一走 trap 清理，只杀这次脚本自己拉起来的 PID。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SERVER_DIR="${ROOT_DIR}/server"
SERVER_BUILD_DIR="${SERVER_DIR}/build"
SERVER_BIN="${SERVER_BUILD_DIR}/LogSentinel"
PROXY_SCRIPT="${ROOT_DIR}/server/ai/proxy/main.py"
DEMO_SCRIPT="${ROOT_DIR}/server/tests/manual_service_monitor_demo.sh"

if [[ -d "${ROOT_DIR}/../venv/bin" ]]; then
    export PATH="${ROOT_DIR}/../venv/bin:$PATH"
fi

BACKEND_PORT="${BACKEND_PORT:-8080}"
PROXY_PORT="${PROXY_PORT:-8001}"
BASE_URL="${BASE_URL:-http://127.0.0.1:${BACKEND_PORT}}"
TRACE_AI_BASE_URL="${TRACE_AI_BASE_URL:-http://127.0.0.1:${PROXY_PORT}}"
FRONTEND_URL="${FRONTEND_URL:-http://127.0.0.1:5173/service-prototype}"
# 联调时默认把服务监控窗口压到 1 分钟，这样跑一轮 demo 就能很快看到进窗/退窗。
# 如果你想恢复产品默认语义，可以在命令前覆盖同名环境变量。
SERVICE_MONITOR_WINDOW_MINUTES="${SERVICE_MONITOR_WINDOW_MINUTES:-1}"
WAIT_RETRY="${WAIT_RETRY:-60}"
WAIT_SLEEP_SEC="${WAIT_SLEEP_SEC:-0.5}"
STOP_RETRY="${STOP_RETRY:-40}"
STOP_SLEEP_SEC="${STOP_SLEEP_SEC:-0.25}"

# 每次联调都单独建一个 run dir，既然这次要同时管理日志、数据库和 PID 生命周期，
# 那么把现场痕迹收口到一个目录里最直观，出问题时也方便你直接 tail 日志。
RUN_DIR="$(mktemp -d /tmp/logsentinel-run-all-demo.XXXXXX)"
DB_PATH="${DB_PATH:-${RUN_DIR}/service-monitor-demo.db}"
BACKEND_LOG="${RUN_DIR}/backend.log"
PROXY_LOG="${RUN_DIR}/proxy.log"

BACKEND_PID=""
PROXY_PID=""

log() {
    local now
    now="$(date '+%F %T')"
    echo "[$now] $*"
}

require_file() {
    local path="$1"
    if [[ ! -f "${path}" ]]; then
        echo "fatal: missing file: ${path}" >&2
        exit 1
    fi
}

require_command() {
    local cmd="$1"
    if ! command -v "${cmd}" >/dev/null 2>&1; then
        echo "fatal: missing command: ${cmd}" >&2
        exit 1
    fi
}

ensure_ai_proxy_python_env() {
    # 这里把 proxy 依赖前置检查，是为了在“起进程之前”就把环境问题说清楚。
    # 否则用户只会看到 proxy ready 超时，然后还得再去翻日志，定位路径太绕。
    if ! python3 - <<'PY' >/dev/null 2>&1
import dotenv
import fastapi
import uvicorn
PY
    then
        echo "fatal: 当前 python3 缺少 AI proxy 依赖，请先执行: cd server/ai && pip install -r requirements.txt" >&2
        exit 1
    fi
}

port_listener_pids() {
    local port="$1"
    lsof -tiTCP:"${port}" -sTCP:LISTEN 2>/dev/null || true
}

ensure_port_available() {
    local port="$1"
    local service_name="$2"
    local pids

    pids="$(port_listener_pids "${port}")"
    if [[ -n "${pids}" ]]; then
        echo "warning: ${service_name} 端口 ${port} 已被占用，正在自动清理残留进程 (PIDs: ${pids})..." >&2
        kill -9 ${pids} 2>/dev/null || true
        sleep 1
    fi
}

wait_for_pid_exit() {
    local pid="$1"
    local retry=0

    while (( retry < STOP_RETRY )); do
        if ! kill -0 "${pid}" >/dev/null 2>&1; then
            wait "${pid}" >/dev/null 2>&1 || true
            return 0
        fi
        sleep "${STOP_SLEEP_SEC}"
        retry=$((retry + 1))
    done

    return 1
}

stop_process() {
    local pid="$1"
    local name="$2"

    if [[ -z "${pid}" ]] || ! kill -0 "${pid}" >/dev/null 2>&1; then
        return 0
    fi

    # 这里先发 TERM，是因为后端和 proxy 都可能还在刷日志、刷 SQLite 或清线程。
    # 先给它们一个自己收尾的机会，比上来直接 KILL 更不容易把现场打烂。
    log "stopping ${name} pid=${pid}"
    kill -TERM "${pid}" >/dev/null 2>&1 || true
    if ! wait_for_pid_exit "${pid}"; then
        log "${name} pid=${pid} still alive after grace period, sending SIGKILL"
        kill -KILL "${pid}" >/dev/null 2>&1 || true
        wait "${pid}" >/dev/null 2>&1 || true
    fi
}

cleanup() {
    # trap 里的清理要尽量“只收尾、不抛错”。
    # 既然这里跑在 EXIT/INT/TERM 路径上，那么清理阶段再因为 set -e 中断，
    # 反而会把真正的退出原因冲掉，所以先关掉严格退出。
    set +e
    stop_process "${BACKEND_PID}" "LogSentinel"
    stop_process "${PROXY_PID}" "AI proxy"
}

trap cleanup EXIT INT TERM

wait_for_http_ready() {
    local url="$1"
    local service_name="$2"
    local pid="$3"
    local log_path="$4"
    local retry=0

    # 这里不用“只看端口”，而是直接打 HTTP。
    # 既然我们最终就是要靠 HTTP 接口联调，那么 readiness 也应该验证到同一层，
    # 否则端口虽然 listen 了，路由还没 ready，后面的 demo 还是会撞墙。
    while (( retry < WAIT_RETRY )); do
        if curl -fsS --max-time 1 "${url}" >/dev/null 2>&1; then
            return 0
        fi

        if ! kill -0 "${pid}" >/dev/null 2>&1; then
            echo "fatal: ${service_name} 进程在 ready 前就退出了，日志见 ${log_path}" >&2
            tail -n 40 "${log_path}" >&2 || true
            exit 1
        fi

        sleep "${WAIT_SLEEP_SEC}"
        retry=$((retry + 1))
    done

    echo "fatal: ${service_name} 在超时内没有 ready，日志见 ${log_path}" >&2
    tail -n 40 "${log_path}" >&2 || true
    exit 1
}

hold_until_interrupt() {
    echo
    log "demo 已完成，前端如果已经通过 npm run dev 跑起来，请打开 ${FRONTEND_URL}"
    log "服务会保持运行，方便你继续点页面；按 Ctrl+C 结束本轮联调并自动清理后台进程"
    log "本次联调日志目录: ${RUN_DIR}"

    # demo 跑完后继续驻留，是因为页面还要实时请求后端。
    # 如果脚本一跑完就退出，EXIT trap 会立刻把服务清掉，浏览器那边只会看到 502。
    while true; do
        if [[ -n "${BACKEND_PID}" ]] && ! kill -0 "${BACKEND_PID}" >/dev/null 2>&1; then
            echo "fatal: LogSentinel 运行中提前退出，日志见 ${BACKEND_LOG}" >&2
            tail -n 40 "${BACKEND_LOG}" >&2 || true
            exit 1
        fi
        if [[ -n "${PROXY_PID}" ]] && ! kill -0 "${PROXY_PID}" >/dev/null 2>&1; then
            echo "fatal: AI proxy 运行中提前退出，日志见 ${PROXY_LOG}" >&2
            tail -n 40 "${PROXY_LOG}" >&2 || true
            exit 1
        fi
        sleep 1
    done
}

require_command cmake
require_command curl
require_command lsof
require_command python3
require_file "${PROXY_SCRIPT}"
require_file "${DEMO_SCRIPT}"
ensure_ai_proxy_python_env

ensure_port_available "${BACKEND_PORT}" "LogSentinel"
ensure_port_available "${PROXY_PORT}" "AI proxy"

# 每次都从新的临时数据库起步，避免上次 demo 残留数据把这次页面口径污染掉。
rm -f "${DB_PATH}"

log "step1: build LogSentinel"
cmake -B "${SERVER_BUILD_DIR}" -S "${SERVER_DIR}"
cmake --build "${SERVER_BUILD_DIR}" --target LogSentinel

if [[ ! -x "${SERVER_BIN}" ]]; then
    echo "fatal: backend binary not found: ${SERVER_BIN}" >&2
    exit 1
fi

log "step2: start AI proxy on ${TRACE_AI_BASE_URL}"
PYTHONUNBUFFERED=1 python3 "${PROXY_SCRIPT}" >"${PROXY_LOG}" 2>&1 &
PROXY_PID=$!
wait_for_http_ready "${TRACE_AI_BASE_URL}/" "AI proxy" "${PROXY_PID}" "${PROXY_LOG}"

log "step3: start LogSentinel on ${BASE_URL}"
# 这里把小窗口参数直接收进联合脚本，是为了让你一条命令就能跑完联调。
# 否则每次想验证退窗，都还得再手敲一遍后端启动参数，重复且容易忘。
"${SERVER_BIN}" \
    --db "${DB_PATH}" \
    --port "${BACKEND_PORT}" \
    --no-auto-start-proxy \
    --trace-ai-provider mock \
    --trace-ai-base-url "${TRACE_AI_BASE_URL}" \
    --service-monitor-window-minutes "${SERVICE_MONITOR_WINDOW_MINUTES}" \
    >"${BACKEND_LOG}" 2>&1 &
BACKEND_PID=$!
wait_for_http_ready "${BASE_URL}/service-monitor/runtime" "LogSentinel" "${BACKEND_PID}" "${BACKEND_LOG}"

log "step4: run manual service monitor demo"
bash "${DEMO_SCRIPT}" "${BASE_URL}"

hold_until_interrupt
