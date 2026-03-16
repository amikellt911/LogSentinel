#!/usr/bin/env bash

set -euo pipefail

# 这份脚本只做最小实验编排：
# 1. 按 profile 选择一套固定的服务端参数和 wrk 参数
# 2. 后台启动 LogSentinel，并等待 8080 ready
# 3. 跑多组 wrk（默认 worker_threads=2/3/4, connections=100/200/500）
# 4. 终端打印 + 文件留痕
# 5. 退出时先抓 shutdown snapshot 埋点，再做有上限的 cleanup，避免残留进程污染下一轮实验

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SERVER_BIN="${ROOT_DIR}/server/build/LogSentinel"
WRK_SCRIPT="${ROOT_DIR}/server/tests/wrk/trace_model.lua"
RESULT_ROOT="${ROOT_DIR}/server/tests/wrk/results"

PROFILE="${1:-}"

PORT="${PORT:-8080}"
DURATION="${DURATION:-30s}"
WRK_THREADS="${WRK_THREADS:-1}"
WORKER_THREAD_SET="${WORKER_THREAD_SET:-2 3 4}"
CONNECTION_SET="${CONNECTION_SET:-100 200 500}"
SERVER_CPUSET="${SERVER_CPUSET:-}"
WRK_CPUSET="${WRK_CPUSET:-}"
WAIT_PORT_RETRY="${WAIT_PORT_RETRY:-50}"
WAIT_PORT_SLEEP_SEC="${WAIT_PORT_SLEEP_SEC:-0.2}"
BENCH_FORCE_STOP_OLD_SERVER="${BENCH_FORCE_STOP_OLD_SERVER:-1}"
STOP_RETRY="${STOP_RETRY:-50}"
STOP_SLEEP_SEC="${STOP_SLEEP_SEC:-0.2}"

SERVER_PID=""
SERVER_LAUNCH_PID=""
RUN_DIR=""
SERVER_LOG=""
SUMMARY_LOG=""

usage() {
    cat <<'EOF'
用法:
  server/tests/wrk/run_bench.sh <profile>

支持的 profile:
  end
  capacity
  token
  timeout
  mixed

可选环境变量:
  PORT=8080
  DURATION=30s
  WRK_THREADS=1
  WORKER_THREAD_SET="2 3 4"
  CONNECTION_SET="100 200 500"
  SERVER_CPUSET="1-2"
  WRK_CPUSET="0"
  STOP_RETRY=50
  STOP_SLEEP_SEC=0.2
EOF
}

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

cleanup() {
    # cleanup 放在 trap 里，本质就和 RAII 的析构一样：
    # 既然脚本中途失败、Ctrl+C、或者正常结束都可能发生，那么统一在退出路径收尾最稳。
    if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" >/dev/null 2>&1; then
        log "stopping LogSentinel pid=${SERVER_PID}"
        kill -TERM "${SERVER_PID}" >/dev/null 2>&1 || true
        if ! wait_for_pid_exit "${SERVER_PID}"; then
            log "LogSentinel pid=${SERVER_PID} did not exit within ${STOP_RETRY} * ${STOP_SLEEP_SEC}s, sending SIGKILL"
            kill -KILL "${SERVER_PID}" >/dev/null 2>&1 || true
            wait "${SERVER_PID}" >/dev/null 2>&1 || true
        fi
    fi
    if [[ -n "${SERVER_LAUNCH_PID}" ]] && [[ "${SERVER_LAUNCH_PID}" != "${SERVER_PID}" ]] && kill -0 "${SERVER_LAUNCH_PID}" >/dev/null 2>&1; then
        kill -TERM "${SERVER_LAUNCH_PID}" >/dev/null 2>&1 || true
        if ! wait_for_pid_exit "${SERVER_LAUNCH_PID}"; then
            kill -KILL "${SERVER_LAUNCH_PID}" >/dev/null 2>&1 || true
            wait "${SERVER_LAUNCH_PID}" >/dev/null 2>&1 || true
        fi
    fi
}

trap cleanup EXIT INT TERM

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

wait_for_port() {
    local port="$1"
    local retry=0
    while (( retry < WAIT_PORT_RETRY )); do
        if ss -ltn "( sport = :${port} )" | grep -q ":${port}"; then
            return 0
        fi
        sleep "${WAIT_PORT_SLEEP_SEC}"
        retry=$((retry + 1))
    done
    return 1
}

