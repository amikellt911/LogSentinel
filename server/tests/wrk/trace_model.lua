-- 用法示例：
-- wrk -t1 -c100 -d30s -s trace_model.lua http://127.0.0.1:8080 -- end 8 4 2048
-- 位置参数含义：
--   1) mode: end | capacity | token | timeout | mixed
--   2) spans_per_trace: 单条 trace 默认 span 数（默认 8）
--   3) timeout_cutoff: timeout 模型发到第几个 span 后故意断尾（默认 4）
--   4) token_payload_size: token 模型大字段大小（默认 2048 字节）

local mode = "end"
local spans_per_trace = 8
local timeout_cutoff = 4
local token_payload_size = 2048
local token_trigger_spans = 3

-- wrk 会在 setup 阶段给每个压测线程分一个独立 Lua 状态。
-- 这里用 thread_id 给不同线程分不同 trace_key 段，避免多线程压测时 trace_key 撞车。
local setup_counter = 0
local thread_id = 0

-- 下面这些状态都属于“当前 wrk 线程”的本地状态。
-- 既然一条 trace 要拆成多次 HTTP 请求发送，那么这些变量就负责跨 request() 保存当前 trace 的进度。
local next_trace_key = 0
local current_trace_key = 0
local current_span_index = 0
local logical_now_ms = 0
local current_mode = "end"
local current_template = nil

-- 模板池用“顺序回环”而不是随机：
-- 既然 benchmark 更在乎可重复性，那么 round-robin 比 random 更稳，便于前后两次压测横向比较。
local template_cursor = {
    ["end"] = 0,
    ["capacity"] = 0,
    ["token"] = 0,
    ["timeout"] = 0,
}

-- mixed 模型也走固定轮转，而不是完全随机。
-- 这样 70/20/10 的分布更稳定，不会因为一次随机偏差把结果测脏。
local mixed_schedule = {
    "end", "end", "end", "end", "end", "end", "end",
    "timeout", "timeout",
    "capacity",
}
local mixed_cursor = 0

local templates = {
    ["end"] = {
        { service_name = "svc-order", names = { "root", "validate", "db", "cache", "rpc", "mq", "biz", "finish" } },
        { service_name = "svc-user", names = { "root", "auth", "profile", "db", "cache", "notify", "render", "finish" } },
        { service_name = "svc-payment", names = { "root", "check", "risk", "db", "ledger", "wallet", "audit", "finish" } },
        { service_name = "svc-search", names = { "root", "rewrite", "index", "rank", "cache", "filter", "render", "finish" } },
        { service_name = "svc-gateway", names = { "root", "decode", "route", "rpc", "fallback", "metrics", "encode", "finish" } },
        { service_name = "svc-inventory", names = { "root", "reserve", "db", "cache", "recheck", "commit", "notify", "finish" } },
    },
    ["capacity"] = {
        { service_name = "svc-cap-order", names = { "cap-root", "cap-db", "cap-cache", "cap-rpc" } },
        { service_name = "svc-cap-batch", names = { "cap-root", "cap-parse", "cap-merge", "cap-flush" } },
        { service_name = "svc-cap-sync", names = { "cap-root", "cap-read", "cap-write", "cap-ack" } },
        { service_name = "svc-cap-export", names = { "cap-root", "cap-scan", "cap-pack", "cap-send" } },
    },
    ["token"] = {
        { service_name = "svc-token-a", names = { "token-root", "token-enrich", "token-flush" }, token_char = "a" },
        { service_name = "svc-token-b", names = { "token-root", "token-join", "token-flush" }, token_char = "b" },
        { service_name = "svc-token-c", names = { "token-root", "token-pack", "token-flush" }, token_char = "c" },
        { service_name = "svc-token-d", names = { "token-root", "token-annotate", "token-flush" }, token_char = "d" },
    },
    ["timeout"] = {
        { service_name = "svc-timeout-order", names = { "timeout-root", "timeout-db", "timeout-cache", "timeout-rpc" } },
        { service_name = "svc-timeout-user", names = { "timeout-root", "timeout-auth", "timeout-profile", "timeout-render" } },
        { service_name = "svc-timeout-report", names = { "timeout-root", "timeout-scan", "timeout-merge", "timeout-upload" } },
        { service_name = "svc-timeout-gateway", names = { "timeout-root", "timeout-route", "timeout-rpc", "timeout-fallback" } },
    },
}

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

local function pick_mode()
    if mode ~= "mixed" then
        return mode
    end
    mixed_cursor = (mixed_cursor % #mixed_schedule) + 1
    return mixed_schedule[mixed_cursor]
end

local function pick_template(model_name)
    local pool = templates[model_name]
    template_cursor[model_name] = (template_cursor[model_name] % #pool) + 1
    return pool[template_cursor[model_name]]
end

local function current_span_name(template, span_index)
    local names = template.names
    return names[((span_index - 1) % #names) + 1]
end

local function start_new_trace()
    current_mode = pick_mode()
    current_template = pick_template(current_mode)
    current_trace_key = next_trace_key
    next_trace_key = next_trace_key + 1
    current_span_index = 0
end

local function build_attributes()
    local attrs = {
        bench_mode = current_mode,
        template = current_template.service_name,
        thread_id = tostring(thread_id),
    }

    if current_mode == "token" then
        attrs.payload = string.rep(current_template.token_char, token_payload_size)
    end

    return attrs
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
        trace_end = false,
        attributes = build_attributes(),
    }

    if current_span_index > 1 then
        span.parent_span_id = current_span_index - 1
    end

    -- 不同模型的核心差异，就在这里决定“这条 trace 什么时候结束”。
    -- 既然 benchmark 的目标是按分发触发机制拆开测，那么这里必须把各模型的结束语义写死。
    if current_mode == "end" and current_span_index >= spans_per_trace then
        span.trace_end = true
    end

    return span
end

local function should_roll_trace()
    if current_mode == "end" then
        return current_span_index >= spans_per_trace
    end
    if current_mode == "capacity" then
        return current_span_index >= spans_per_trace
    end
    if current_mode == "token" then
        return current_span_index >= token_trigger_spans
    end
    if current_mode == "timeout" then
        return current_span_index >= timeout_cutoff
    end
    return false
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
    if args[1] ~= nil and args[1] ~= "" then
        mode = args[1]
    end
    spans_per_trace = parse_positive_int(args[2], spans_per_trace)
    timeout_cutoff = parse_positive_int(args[3], timeout_cutoff)
    token_payload_size = parse_positive_int(args[4], token_payload_size)

    if mode ~= "end" and mode ~= "capacity" and mode ~= "token" and mode ~= "timeout" and mode ~= "mixed" then
        error("unsupported mode: " .. tostring(mode))
    end

    -- 线程号由 wrk 在 setup() 阶段注入。
    -- 这里给每个线程预留一大段 trace_key 范围，避免多线程发压时不同线程生成同一个 trace_key。
    thread_id = thread_id or 0
    next_trace_key = (thread_id + 1) * 1000000000
    logical_now_ms = os.time() * 1000
    start_new_trace()
end

function request()
    local span = build_span()
    local body = encode_span_json(span)
    local request_str = wrk.format("POST", "/logs/spans", {
        ["Content-Type"] = "application/json"
    }, body)

    if should_roll_trace() then
        start_new_trace()
    end

    return request_str
end

function done(summary, latency, requests)
    io.write(string.format(
        "trace_model done: mode=%s, threads=%d, requests=%d, timeout_cutoff=%d, token_payload_size=%d\n",
        mode,
        summary.threads,
        summary.requests,
        timeout_cutoff,
        token_payload_size
    ))
end
