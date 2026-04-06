#!/usr/bin/env bash
set -euo pipefail

# 这个脚本现在只做“总入口包装器”：
# 1) 先调用 manual_service_monitor_demo.sh 起服务，但用 detach 模式把生命周期交给当前脚本接管
# 2) 再调用 post_random_trace_once.py 发送 1 条随机 trace
# 3) 等一小会儿让时间窗发布和 AI 样本回填，再拉一次 runtime JSON
# 4) 最后继续驻留，方便你开前端观察；Ctrl+C 时统一清理后台进程

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
START_SCRIPT="${ROOT_DIR}/server/tests/manual_service_monitor_demo.sh"
POST_SCRIPT="${ROOT_DIR}/server/tests/post_random_trace_once.py"

BACKEND_PORT="${BACKEND_PORT:-8080}"
PROXY_PORT="${PROXY_PORT:-8001}"
BASE_URL="${BASE_URL:-http://127.0.0.1:${BACKEND_PORT}}"
TRACE_AI_BASE_URL="${TRACE_AI_BASE_URL:-http://127.0.0.1:${PROXY_PORT}}"
FRONTEND_URL="${FRONTEND_URL:-http://127.0.0.1:5173/service-prototype}"
SERVICE_MONITOR_WINDOW_MINUTES="${SERVICE_MONITOR_WINDOW_MINUTES:-1}"
WAIT_SECONDS="${WAIT_SECONDS:-6}"
STOP_RETRY="${STOP_RETRY:-40}"
STOP_SLEEP_SEC="${STOP_SLEEP_SEC:-0.25}"

RUN_DIR="${RUN_DIR:-$(mktemp -d /tmp/logsentinel-run-all-demo.XXXXXX)}"
RUN_INFO_FILE="${RUN_DIR}/run_info.env"

BACKEND_PID=""
PROXY_PID=""
BACKEND_LOG=""
PROXY_LOG=""

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

    log "stopping ${name} pid=${pid}"
    kill -TERM "${pid}" >/dev/null 2>&1 || true
    if ! wait_for_pid_exit "${pid}"; then
        log "${name} pid=${pid} still alive after grace period, sending SIGKILL"
        kill -KILL "${pid}" >/dev/null 2>&1 || true
        wait "${pid}" >/dev/null 2>&1 || true
    fi
}

cleanup() {
    set +e
    stop_process "${BACKEND_PID}" "LogSentinel"
    stop_process "${PROXY_PID}" "AI proxy"
}

trap cleanup EXIT INT TERM

hold_until_interrupt() {
    echo
    log "demo 已完成，前端如果已经通过 npm run dev 跑起来，请打开 ${FRONTEND_URL}"
    log "服务会保持运行，方便你继续观察时间窗进窗/退窗；按 Ctrl+C 结束本轮联调并自动清理后台进程"
    log "本次联调目录: ${RUN_DIR}"
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

require_file "${START_SCRIPT}"
require_file "${POST_SCRIPT}"
mkdir -p "${RUN_DIR}"

log "step1: start service stack via manual_service_monitor_demo.sh"
DEMO_DETACH=1 \
RUN_DIR="${RUN_DIR}" \
BACKEND_PORT="${BACKEND_PORT}" \
PROXY_PORT="${PROXY_PORT}" \
BASE_URL="${BASE_URL}" \
TRACE_AI_BASE_URL="${TRACE_AI_BASE_URL}" \
FRONTEND_URL="${FRONTEND_URL}" \
SERVICE_MONITOR_WINDOW_MINUTES="${SERVICE_MONITOR_WINDOW_MINUTES}" \
bash "${START_SCRIPT}"

if [[ ! -f "${RUN_INFO_FILE}" ]]; then
    echo "fatal: run info file not found: ${RUN_INFO_FILE}" >&2
    exit 1
fi

# run_info.env 只包含脚本自己生成的常量和 PID，用它把运行现场重新读回来，
# 这样当前包装器就能继续接管驻留与清理，不需要重复解析日志。
source "${RUN_INFO_FILE}"

log "step2: post one random complex trace"
python3 "${POST_SCRIPT}" "${BASE_URL}"

echo
log "step3: wait ${WAIT_SECONDS}s for OnTick publish and AI summary backfill"
sleep "${WAIT_SECONDS}"

echo
log "step4: GET ${BASE_URL}/service-monitor/runtime"
curl -sS "${BASE_URL}/service-monitor/runtime" | python3 -m json.tool

hold_until_interrupt