port_listener_pids() {
    local port="$1"
    lsof -tiTCP:"${port}" -sTCP:LISTEN 2>/dev/null || true
}

port_owned_by_pid() {
    local port="$1"
    local target_pid="$2"
    local pid
    for pid in $(port_listener_pids "${port}"); do
        if [[ "${pid}" == "${target_pid}" ]]; then
            return 0
        fi
    done
    return 1
}

ensure_port_available() {
    local port="$1"
    local stale_found=0
    local pid

    for pid in $(port_listener_pids "${port}"); do
        local args
        args="$(ps -p "${pid}" -o args= 2>/dev/null || true)"
        if [[ "${args}" == *"${SERVER_BIN}"* ]]; then
            stale_found=1
            if [[ "${BENCH_FORCE_STOP_OLD_SERVER}" == "1" ]]; then
                log "stopping stale LogSentinel on port ${port}, pid=${pid}"
                kill -TERM "${pid}" >/dev/null 2>&1 || true
            else
                echo "fatal: port ${port} is occupied by existing LogSentinel pid=${pid}" >&2
                exit 1
            fi
        else
            echo "fatal: port ${port} is occupied by non-benchmark process pid=${pid}" >&2
            echo "fatal: refusing to kill unknown owner automatically" >&2
            ps -p "${pid}" -o pid,ppid,user,comm,args >&2 || true
            exit 1
        fi
    done

    if [[ "${stale_found}" == "1" ]]; then
        local retry=0
        while (( retry < WAIT_PORT_RETRY )); do
            if [[ -z "$(port_listener_pids "${port}")" ]]; then
                return 0
            fi
            sleep "${WAIT_PORT_SLEEP_SEC}"
            retry=$((retry + 1))
        done
        echo "fatal: port ${port} is still occupied after attempting to stop old LogSentinel" >&2
        lsof -nP -iTCP:"${port}" -sTCP:LISTEN >&2 || true
        exit 1
    fi
}

taskset_wrap() {
    local cpuset="$1"
    shift
    if [[ -n "${cpuset}" ]]; then
        taskset -c "${cpuset}" "$@"
    else
        "$@"
    fi
}

set_profile_args() {
    local profile="$1"

    MODE="${profile}"
    SPANS_PER_TRACE=8
    TIMEOUT_CUTOFF=4
    TOKEN_PAYLOAD_SIZE=2048

    TRACE_CAPACITY=16
    TRACE_TOKEN_LIMIT=0
    TRACE_SWEEP_INTERVAL_MS=200
    TRACE_IDLE_TIMEOUT_MS=1200
    TRACE_MAX_DISPATCH_PER_TICK=64
    TRACE_BUFFERED_SPAN_LIMIT=4096
    TRACE_ACTIVE_SESSION_LIMIT=512
    WORKER_QUEUE_SIZE=2048

    case "${profile}" in
        end)
            ;;
        capacity)
            TRACE_CAPACITY=8
            TRACE_IDLE_TIMEOUT_MS=2000
            ;;
        token)
            TRACE_TOKEN_LIMIT=2000
            TRACE_IDLE_TIMEOUT_MS=1500
            ;;
        timeout)
            TRACE_SWEEP_INTERVAL_MS=100
            TRACE_IDLE_TIMEOUT_MS=600
            TRACE_MAX_DISPATCH_PER_TICK=32
            ;;
        mixed)
            TRACE_CAPACITY=12
            TRACE_IDLE_TIMEOUT_MS=800
            ;;
        *)
            echo "fatal: unsupported profile: ${profile}" >&2
            usage
            exit 1
            ;;
    esac
}

