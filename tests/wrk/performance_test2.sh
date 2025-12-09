#!/bin/bash

# --- 配置 ---
SERVER_BIN="../../build/LogSentinel" # 假设你在 build 目录下运行
TARGET_URL="http://127.0.0.1:8080/logs"
THREADS=4
DURATION="15s" # 缩短一点，因为队列满了之后测太久也没意义
LUA_SCRIPT="post.lua"
LOG_FILE="mvp2_strict_performance.log"

# --- 辅助函数：启动服务器 ---
start_server() {
    echo "Starting LogSentinel..."
    # 后台启动服务器，重定向日志防止干扰
    $SERVER_BIN > server_output.log 2>&1 &
    SERVER_PID=$!
    
    # 等待端口就绪 (简单 sleep，或者用 nc 探测)
    echo "Waiting for server to initialize (PID: $SERVER_PID)..."
    sleep 2 
}

# --- 辅助函数：关闭服务器 ---
stop_server() {
    echo "Stopping LogSentinel (PID: $SERVER_PID)..."
    kill $SERVER_PID
    wait $SERVER_PID 2>/dev/null
    echo "Server stopped."
}

# --- 准备工作 ---
echo "Strict Performance Test - $(date)" > ${LOG_FILE}
echo "Target: ${TARGET_URL}" >> ${LOG_FILE}

if [ ! -f "$LUA_SCRIPT" ]; then
cat <<'EOF' > $LUA_SCRIPT
wrk.method = "POST"
wrk.body   = '{"msg": "perf test"}'
wrk.headers["Content-Type"] = "application/json"
EOF
fi

# --- 测试函数 ---
run_test() {
  CONNECTIONS=$1
  
  # 1. 启动干净的服务器环境
  start_server
  
  # 2. 运行测试
  echo -e "\n\n### Fresh Run: Connections: ${CONNECTIONS} ###" | tee -a ${LOG_FILE}
  wrk -t${THREADS} -c${CONNECTIONS} -d${DURATION} --latency -s${LUA_SCRIPT} ${TARGET_URL} >> ${LOG_FILE}
  
  # 3. 杀死服务器 (清除内存队列，模拟下次全新启动)
  stop_server
  
  # 4. 冷却一下 (释放 TCP 端口)
  sleep 2
}

# --- 执行 ---
# 确保没有残留进程
pkill LogSentinel || true

echo "Starting Tests..."

# 从小到大测，但每次都是全新的世界
run_test 50
run_test 200
run_test 1000
run_test 2000

echo "Done. Results in ${LOG_FILE}"