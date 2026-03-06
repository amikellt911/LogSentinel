#!/bin/bash

# --- 配置 ---
# 目标 URL，指向新的 /logs 端点
TARGET_URL="http://127.0.0.1:8080/logs"
# 使用的线程数
THREADS=4
# 测试持续时间
DURATION="30s"
# 使用新的 Lua 脚本
LUA_SCRIPT="mvp1_post.lua"
# 新的日志文件名
LOG_FILE="mvp1_performance_results.log"

# --- 清理并准备日志文件 ---
echo "MVP1 Performance Test Results - $(date)" > ${LOG_FILE}
echo "=======================================" >> ${LOG_FILE}
echo "Target URL: ${TARGET_URL}" >> ${LOG_FILE}
echo "" >> ${LOG_FILE}


# --- 测试函数 ---
run_test() {
  CONNECTIONS=$1
  echo "--- Running test with ${CONNECTIONS} connections, ${THREADS} threads for ${DURATION} ---" 
  
  # 将结果同时输出到屏幕和日志文件
  (
    echo -e "\n\n### Test with Connections: ${CONNECTIONS} ###"
    # 运行 wrk 命令
    # -t: 线程数
    # -c: 连接数
    # -d: 持续时间
    # -s: 使用的 Lua 脚本
    wrk -t${THREADS} -c${CONNECTIONS} -d${DURATION} -s${LUA_SCRIPT} ${TARGET_URL}
  ) | tee -a ${LOG_FILE}
  
  # 在两次测试之间暂停一下，让服务器资源恢复
  echo "--- Pausing for 5 seconds to let the server recover ---"
  sleep 5
}

# --- 检查 wrk 是否安装 ---
if ! command -v wrk &> /dev/null
then
    echo "Error: wrk is not installed. Please install it to run this performance test."
    echo "On Debian/Ubuntu: sudo apt-get install wrk"
    echo "On macOS: brew install wrk"
    exit 1
fi


# --- 执行测试 ---
# 阶段一：基准测试
run_test 200

# 阶段二：压力测试 (递增连接数)
run_test 400
run_test 600
run_test 800

echo "=======================================" >> ${LOG_FILE}
echo "All performance tests completed. Results are in ${LOG_FILE}"