start_server() {
    local worker_threads="$1"
    local listener_pid=""

    SERVER_LOG="${RUN_DIR}/server-w${worker_threads}.log"
    log "starting LogSentinel profile=${PROFILE} worker_threads=${worker_threads}"
    ensure_port_available "${PORT}"

    if [[ -n "${SERVER_CPUSET}" ]]; then
        taskset -c "${SERVER_CPUSET}" \
            "${SERVER_BIN}" \
            --db "${RUN_DIR}/trace-bench-w${worker_threads}.db" \
            --port "${PORT}" \
            --auto-start-proxy \
            --auto-start-webhook-mock \
            --trace-ai-provider mock \
            --worker-threads "${worker_threads}" \
            --worker-queue-size "${WORKER_QUEUE_SIZE}" \
            --trace-capacity "${TRACE_CAPACITY}" \
            --trace-token-limit "${TRACE_TOKEN_LIMIT}" \
            --trace-sweep-interval-ms "${TRACE_SWEEP_INTERVAL_MS}" \
            --trace-idle-timeout-ms "${TRACE_IDLE_TIMEOUT_MS}" \
            --trace-max-dispatch-per-tick "${TRACE_MAX_DISPATCH_PER_TICK}" \
            --trace-buffered-span-limit "${TRACE_BUFFERED_SPAN_LIMIT}" \
            --trace-active-session-limit "${TRACE_ACTIVE_SESSION_LIMIT}" \
            > "${SERVER_LOG}" 2>&1 &
    else
        "${SERVER_BIN}" \
            --db "${RUN_DIR}/trace-bench-w${worker_threads}.db" \
            --port "${PORT}" \
            --auto-start-proxy \
            --auto-start-webhook-mock \
            --trace-ai-provider mock \
            --worker-threads "${worker_threads}" \
            --worker-queue-size "${WORKER_QUEUE_SIZE}" \
            --trace-capacity "${TRACE_CAPACITY}" \
            --trace-token-limit "${TRACE_TOKEN_LIMIT}" \
            --trace-sweep-interval-ms "${TRACE_SWEEP_INTERVAL_MS}" \
            --trace-idle-timeout-ms "${TRACE_IDLE_TIMEOUT_MS}" \
            --trace-max-dispatch-per-tick "${TRACE_MAX_DISPATCH_PER_TICK}" \
            --trace-buffered-span-limit "${TRACE_BUFFERED_SPAN_LIMIT}" \
            --trace-active-session-limit "${TRACE_ACTIVE_SESSION_LIMIT}" \
            > "${SERVER_LOG}" 2>&1 &
    fi

    SERVER_LAUNCH_PID=$!

    if ! wait_for_port "${PORT}"; then
        echo "fatal: LogSentinel failed to listen on port ${PORT}" >&2
        tail -n 50 "${SERVER_LOG}" >&2 || true
        exit 1
    fi

    listener_pid="$(port_listener_pids "${PORT}" | head -n 1)"
    if [[ -z "${listener_pid}" ]]; then
        echo "fatal: port ${PORT} became ready, but listener pid cannot be resolved" >&2
        tail -n 50 "${SERVER_LOG}" >&2 || true
        exit 1
    fi

    SERVER_PID="${listener_pid}"

    if ! port_owned_by_pid "${PORT}" "${SERVER_PID}"; then
        echo "fatal: port ${PORT} became ready, but owner is not the benchmark LogSentinel pid=${SERVER_PID}" >&2
        echo "fatal: current listener(s):" >&2
        lsof -nP -iTCP:"${PORT}" -sTCP:LISTEN >&2 || true
        tail -n 50 "${SERVER_LOG}" >&2 || true
        exit 1
    fi
}

run_wrk_once() {
    local worker_threads="$1"
    local connections="$2"
    local wrk_log="${RUN_DIR}/wrk-${PROFILE}-w${worker_threads}-c${connections}.log"

    log "running wrk profile=${PROFILE} worker_threads=${worker_threads} connections=${connections}"
    {
        echo "===== WRK RUN BEGIN ====="
        echo "profile=${PROFILE}"
        echo "worker_threads=${worker_threads}"
        echo "connections=${connections}"
        echo "duration=${DURATION}"
        echo "wrk_threads=${WRK_THREADS}"
        echo "server_cpuset=${SERVER_CPUSET:-<unset>}"
        echo "wrk_cpuset=${WRK_CPUSET:-<unset>}"
        echo "trace_capacity=${TRACE_CAPACITY}"
        echo "trace_token_limit=${TRACE_TOKEN_LIMIT}"
        echo "trace_sweep_interval_ms=${TRACE_SWEEP_INTERVAL_MS}"
        echo "trace_idle_timeout_ms=${TRACE_IDLE_TIMEOUT_MS}"
        echo "trace_max_dispatch_per_tick=${TRACE_MAX_DISPATCH_PER_TICK}"
        echo "trace_buffered_span_limit=${TRACE_BUFFERED_SPAN_LIMIT}"
        echo "trace_active_session_limit=${TRACE_ACTIVE_SESSION_LIMIT}"
        echo "worker_queue_size=${WORKER_QUEUE_SIZE}"
        echo
        export TRACE_WRK_MODE="${MODE}"
        export TRACE_WRK_THREADS="${WRK_THREADS}"
        taskset_wrap "${WRK_CPUSET}" \
            wrk -t"${WRK_THREADS}" -c"${connections}" -d"${DURATION}" --latency \
            -s "${WRK_SCRIPT}" "http://127.0.0.1:${PORT}" -- \
            "${MODE}" "${SPANS_PER_TRACE}" "${TIMEOUT_CUTOFF}" "${TOKEN_PAYLOAD_SIZE}"
        echo "===== WRK RUN END ====="
    } | tee "${wrk_log}"
}

