#!/usr/bin/env bash
set -euo pipefail

# 用法：
#   bash server/tests/manual_service_monitor_demo.sh
#   DEMO_DETACH=1 bash server/tests/manual_service_monitor_demo.sh
#   RUN_DIR=/tmp/demo-run BACKEND_PORT=18080 PROXY_PORT=8001 bash server/tests/manual_service_monitor_demo.sh
#
# 这个脚本现在只负责“起服务”和“管生命周期”，不再发送任何 trace：
# 1) 编译 LogSentinel
# 2) 拉起独立 Python proxy
# 3) 拉起后端，并把服务监控窗口分钟数和桶粒度一起透传进去
# 4) 直接运行时默认驻留，方便你打开前端观察
# 5) 如果 DEMO_DETACH=1，就把运行信息写到 run_info.env 后退出，由外层包装脚本继续往下跑

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SERVER_DIR="${ROOT_DIR}/server"
SERVER_BUILD_DIR="${SERVER_DIR}/build"
SERVER_BIN="${SERVER_BUILD_DIR}/LogSentinel"
PROXY_SCRIPT="${ROOT_DIR}/server/ai/proxy/main.py"

if [[ -d "${ROOT_DIR}/../venv/bin" ]]; then
    export PATH="${ROOT_DIR}/../venv/bin:$PATH"
fi

BACKEND_PORT="${BACKEND_PORT:-8080}"
PROXY_PORT="${PROXY_PORT:-8001}"
BASE_URL="${BASE_URL:-http://127.0.0.1:${BACKEND_PORT}}"
TRACE_AI_BASE_URL="${TRACE_AI_BASE_URL:-http://127.0.0.1:${PROXY_PORT}}"
FRONTEND_URL="${FRONTEND_URL:-http://127.0.0.1:5173/service-prototype}"
SERVICE_MONITOR_WINDOW_MINUTES="${SERVICE_MONITOR_WINDOW_MINUTES:-1}"
# 联调默认把桶粒度压到 3 秒，这样服务监控不需要再傻等整整 1 分钟才首次出榜。
# 这里仍然和窗口分钟数分开配置，避免把“最近 30 分钟”这种产品语义一起改掉。
SERVICE_MONITOR_BUCKET_SECONDS="${SERVICE_MONITOR_BUCKET_SECONDS:-3}"
WAIT_RETRY="${WAIT_RETRY:-60}"
WAIT_SLEEP_SEC="${WAIT_SLEEP_SEC:-0.5}"
STOP_RETRY="${STOP_RETRY:-40}"
STOP_SLEEP_SEC="${STOP_SLEEP_SEC:-0.25}"
DEMO_DETACH="${DEMO_DETACH:-0}"

RUN_DIR="${RUN_DIR:-$(mktemp -d /tmp/logsentinel-service-monitor-demo.XXXXXX)}"
DB_PATH="${DB_PATH:-${RUN_DIR}/service-monitor-demo.db}"
BACKEND_LOG="${RUN_DIR}/backend.log"
PROXY_LOG="${RUN_DIR}/proxy.log"
RUN_INFO_FILE="${RUN_DIR}/run_info.env"

BACKEND_PID=""
PROXY_PID=""
KEEP_CHILDREN_ON_EXIT=0

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
    # proxy 依赖前置检查要在起进程前做完，否则 ready 超时后再回头翻日志很绕。
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

    # 这里还是先发 TERM，让后端和 proxy 有机会自己把日志、SQLite 和线程收尾做完。
    log "stopping ${name} pid=${pid}"
    kill -TERM "${pid}" >/dev/null 2>&1 || true
    if ! wait_for_pid_exit "${pid}"; then
        log "${name} pid=${pid} still alive after grace period, sending SIGKILL"
        kill -KILL "${pid}" >/dev/null 2>&1 || true
        wait "${pid}" >/dev/null 2>&1 || true
    fi
}

cleanup() {
    # detach 模式下，服务所有权已经移交给外层包装脚本；
    # 所以这里不能在 EXIT 时再把子进程杀掉，否则外层刚起好服务，脚本一退出就全没了。
    set +e
    if [[ "${KEEP_CHILDREN_ON_EXIT}" == "1" ]]; then
        return 0
    fi
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

write_run_info() {
    mkdir -p "${RUN_DIR}"
    cat >"${RUN_INFO_FILE}" <<EOF
RUN_DIR='${RUN_DIR}'
DB_PATH='${DB_PATH}'
BACKEND_LOG='${BACKEND_LOG}'
PROXY_LOG='${PROXY_LOG}'
BACKEND_PORT='${BACKEND_PORT}'
PROXY_PORT='${PROXY_PORT}'
BASE_URL='${BASE_URL}'
TRACE_AI_BASE_URL='${TRACE_AI_BASE_URL}'
FRONTEND_URL='${FRONTEND_URL}'
SERVICE_MONITOR_WINDOW_MINUTES='${SERVICE_MONITOR_WINDOW_MINUTES}'
SERVICE_MONITOR_BUCKET_SECONDS='${SERVICE_MONITOR_BUCKET_SECONDS}'
BACKEND_PID='${BACKEND_PID}'
PROXY_PID='${PROXY_PID}'
EOF
}

hold_until_interrupt() {
    echo
    log "服务已就绪，前端如果已经通过 npm run dev 跑起来，请打开 ${FRONTEND_URL}"
    log "当前只负责起服务，不自动发送 trace；你可以单独运行 post_random_trace_once.py 灌一条随机链路"
    log "本次运行目录: ${RUN_DIR}"
    log "后端日志: ${BACKEND_LOG}"
    log "代理日志: ${PROXY_LOG}"

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
ensure_ai_proxy_python_env

ensure_port_available "${BACKEND_PORT}" "LogSentinel"
ensure_port_available "${PROXY_PORT}" "AI proxy"

# 这次起服务脚本仍然默认走独立数据库，避免上一次 demo 残留把这次窗口统计污染掉。
rm -f "${DB_PATH}"
mkdir -p "${RUN_DIR}"

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
"${SERVER_BIN}" \
    --db "${DB_PATH}" \
    --port "${BACKEND_PORT}" \
    --no-auto-start-proxy \
    --trace-ai-provider mock \
    --trace-ai-base-url "${TRACE_AI_BASE_URL}" \
    --service-monitor-window-minutes "${SERVICE_MONITOR_WINDOW_MINUTES}" \
    --service-monitor-bucket-seconds "${SERVICE_MONITOR_BUCKET_SECONDS}" \
    >"${BACKEND_LOG}" 2>&1 &
BACKEND_PID=$!
wait_for_http_ready "${BASE_URL}/service-monitor/runtime" "LogSentinel" "${BACKEND_PID}" "${BACKEND_LOG}"

write_run_info
log "step4: service stack ready, run info written to ${RUN_INFO_FILE}"

if [[ "${DEMO_DETACH}" == "1" ]]; then
    # detach 模式下由外层脚本接管生命周期，所以这里直接放行退出。
    KEEP_CHILDREN_ON_EXIT=1
    log "detach mode enabled; leaving services running for wrapper script"
    exit 0
fi

hold_until_interrupt
