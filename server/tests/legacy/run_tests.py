"""
DEPRECATED（软下线中）：
- 原因：该脚本依赖旧测试编排思路，和当前 `smoke_trace_spans.py` + CI workflow 存在职责重叠，
  且维护状态不稳定，继续作为默认入口会误导测试执行方式。
- 当前状态：保留文件仅用于历史参考，不纳入默认 CI，也不建议作为日常回归入口。
- 替代方案：
  1) 基础/增强冒烟：`python server/tests/smoke_trace_spans.py --mode basic|advanced`
  2) C++ 单元测试：`cd server/build && ctest -R test_trace_session_manager_unit --output-on-failure`
"""

# tests/runner.py (新建文件)
import subprocess
import time
import os
import requests
import sqlite3
import uuid
import signal
import sys

# 假设编译好的可执行文件在 build 目录
SERVER_BIN = "./build/LogSentinel"
TEST_PORT = 8081 # 使用非标准端口避免冲突

def run_integration_tests():
    # 1. 准备环境
    unique_id = str(uuid.uuid4())[:8]
    db_file = f"test_env_{unique_id}.db"
    
    # 确保路径是绝对路径，防止 C++ 程序在错误的目录创建 DB
    abs_db_path = os.path.abspath(db_file)
    print(f"🛠️  [Setup] 创建临时测试环境: {db_file}")

    #(可选) 预注入 Webhook 配置
    conn = sqlite3.connect(abs_db_path)
    cursor = conn.cursor()
    create_table_sql = """
    CREATE TABLE IF NOT EXISTS webhook_configs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        url TEXT NOT NULL UNIQUE,
        is_active INTEGER DEFAULT 1,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    );
    """
    cursor.execute(create_table_sql) 
    
    # 插入测试数据
    try:
        cursor.execute("INSERT INTO webhook_configs (url) VALUES ('http://localhost:9999/webhook')")
        conn.commit()
    except sqlite3.IntegrityError:
        pass # 防止重复插入报错
        
    conn.close()

    # 2. 启动服务器
    print(f"🚀 [Setup] 启动 LogSentinel (Port: {TEST_PORT})...")
    server_process = subprocess.Popen(
        [SERVER_BIN, "--db", "./" + db_file, "--port", str(TEST_PORT)],
        stdout=sys.stdout, 
        stderr=sys.stderr
    )

    # 等待服务器就绪 (简单的 sleep，或者轮询健康检查接口)
    time.sleep(2)

    # 检查进程是否意外挂掉
    if server_process.poll() is not None:
        print("❌ [Error] 服务器启动失败！")
        print(server_process.stderr.read().decode())
        cleanup(db_file, server_process)
        sys.exit(1)

    try:
        # 3. 运行测试逻辑
        # 这里可以 import 之前的 test_history_api 模块来跑
        # 为了演示，这里直接调
        print("🧪 [Test] 运行测试用例...")
        
        # 示例：调用我们刚才写的测试脚本 (需要稍微改造一下 test_history_api 支持传 URL)
        import test_history_api
        test_history_api.SERVER_URL = f"http://127.0.0.1:{TEST_PORT}"
        
        # 跑灌数据
        if test_history_api.seed_test_data(25):
            test_history_api.test_pagination()
            print("✅ [Test] 所有集成测试通过！")
        else:
            print("❌ [Test] 数据生成失败")

    except Exception as e:
        print(f"❌ [Test] 测试过程中发生异常: {e}")
    finally:
        # 4. 清理环境 (Teardown)
        cleanup(db_file, server_process)

def cleanup(db_file, process):
    print("🧹 [Teardown] 清理资源...")
    if process:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
        print("   -> 服务器进程已停止")
    
    if os.path.exists(db_file):
        os.remove(db_file)
        print(f"   -> 删除临时数据库: {db_file}")
    
    # 清理 WAL 文件
    if os.path.exists(db_file + "-wal"): os.remove(db_file + "-wal")
    if os.path.exists(db_file + "-shm"): os.remove(db_file + "-shm")

if __name__ == "__main__":
    run_integration_tests()