emit_runtime_stats() {
    local worker_threads="$1"
    local trace_stats=""
    local buffered_stats=""

    if [[ -n "${SERVER_LOG}" && -f "${SERVER_LOG}" ]]; then
        # benchmark 结束后不该再靠人肉翻 server log。
        # 这里直接把最后一条 Trace/BufferedTrace 埋点摘出来，写进 run-summary，后面抄表更稳。
        trace_stats="$(grep -F "[TraceRuntimeStats]" "${SERVER_LOG}" | tail -n 1 || true)"
        buffered_stats="$(grep -F "[BufferedTraceRuntimeStats]" "${SERVER_LOG}" | tail -n 1 || true)"
    fi

    {
        echo "===== SERVER RUNTIME STATS BEGIN ====="
        echo "worker_threads=${worker_threads}"
        echo "server_log=${SERVER_LOG:-<unset>}"
        if [[ -n "${trace_stats}" ]]; then
            echo "${trace_stats}"
        else
            echo "[TraceRuntimeStats] <missing>"
        fi
        if [[ -n "${buffered_stats}" ]]; then
            echo "${buffered_stats}"
        else
            echo "[BufferedTraceRuntimeStats] <missing>"
        fi
        echo "===== SERVER RUNTIME STATS END ====="
    } | tee -a "${SUMMARY_LOG}"
}

if [[ -z "${PROFILE}" ]]; then
    usage
    exit 1
fi

require_file "${SERVER_BIN}"
require_file "${WRK_SCRIPT}"
require_command wrk
require_command ss

set_profile_args "${PROFILE}"

mkdir -p "${RESULT_ROOT}"
RUN_DIR="${RESULT_ROOT}/$(date '+%Y%m%d-%H%M%S')-${PROFILE}"
mkdir -p "${RUN_DIR}"
SUMMARY_LOG="${RUN_DIR}/run-summary.log"

{
    echo "profile=${PROFILE}"
    echo "duration=${DURATION}"
    echo "wrk_threads=${WRK_THREADS}"
    echo "worker_thread_set=${WORKER_THREAD_SET}"
    echo "connection_set=${CONNECTION_SET}"
    echo "server_cpuset=${SERVER_CPUSET:-<unset>}"
    echo "wrk_cpuset=${WRK_CPUSET:-<unset>}"
    echo "trace_capacity=${TRACE_CAPACITY}"
    echo "trace_token_limit=${TRACE_TOKEN_LIMIT}"
    echo "trace_sweep_interval_ms=${TRACE_SWEEP_INTERVAL_MS}"
    echo "trace_idle_timeout_ms=${TRACE_IDLE_TIMEOUT_MS}"
    echo "trace_max_dispatch_per_tick=${TRACE_MAX_DISPATCH_PER_TICK}"
    echo "trace_buffered_span_limit=${TRACE_BUFFERED_SPAN_LIMIT}"
    echo "trace_active_session_limit=${TRACE_ACTIVE_SESSION_LIMIT}"
    echo "worker_queue_size=${WORKER_QUEUE_SIZE}"
    echo "mode=${MODE}"
    echo "spans_per_trace=${SPANS_PER_TRACE}"
    echo "timeout_cutoff=${TIMEOUT_CUTOFF}"
    echo "token_payload_size=${TOKEN_PAYLOAD_SIZE}"
} | tee "${SUMMARY_LOG}"

for worker_threads in ${WORKER_THREAD_SET}; do
    start_server "${worker_threads}"
    for connections in ${CONNECTION_SET}; do
        run_wrk_once "${worker_threads}" "${connections}"
        sleep 2
    done
    cleanup
    emit_runtime_stats "${worker_threads}"
    SERVER_PID=""
    SERVER_LAUNCH_PID=""
    sleep 1
done

log "benchmark finished. results saved to ${RUN_DIR}"
