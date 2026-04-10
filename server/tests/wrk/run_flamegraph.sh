#!/usr/bin/env bash

set -euo pipefail

# 这份脚本只做“第一张 CPU 火焰图”的最小实验编排：
# 1. 按 profile 启动 LogSentinel
# 2. 做一小段 warmup，让 SQLite/Python proxy/连接先热起来
# 3. 用 perf 附着到服务端 PID 采样
# 4. 在 perf 采样窗口里跑正式 wrk
# 5. 把 perf.data 转成 flamegraph.svg，并自动 cleanup

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SERVER_BIN="${ROOT_DIR}/server/build/LogSentinel"
# 默认仍走老的 trace_model.lua，保证历史 flamegraph 口径不被这次 active-pool 升级直接改脏。
# 如果要切到新脚本，调用方只需要在环境变量里覆写 WRK_SCRIPT 即可。
WRK_SCRIPT="${WRK_SCRIPT:-${ROOT_DIR}/server/tests/wrk/trace_model.lua}"
PACED_SENDER="${ROOT_DIR}/server/tests/wrk/trace_paced_sender.py"
RESULT_ROOT="${ROOT_DIR}/server/tests/wrk/results"
FLAMEGRAPH_DIR="${FLAMEGRAPH_DIR:-${HOME}/tools/FlameGraph}"
PROFILE="${1:-end}"

PORT="${PORT:-8080}"
SERVER_CPUSET="${SERVER_CPUSET:-1-2}"
WRK_CPUSET="${WRK_CPUSET:-0}"
WORKER_THREADS="${WORKER_THREADS:-3}"
WORKER_QUEUE_SIZE="${WORKER_QUEUE_SIZE:-2048}"
WARMUP_DURATION="${WARMUP_DURATION:-3s}"
WARMUP_CONNECTIONS="${WARMUP_CONNECTIONS:-20}"
DURATION="${DURATION:-15s}"
CONNECTIONS="${CONNECTIONS:-100}"
WRK_THREADS="${WRK_THREADS:-1}"
ACTIVE_POOL_SIZE="${ACTIVE_POOL_SIZE:-4}"
TRACE_WRK_ROLE_PLAN="${TRACE_WRK_ROLE_PLAN:-}"
LOAD_GENERATOR="${LOAD_GENERATOR:-wrk}"
PACED_BATCH_TRACES="${PACED_BATCH_TRACES:-5}"
PACED_BATCH_SLEEP_MS="${PACED_BATCH_SLEEP_MS:-100}"
PACED_REQUEST_TIMEOUT_MS="${PACED_REQUEST_TIMEOUT_MS:-1000}"
PACED_SERVICE_NAME="${PACED_SERVICE_NAME:-svc-paced}"
PERF_FREQ="${PERF_FREQ:-99}"
PERF_CALL_GRAPH="${PERF_CALL_GRAPH:-dwarf}"
PERF_EVENT="${PERF_EVENT:-cpu-clock}"
WARMUP_SETTLE_SEC="${WARMUP_SETTLE_SEC:-1}"
DRAIN_WAIT_SEC="${DRAIN_WAIT_SEC:-10}"
WAIT_PORT_RETRY="${WAIT_PORT_RETRY:-50}"
WAIT_PORT_SLEEP_SEC="${WAIT_PORT_SLEEP_SEC:-0.2}"

SERVER_PID=""
SERVER_LAUNCH_PID=""
RUN_DIR=""
SERVER_LOG=""
WARMUP_LOG=""
WRK_LOG=""
PERF_DATA=""
PERF_SCRIPT=""
FLAME_SVG=""
TRACE_DB="${ROOT_DIR}/server/tests/wrk/results/trace-flame.db"

