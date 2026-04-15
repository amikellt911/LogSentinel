wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"

-- 每次请求调用，生成稍微不同的数据
request = function()
   -- 模拟一个随机的 Trace ID (可选，如果服务端会自动生成则不需要)
   local trace_id = "trace-" .. math.random(100000, 999999)
   wrk.headers["X-Trace-ID"] = trace_id
   
   -- 构造 JSON Body
   local body = '{"msg": "Performance test log entry", "random_val": ' .. math.random(1, 100) .. '}'
   return wrk.format(nil, nil, nil, body)
end