#!/bin/bash

# --- 配置 ---
TARGET_URL="http://127.0.0.1:8080/log"
THREADS=4
DURATION="30s"
LUA_SCRIPT="post.lua"
LOG_FILE="base_performance_results_without_log_has_timeout_60s.log"

# --- 清理并准备日志文件 ---
echo "Performance Test Results - $(date)" > ${LOG_FILE}
echo "=======================================" >> ${LOG_FILE}

# --- Lua 脚本 (用于 POST 请求) ---
# 如果脚本不存在，就创建它
if [ ! -f "$LUA_SCRIPT" ]; then
cat <<'EOF' > $LUA_SCRIPT
wrk.method = "POST"
wrk.body   = '{"message": "performance test"}'
wrk.headers["Content-Type"] = "application/json"
EOF
fi

# --- 测试函数 ---（长连接）
run_test() {
  CONNECTIONS=$1
  echo "--- Running test with ${CONNECTIONS} connections ---"
  
  # 将结果同时输出到屏幕和日志文件
  (
    echo -e "\n\n### Test with Connections: ${CONNECTIONS}, Threads: ${THREADS}, Duration: ${DURATION} ###"
    # 运行 wrk 命令
    wrk -t${THREADS} -c${CONNECTIONS} -d${DURATION} -s${LUA_SCRIPT} ${TARGET_URL}
  ) | tee -a ${LOG_FILE}
  
  # 在两次测试之间暂停一下，让服务器资源恢复
  echo "--- Pausing for 5 seconds ---"
  sleep 5
}

# --- 执行测试 ---
# 阶段一：基准测试
run_test 200

# 阶段二：压力测试 (递增)
run_test 400
run_test 600
run_test 800
run_test 1000
run_test 1500
run_test 2000
# ... 你可以继续增加

echo "=======================================" >> ${LOG_FILE}
echo "All performance tests completed. Results are in ${LOG_FILE}"