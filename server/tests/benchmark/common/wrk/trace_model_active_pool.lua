-- 用法示例：
--   TRACE_WRK_ROLE_PLAN="end,end,capacity,timeout" \
--   wrk -t4 -c128 -d15s -s trace_model_active_pool.lua http://127.0.0.1:8080 -- mixed 8 4 2048 4
--
-- 位置参数含义：
--   1) default_mode: end | capacity | token | timeout | mixed
--      当没有显式提供 TRACE_WRK_ROLE_PLAN 时，所有线程都使用这个模式。
--   2) spans_per_trace: end/capacity 模型每条 trace 计划发多少个 span（默认 8）
--   3) timeout_cutoff: timeout 模型发到第几个 span 后故意断尾（默认 4）
--   4) token_payload_size: token 模型大字段大小（默认 2048 字节）
--   5) active_pool_size: 每个 wrk 线程同时维护多少条活跃 trace（默认 4）
--
-- 环境变量：
--   TRACE_WRK_ROLE_PLAN="end,end,capacity,timeout"
--     setup() 会按线程顺序给 role_plan 轮转分角色；如果 wrk 线程数比计划更大，就继续回环。

local default_mode = "end"
local spans_per_trace = 8
local timeout_cutoff = 4
local token_payload_size = 2048
local token_trigger_spans = 3
local active_pool_size = 4

-- setup() 跑在控制态，不是每个 wrk worker 自己并发改这份数据。
-- 所以这里的计数器只负责“给第几个线程分什么角色”，不参与 request() 热路径。
local setup_counter = 0
local role_plan_cache = nil

-- 下面这些变量都属于“当前 wrk 线程自己的 Lua state”。
-- 也就是说，一个 worker 线程维护一份 active trace 池；不同线程之间互不共享，也不需要加锁。
local worker_thread_id = 0
local worker_thread_role = nil
local next_trace_key = 0
local logical_now_ms = 0
local request_counter = 0
local mixed_cursor = 0
local active_traces = {}

local template_cursor = {
    ["end"] = 0,
    ["capacity"] = 0,
    ["token"] = 0,
    ["timeout"] = 0,
}

local mixed_schedule = {
    "end", "end", "end", "end", "end", "end", "end",
    "timeout", "timeout",
    "capacity",
}

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

local function normalize_mode(value)
    local normalized = tostring(value or ""):lower()
    if normalized == "end" or normalized == "capacity" or normalized == "token" or normalized == "timeout" or normalized == "mixed" then
        return normalized
    end
    return nil
end