usage() {
    cat <<'EOF'
用法:
  server/tests/wrk/run_flamegraph.sh [profile]

支持的 profile:
  end
  capacity
  token
  timeout
  mixed

常用环境变量:
  PORT=8080
  SERVER_CPUSET="1-2"
  WRK_CPUSET="0"
  WORKER_THREADS=3
  LOAD_GENERATOR=wrk|paced
  WARMUP_DURATION=3s
  WARMUP_CONNECTIONS=20
  DURATION=15s
  CONNECTIONS=100
  WRK_THREADS=1
  WRK_SCRIPT=/path/to/trace_model_active_pool.lua
  ACTIVE_POOL_SIZE=4
  TRACE_WRK_ROLE_PLAN="end,end,end,end,capacity,capacity,timeout,timeout"
  PACED_BATCH_TRACES=5
  PACED_BATCH_SLEEP_MS=100
  PACED_REQUEST_TIMEOUT_MS=1000
  PACED_SERVICE_NAME=svc-paced
  PERF_FREQ=99
  PERF_CALL_GRAPH=dwarf
  PERF_EVENT=cpu-clock
  WARMUP_SETTLE_SEC=1
  DRAIN_WAIT_SEC=10
  FLAMEGRAPH_DIR=~/tools/FlameGraph
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

taskset_wrap() {
    local cpuset="$1"
    shift
    if [[ -n "${cpuset}" ]]; then
        taskset -c "${cpuset}" "$@"
    else
        "$@"
    fi
}

cleanup() {
    if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" >/dev/null 2>&1; then
        log "stopping LogSentinel pid=${SERVER_PID}"
        kill -TERM "${SERVER_PID}" >/dev/null 2>&1 || true
        wait "${SERVER_PID}" >/dev/null 2>&1 || true
    fi
    if [[ -n "${SERVER_LAUNCH_PID}" ]] && [[ "${SERVER_LAUNCH_PID}" != "${SERVER_PID}" ]] && kill -0 "${SERVER_LAUNCH_PID}" >/dev/null 2>&1; then
        kill -TERM "${SERVER_LAUNCH_PID}" >/dev/null 2>&1 || true
        wait "${SERVER_LAUNCH_PID}" >/dev/null 2>&1 || true
    fi
}

trap cleanup EXIT INT TERM

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

server_thread_ids() {
    local pid="$1"
    ps -T -p "${pid}" -o spid= 2>/dev/null | awk '{$1=$1; print}' | paste -sd, -
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
    local pid
    for pid in $(port_listener_pids "${port}"); do
        local args
        args="$(ps -p "${pid}" -o args= 2>/dev/null || true)"
        if [[ "${args}" == *"${SERVER_BIN}"* ]]; then
            log "stopping stale LogSentinel on port ${port}, pid=${pid}"
            kill -TERM "${pid}" >/dev/null 2>&1 || true
            sleep 1
        else
            echo "fatal: port ${port} is occupied by non-benchmark process pid=${pid}" >&2
            ps -p "${pid}" -o pid,ppid,user,comm,args >&2 || true
            exit 1
        fi
    done
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
    SERVER_LOG="${RUN_DIR}/server.log"
    TRACE_DB="${RUN_DIR}/trace-flame.db"
    local listener_pid=""
    ensure_port_available "${PORT}"

    log "starting LogSentinel profile=${PROFILE} worker_threads=${WORKER_THREADS}"
    taskset_wrap "${SERVER_CPUSET}" \
        "${SERVER_BIN}" \
        --db "${TRACE_DB}" \
        --port "${PORT}" \
        --auto-start-proxy \
        --auto-start-webhook-mock \
        --trace-ai-provider mock \
        --worker-threads "${WORKER_THREADS}" \
        --worker-queue-size "${WORKER_QUEUE_SIZE}" \
        --trace-capacity "${TRACE_CAPACITY}" \
        --trace-token-limit "${TRACE_TOKEN_LIMIT}" \
        --trace-sweep-interval-ms "${TRACE_SWEEP_INTERVAL_MS}" \
        --trace-idle-timeout-ms "${TRACE_IDLE_TIMEOUT_MS}" \
        --trace-max-dispatch-per-tick "${TRACE_MAX_DISPATCH_PER_TICK}" \
        --trace-buffered-span-limit "${TRACE_BUFFERED_SPAN_LIMIT}" \
        --trace-active-session-limit "${TRACE_ACTIVE_SESSION_LIMIT}" \
        > "${SERVER_LOG}" 2>&1 &
    SERVER_LAUNCH_PID=$!

    if ! wait_for_port "${PORT}"; then
        echo "fatal: LogSentinel failed to listen on port ${PORT}" >&2
        tail -n 50 "${SERVER_LOG}" >&2 || true
        exit 1
    fi

    for listener_pid in $(port_listener_pids "${PORT}"); do
        local args
        args="$(ps -p "${listener_pid}" -o args= 2>/dev/null || true)"
        if [[ "${args}" == *"${SERVER_BIN}"* ]]; then
            SERVER_PID="${listener_pid}"
            break
        fi
    done

    if [[ -z "${SERVER_PID}" ]]; then
        echo "fatal: unable to resolve benchmark LogSentinel listener pid on port ${PORT}" >&2
        lsof -nP -iTCP:"${PORT}" -sTCP:LISTEN >&2 || true
        exit 1
    fi

    if ! port_owned_by_pid "${PORT}" "${SERVER_PID}"; then
        echo "fatal: port ${PORT} became ready, but owner is not the benchmark LogSentinel pid=${SERVER_PID}" >&2
        lsof -nP -iTCP:"${PORT}" -sTCP:LISTEN >&2 || true
        exit 1
    fi
}

run_warmup() {
    WARMUP_LOG="${RUN_DIR}/warmup.log"
    log "warmup profile=${PROFILE} connections=${WARMUP_CONNECTIONS} duration=${WARMUP_DURATION}"
    # warmup 和正式阶段必须吃同一套 Lua 脚本与角色计划，
    # 否则“热起来的是老模型、采样的是新模型”，火焰图会被两套流量口径混脏。
    TRACE_WRK_MODE="${MODE}" TRACE_WRK_THREADS="${WRK_THREADS}" TRACE_WRK_ROLE_PLAN="${TRACE_WRK_ROLE_PLAN}" \
        taskset_wrap "${WRK_CPUSET}" \
        wrk -t"${WRK_THREADS}" -c"${WARMUP_CONNECTIONS}" -d"${WARMUP_DURATION}" --latency \
        -s "${WRK_SCRIPT}" "http://127.0.0.1:${PORT}" -- \
        "${MODE}" "${SPANS_PER_TRACE}" "${TIMEOUT_CUTOFF}" "${TOKEN_PAYLOAD_SIZE}" "${ACTIVE_POOL_SIZE}" \
        > "${WARMUP_LOG}" 2>&1
    sleep "${WARMUP_SETTLE_SEC}"
}

run_perf_and_load() {
    WRK_LOG="${RUN_DIR}/wrk.log"
    PERF_DATA="${RUN_DIR}/perf.data"
    PERF_SCRIPT="${RUN_DIR}/perf.script"
    FLAME_SVG="${RUN_DIR}/trace.svg"

    local perf_targets
    perf_targets="$(server_thread_ids "${SERVER_PID}")"
    if [[ -z "${perf_targets}" ]]; then
        echo "fatal: unable to resolve LogSentinel thread ids for pid=${SERVER_PID}" >&2
        exit 1
    fi

    local duration_sec="${DURATION%s}"
    local perf_duration_sec=$(( duration_sec + DRAIN_WAIT_SEC + 1 ))

    log "recording flamegraph profile=${PROFILE} pid=${SERVER_PID} tids=${perf_targets} event=${PERF_EVENT} freq=${PERF_FREQ} perf_duration_sec=${perf_duration_sec}"
    perf record -e "${PERF_EVENT}" -F "${PERF_FREQ}" -g --call-graph "${PERF_CALL_GRAPH}" \
        -t "${perf_targets}" -o "${PERF_DATA}" -- sleep "${perf_duration_sec}" >/dev/null 2>&1 &
    local perf_pid=$!

    sleep 1
    if [[ "${LOAD_GENERATOR}" == "wrk" ]]; then
        log "running wrk profile=${PROFILE} connections=${CONNECTIONS} duration=${DURATION}"
        {
            echo "===== WRK RUN BEGIN ====="
            echo "profile=${PROFILE}"
            echo "worker_threads=${WORKER_THREADS}"
            echo "connections=${CONNECTIONS}"
            echo "duration=${DURATION}"
            echo "wrk_threads=${WRK_THREADS}"
            echo "wrk_script=${WRK_SCRIPT}"
            echo "active_pool_size=${ACTIVE_POOL_SIZE}"
            echo "trace_wrk_role_plan=${TRACE_WRK_ROLE_PLAN:-<unset>}"
            echo "server_cpuset=${SERVER_CPUSET}"
            echo "wrk_cpuset=${WRK_CPUSET}"
            echo "perf_freq=${PERF_FREQ}"
            echo "perf_call_graph=${PERF_CALL_GRAPH}"
            echo "perf_event=${PERF_EVENT}"
            echo "load_generator=${LOAD_GENERATOR}"
            echo
            TRACE_WRK_MODE="${MODE}" TRACE_WRK_THREADS="${WRK_THREADS}" TRACE_WRK_ROLE_PLAN="${TRACE_WRK_ROLE_PLAN}" \
                taskset_wrap "${WRK_CPUSET}" \
                wrk -t"${WRK_THREADS}" -c"${CONNECTIONS}" -d"${DURATION}" --latency \
                -s "${WRK_SCRIPT}" "http://127.0.0.1:${PORT}" -- \
                "${MODE}" "${SPANS_PER_TRACE}" "${TIMEOUT_CUTOFF}" "${TOKEN_PAYLOAD_SIZE}" "${ACTIVE_POOL_SIZE}"
            echo "===== WRK RUN END ====="
        } | tee "${WRK_LOG}"
    elif [[ "${LOAD_GENERATOR}" == "paced" ]]; then
        log "running paced sender profile=${PROFILE} batch_traces=${PACED_BATCH_TRACES} batch_sleep_ms=${PACED_BATCH_SLEEP_MS} duration=${DURATION}"
        {
            echo "===== PACED RUN BEGIN ====="
            echo "profile=${PROFILE}"
            echo "worker_threads=${WORKER_THREADS}"
            echo "duration=${DURATION}"
            echo "server_cpuset=${SERVER_CPUSET}"
            echo "wrk_cpuset=${WRK_CPUSET}"
            echo "perf_freq=${PERF_FREQ}"
            echo "perf_call_graph=${PERF_CALL_GRAPH}"
            echo "perf_event=${PERF_EVENT}"
            echo "load_generator=${LOAD_GENERATOR}"
            echo "paced_batch_traces=${PACED_BATCH_TRACES}"
            echo "paced_batch_sleep_ms=${PACED_BATCH_SLEEP_MS}"
            echo "paced_request_timeout_ms=${PACED_REQUEST_TIMEOUT_MS}"
            echo
            taskset_wrap "${WRK_CPUSET}" \
                python3 "${PACED_SENDER}" \
                --url "http://127.0.0.1:${PORT}/logs/spans" \
                --mode "${MODE}" \
                --spans-per-trace "${SPANS_PER_TRACE}" \
                --batch-traces "${PACED_BATCH_TRACES}" \
                --batch-sleep-ms "${PACED_BATCH_SLEEP_MS}" \
                --duration-sec "${DURATION%s}" \
                --request-timeout-ms "${PACED_REQUEST_TIMEOUT_MS}" \
                --service-name "${PACED_SERVICE_NAME}"
            echo "===== PACED RUN END ====="
        } | tee "${WRK_LOG}"
    else
        echo "fatal: unsupported LOAD_GENERATOR=${LOAD_GENERATOR}, expected wrk or paced" >&2
        exit 1
    fi

    if (( DRAIN_WAIT_SEC > 0 )); then
        log "draining backend profile=${PROFILE} drain_wait_sec=${DRAIN_WAIT_SEC}"
        sleep "${DRAIN_WAIT_SEC}"
    fi

    wait "${perf_pid}"

    log "collapsing perf stacks"
    perf script -i "${PERF_DATA}" > "${PERF_SCRIPT}"
    "${FLAMEGRAPH_DIR}/stackcollapse-perf.pl" "${PERF_SCRIPT}" > "${RUN_DIR}/trace.folded"
    "${FLAMEGRAPH_DIR}/flamegraph.pl" "${RUN_DIR}/trace.folded" > "${FLAME_SVG}"
}

emit_trace_db_stats() {
    if ! command -v sqlite3 >/dev/null 2>&1; then
        log "skip trace db stats because sqlite3 is unavailable"
        return 0
    fi

    if [[ ! -f "${TRACE_DB}" ]]; then
        log "skip trace db stats because db file is missing: ${TRACE_DB}"
        return 0
    fi

    local summary_count span_count span_stats
    summary_count="$(sqlite3 "${TRACE_DB}" 'select count(*) from trace_summary;' 2>/dev/null || echo "0")"
    span_count="$(sqlite3 "${TRACE_DB}" 'select count(*) from trace_span;' 2>/dev/null || echo "0")"
    span_stats="$(sqlite3 "${TRACE_DB}" 'select coalesce(min(span_count),0), coalesce(avg(span_count),0), coalesce(max(span_count),0) from trace_summary;' 2>/dev/null || echo "0|0|0")"

    {
        echo "trace_db=${TRACE_DB}"
        echo "trace_summary_count=${summary_count}"
        echo "trace_span_count=${span_count}"
        echo "trace_span_stats=${span_stats}"
    } | tee -a "${RUN_DIR}/run-summary.log"
}

if [[ "${PROFILE}" == "-h" || "${PROFILE}" == "--help" ]]; then
    usage
    exit 0
fi

require_file "${SERVER_BIN}"
require_file "${WRK_SCRIPT}"
require_file "${PACED_SENDER}"
require_file "${FLAMEGRAPH_DIR}/stackcollapse-perf.pl"
require_file "${FLAMEGRAPH_DIR}/flamegraph.pl"
require_command wrk
require_command python3
require_command perf
require_command ss
require_command lsof
require_command sqlite3

set_profile_args "${PROFILE}"

mkdir -p "${RESULT_ROOT}"
RUN_DIR="${RESULT_ROOT}/$(date '+%Y%m%d-%H%M%S')-flamegraph-${PROFILE}"
mkdir -p "${RUN_DIR}"

{
    echo "profile=${PROFILE}"
    echo "port=${PORT}"
    echo "worker_threads=${WORKER_THREADS}"
    echo "worker_queue_size=${WORKER_QUEUE_SIZE}"
    echo "warmup_duration=${WARMUP_DURATION}"
    echo "warmup_connections=${WARMUP_CONNECTIONS}"
    echo "warmup_settle_sec=${WARMUP_SETTLE_SEC}"
    echo "drain_wait_sec=${DRAIN_WAIT_SEC}"
    echo "duration=${DURATION}"
    echo "connections=${CONNECTIONS}"
    echo "wrk_threads=${WRK_THREADS}"
    echo "wrk_script=${WRK_SCRIPT}"
    echo "active_pool_size=${ACTIVE_POOL_SIZE}"
    echo "trace_wrk_role_plan=${TRACE_WRK_ROLE_PLAN:-<unset>}"
    echo "load_generator=${LOAD_GENERATOR}"
    echo "paced_batch_traces=${PACED_BATCH_TRACES}"
    echo "paced_batch_sleep_ms=${PACED_BATCH_SLEEP_MS}"
    echo "paced_request_timeout_ms=${PACED_REQUEST_TIMEOUT_MS}"
    echo "paced_service_name=${PACED_SERVICE_NAME}"
    echo "server_cpuset=${SERVER_CPUSET}"
    echo "wrk_cpuset=${WRK_CPUSET}"
    echo "perf_freq=${PERF_FREQ}"
    echo "perf_call_graph=${PERF_CALL_GRAPH}"
    echo "perf_event=${PERF_EVENT}"
    echo "trace_capacity=${TRACE_CAPACITY}"
    echo "trace_token_limit=${TRACE_TOKEN_LIMIT}"
    echo "trace_sweep_interval_ms=${TRACE_SWEEP_INTERVAL_MS}"
    echo "trace_idle_timeout_ms=${TRACE_IDLE_TIMEOUT_MS}"
    echo "trace_max_dispatch_per_tick=${TRACE_MAX_DISPATCH_PER_TICK}"
    echo "trace_buffered_span_limit=${TRACE_BUFFERED_SPAN_LIMIT}"
    echo "trace_active_session_limit=${TRACE_ACTIVE_SESSION_LIMIT}"
    echo "flamegraph_dir=${FLAMEGRAPH_DIR}"
} > "${RUN_DIR}/run-summary.log"

start_server
run_warmup
run_perf_and_load
emit_trace_db_stats

log "flamegraph generated at ${FLAME_SVG}"
