#!/bin/bash

# --- 配置 ---
# 修正路径：是 /logs 不是 /log
TARGET_URL="http://127.0.0.1:8080/logs" 
THREADS=4
DURATION="30s"
LUA_SCRIPT="post.lua" # 确保同目录下有这个文件
LOG_FILE="mvp2_backpressure_test_results.log"

# --- 清理并准备日志文件 ---
echo "Performance Test Results (MVP2 Back-pressure) - $(date)" > ${LOG_FILE}
echo "Target: ${TARGET_URL}" >> ${LOG_FILE}
echo "=======================================" >> ${LOG_FILE}

# --- Lua 脚本检查 ---
if [ ! -f "$LUA_SCRIPT" ]; then
    echo "Creating default post.lua..."
cat <<'EOF' > $LUA_SCRIPT
wrk.method = "POST"
wrk.body   = '{"msg": "performance test log entry"}'
wrk.headers["Content-Type"] = "application/json"
-- 可选：随机化 TraceID 头，模拟真实流量
request = function()
   local path = "/logs"
   local body = '{"msg": "perf test", "val": ' .. math.random(1,1000) .. '}'
   return wrk.format("POST", path, wrk.headers, body)
end
EOF
fi

# --- 测试函数 ---
run_test() {
  CONNECTIONS=$1
  echo "--- Running test with ${CONNECTIONS} connections ---"
  
  # 将结果同时输出到屏幕和日志文件
  (
    echo -e "\n\n### Connections: ${CONNECTIONS}, Threads: ${THREADS}, Duration: ${DURATION} ###"
    # 【关键】加入 --latency 参数查看 p99
    wrk -t${THREADS} -c${CONNECTIONS} -d${DURATION} --latency -s${LUA_SCRIPT} ${TARGET_URL}
  ) | tee -a ${LOG_FILE}
  
  echo "--- Pausing for 5 seconds ---"
  sleep 5
}

# --- 执行阶梯测试 ---
# 1. 轻负载 (主要看 202 成功率)
run_test 50

# 2. 中负载 (开始出现堆积)
run_test 200

# 3. 高负载 (主要测试 503 拒绝策略和系统稳定性)
run_test 1000
run_test 2000

echo "=======================================" >> ${LOG_FILE}
echo "Test completed. Check ${LOG_FILE} for details."