local function parse_role_plan(raw)
    if raw == nil or raw == "" then
        return nil
    end

    local plan = {}
    for token in string.gmatch(raw, "([^,%s]+)") do
        local normalized = normalize_mode(token)
        if normalized == nil then
            error("unsupported role in TRACE_WRK_ROLE_PLAN: " .. tostring(token))
        end
        plan[#plan + 1] = normalized
    end

    if #plan == 0 then
        return nil
    end
    return plan
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

local function pick_mixed_mode()
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

local function pick_role_for_new_trace()
    if worker_thread_role == "mixed" then
        return pick_mixed_mode()
    end
    return worker_thread_role
end

local function allocate_trace_slot(slot_index)
    local slot_mode = pick_role_for_new_trace()
    active_traces[slot_index] = {
        trace_key = next_trace_key,
        span_index = 0,
        mode = slot_mode,
        template = pick_template(slot_mode),
    }
    next_trace_key = next_trace_key + 1
end

local function init_active_trace_pool()
    active_traces = {}
    for slot_index = 1, active_pool_size do
        allocate_trace_slot(slot_index)
    end
end

local function build_attributes(slot, slot_index)
    local attrs = {
        bench_mode = slot.mode,
        thread_id = tostring(worker_thread_id),
        thread_role = tostring(worker_thread_role),
        active_slot = tostring(slot_index),
        template = slot.template.service_name,
    }

    if slot.mode == "token" then
        attrs.payload = string.rep(slot.template.token_char, token_payload_size)
    end

    return attrs
end

local function build_span_for_slot(slot_index)
    local slot = active_traces[slot_index]
    slot.span_index = slot.span_index + 1
    logical_now_ms = logical_now_ms + 1

    local span = {
        trace_key = slot.trace_key,
        span_id = slot.span_index,
        start_time_ms = logical_now_ms,
        name = current_span_name(slot.template, slot.span_index),
        service_name = slot.template.service_name,
        trace_end = false,
        attributes = build_attributes(slot, slot_index),
    }

    if slot.span_index > 1 then
        span.parent_span_id = slot.span_index - 1
    end

    -- end 模型最后一个 span 明确带 trace_end，其他模型则故意不带，逼服务端走别的收口路径。
    if slot.mode == "end" and slot.span_index >= spans_per_trace then
        span.trace_end = true
    end

    return span
end

local function should_roll_slot(slot_index)
    local slot = active_traces[slot_index]
    if slot.mode == "end" then
        return slot.span_index >= spans_per_trace
    end
    if slot.mode == "capacity" then
        return slot.span_index >= spans_per_trace
    end
    if slot.mode == "token" then
        return slot.span_index >= token_trigger_spans
    end
    if slot.mode == "timeout" then
        return slot.span_index >= timeout_cutoff
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
    if role_plan_cache == nil then
        role_plan_cache = parse_role_plan(os.getenv("TRACE_WRK_ROLE_PLAN"))
    end

    local assigned_role = nil
    if role_plan_cache ~= nil then
        assigned_role = role_plan_cache[((setup_counter - 1) % #role_plan_cache) + 1]
    end

    -- 这里显式把线程号和角色都注入线程私有 state。
    -- 后面 init()/request() 只消费自己的这份副本，不会去碰别的线程状态。
    thread:set("thread_id", setup_counter)
    thread:set("thread_role", assigned_role)
end

function init(args)
    if args[1] ~= nil and args[1] ~= "" then
        local parsed_mode = normalize_mode(args[1])
        if parsed_mode == nil then
            error("unsupported default_mode: " .. tostring(args[1]))
        end
        default_mode = parsed_mode
    end
    spans_per_trace = parse_positive_int(args[2], spans_per_trace)
    timeout_cutoff = parse_positive_int(args[3], timeout_cutoff)
    token_payload_size = parse_positive_int(args[4], token_payload_size)
    active_pool_size = parse_positive_int(args[5], active_pool_size)

    -- thread_role 来自 setup()；如果调用方没传 TRACE_WRK_ROLE_PLAN，就统一回退到 CLI 传入的 default_mode。
    -- setup(thread) 注入的是线程私有全局变量，这里要先把它们搬进脚本自己的局部状态。
    -- 如果直接把同名变量声明成 local，会把 setup 注入的全局值遮住，这是 wrk Lua 很容易踩的坑。
    worker_thread_id = _G.thread_id or 0
    worker_thread_role = normalize_mode(_G.thread_role) or default_mode
    next_trace_key = (worker_thread_id + 1) * 1000000000000
    logical_now_ms = os.time() * 1000
    request_counter = 0
    mixed_cursor = 0

    init_active_trace_pool()
end

function request()
    request_counter = request_counter + 1
    local slot_index = ((request_counter - 1) % active_pool_size) + 1
    local span = build_span_for_slot(slot_index)
    local body = encode_span_json(span)
    local request_str = wrk.format("POST", "/logs/spans", {
        ["Content-Type"] = "application/json"
    }, body)

    -- 某个 slot 对应的 trace 一旦结束，就在原槽位原地换一条新 trace。
    -- 这样线程内总是稳定维护 active_pool_size 条活跃 trace，而不是退化回单 current trace。
    if should_roll_slot(slot_index) then
        allocate_trace_slot(slot_index)
    end

    return request_str
end

function done(summary, latency, requests)
    local reported_threads = tonumber(os.getenv("TRACE_WRK_THREADS")) or 0
    local request_count = 0
    if summary ~= nil and summary.requests ~= nil then
        request_count = summary.requests
    elseif requests ~= nil and requests.total ~= nil then
        request_count = requests.total
    end

    io.write(string.format(
        "trace_model_active_pool done: threads=%d, requests=%d, active_pool_size=%d, role_plan=%s\n",
        reported_threads,
        request_count,
        active_pool_size,
        os.getenv("TRACE_WRK_ROLE_PLAN") or default_mode
    ))
end
