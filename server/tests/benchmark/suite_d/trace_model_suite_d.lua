-- Suite D 只测 clean end-trace 扩展性。
-- 这里故意不复用 mixed/capacity/token 那些模型，避免主扩展曲线混入脏时序或容量触发语义。

local spans_per_trace = 8
local setup_counter = 0
-- wrk 的 thread:set 会把 thread_id 注入到每个工作线程的全局表里；
-- 这里不能再定义同名 local，否则 Lua 会优先读本地 0，所有线程都会撞到同一段 trace_key。
local wrk_thread_id = 0
local next_trace_key = 0
local current_trace_key = 0
local current_span_index = 0
local logical_now_ms = 0

local templates = {
    { service_name = "svc-order", names = { "root", "validate", "db", "cache", "rpc", "mq", "biz", "finish" } },
    { service_name = "svc-user", names = { "root", "auth", "profile", "db", "cache", "notify", "render", "finish" } },
    { service_name = "svc-payment", names = { "root", "check", "risk", "db", "ledger", "wallet", "audit", "finish" } },
    { service_name = "svc-search", names = { "root", "rewrite", "index", "rank", "cache", "filter", "render", "finish" } },
    { service_name = "svc-gateway", names = { "root", "decode", "route", "rpc", "fallback", "metrics", "encode", "finish" } },
}
local template_cursor = 0
local current_template = templates[1]

local function parse_positive_int(value, fallback)
    local parsed = tonumber(value)
    if parsed == nil or parsed <= 0 then
        return fallback
    end
    return math.floor(parsed)
end

local function json_escape(value)
    value = tostring(value)
    value = value:gsub("\\", "\\\\")
    value = value:gsub("\"", "\\\"")
    value = value:gsub("\n", "\\n")
    value = value:gsub("\r", "\\r")
    value = value:gsub("\t", "\\t")
    return value
end

local function encode_string_map(map)
    local pairs_buf = {}
    for key, value in pairs(map) do
        pairs_buf[#pairs_buf + 1] = "\"" .. json_escape(key) .. "\":\"" .. json_escape(value) .. "\""
    end
    return "{" .. table.concat(pairs_buf, ",") .. "}"
end

local function pick_template()
    template_cursor = (template_cursor % #templates) + 1
    return templates[template_cursor]
end

local function current_span_name(template, span_index)
    local names = template.names
    return names[((span_index - 1) % #names) + 1]
end

local function start_new_trace()
    current_template = pick_template()
    current_trace_key = next_trace_key
    next_trace_key = next_trace_key + 1
    current_span_index = 0
end

local function build_span()
    current_span_index = current_span_index + 1
    logical_now_ms = logical_now_ms + 1

    local span = {
        trace_key = current_trace_key,
        span_id = current_span_index,
        start_time_ms = logical_now_ms,
        name = current_span_name(current_template, current_span_index),
        service_name = current_template.service_name,
        trace_end = current_span_index >= spans_per_trace,
        attributes = {
            bench_suite = "suite_d",
            bench_mode = "end",
            -- 把 wrk 线程 ID 写进 attributes，后面查库或看日志时能确认 trace_key 分片是否生效。
            thread_id = tostring(wrk_thread_id),
        },
    }

    if current_span_index > 1 then
        span.parent_span_id = current_span_index - 1
    end

    return span
end

local function encode_span_json(span)
    local parts = {
        "\"trace_key\":" .. tostring(span.trace_key),
        "\"span_id\":" .. tostring(span.span_id),
        "\"start_time_ms\":" .. tostring(span.start_time_ms),
        "\"name\":\"" .. json_escape(span.name) .. "\"",
        "\"service_name\":\"" .. json_escape(span.service_name) .. "\"",
        "\"trace_end\":" .. tostring(span.trace_end),
        "\"attributes\":" .. encode_string_map(span.attributes),
    }

    if span.parent_span_id ~= nil then
        parts[#parts + 1] = "\"parent_span_id\":" .. tostring(span.parent_span_id)
    end

    return "{" .. table.concat(parts, ",") .. "}"
end

function setup(thread)
    setup_counter = setup_counter + 1
    thread:set("thread_id", setup_counter)
end

function init(args)
    spans_per_trace = parse_positive_int(args[2], spans_per_trace)
    -- 必须从 _G.thread_id 读取 wrk 在 setup() 阶段注入的线程编号；
    -- 既然每个 wrk 线程都有独立编号，那么每个线程就能拿到互不重叠的 trace_key 区间，
    -- 避免 trace_summary.trace_id UNIQUE 冲突把整个 SQLite batch 回滚。
    wrk_thread_id = _G.thread_id or 0
    next_trace_key = (wrk_thread_id + 1) * 1000000000
    logical_now_ms = os.time() * 1000
    start_new_trace()
end

function request()
    local span = build_span()
    local body = encode_span_json(span)
    local request_str = wrk.format("POST", "/logs/spans", {
        ["Content-Type"] = "application/json"
    }, body)

    if current_span_index >= spans_per_trace then
        start_new_trace()
    end

    return request_str
end

function done(summary, latency, requests)
    local request_count = 0
    if summary ~= nil and summary.requests ~= nil then
        request_count = summary.requests
    elseif requests ~= nil and requests.total ~= nil then
        request_count = requests.total
    end

    local offered_traces = math.floor(request_count / spans_per_trace)
    local p95_ms = latency:percentile(95.0) / 1000.0
    local p99_ms = latency:percentile(99.0) / 1000.0
    io.write(string.format(
        "trace_model_suite_d metrics: offered_traces=%d spans_per_trace=%d latency_p95_ms=%.2f latency_p99_ms=%.2f\n",
        offered_traces,
        spans_per_trace,
        p95_ms,
        p99_ms
    ))
end
