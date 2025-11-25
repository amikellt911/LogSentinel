import requests
import sqlite3
import time
import json
import subprocess
import os

# 配置
SERVER_URL = "http://127.0.0.1:8080"
DB_PATH = "/home/llt/Project/llt/LogSentinel/persistence/data/test.db" # 确保和 C++ 用的路径一致

def test_end_to_end_flow():
    #  # --- 调试代码开始 ---
    # # 1. 打印 Python 脚本当前的工作目录
    # print(f"DEBUG: Python CWD: {os.getcwd()}")
    
    # # 2. 打印 Python 实际上试图打开的数据库绝对路径
    # abs_db_path = os.path.abspath(DB_PATH)
    # print(f"DEBUG: Python trying to open DB at: {abs_db_path}")
    
    # # 3. 检查文件到底存不存在
    # if not os.path.exists(abs_db_path):
    #     print("DEBUG: ⚠️ 警告！数据库文件根本不存在！Python 将会创建一个新的空文件！")
    # else:
    #     print(f"DEBUG: 数据库文件存在，大小为: {os.path.getsize(abs_db_path)} bytes")
    # # --- 调试代码结束 ---
    print("1. 发送日志请求...")
    payload = {"msg": "NullPointerException in AuthenticationModule"}
    # 发送 POST 请求
    response = requests.post(f"{SERVER_URL}/logs", json=payload)
    assert response.status_code == 202
    data = response.json()
    trace_id = data["trace_id"]
    print(f"   -> 收到 trace_id: {trace_id}")

    print("2. 等待 AI 异步处理 (等待 2-3 秒)...")
    time.sleep(3) # 给 Worker 线程一点时间去跑网络请求

    print("3. 验证数据库记录...")
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    # 查询结果
    cursor.execute("SELECT status, analysis_content FROM analysis_results WHERE trace_id=?", (trace_id,))
    row = cursor.fetchone()
    
    # 断言 1: 必须有记录
    assert row is not None, "数据库中没找到该 trace_id 的记录"
    
    status, content = row
    
    # 断言 2: 状态必须是 SUCCESS
    # 如果是 FAILURE，打印错误内容方便调试
    assert status == "SUCCESS", f"任务失败，错误信息: {content}"
    
    print("   -> 数据库状态为 SUCCESS")

    # 断言 3: 验证 LLM 输出的结构 (解决不确定性问题)
    try:
        analysis_json = json.loads(content)
    except json.JSONDecodeError:
        assert False, "存储的 analysis_content 不是有效的 JSON"
        
    # 验证必要字段存在
    required_fields = ["summary", "risk_level", "root_cause", "solution"]
    for field in required_fields:
        assert field in analysis_json, f"缺少字段: {field}"
        
    # 验证 risk_level 的值域 (即使 LLM 随机，也不能瞎填)
    valid_risks = ["high", "medium", "low", "info"]
    assert analysis_json["risk_level"] in valid_risks, f"无效的风险等级: {analysis_json['risk_level']}"

    print(f"   -> LLM 分析结果结构验证通过! 风险等级: {analysis_json['risk_level']}")
    
    conn.close()
    print("\n✅ 集成测试全部通过！")

if __name__ == "__main__":
    # 确保你的 C++ Server 和 Python Proxy 都在运行
    try:
        test_end_to_end_flow()
    except AssertionError as e:
        print(f"\n❌ 测试失败: {e}")
    except Exception as e:
        print(f"\n❌ 发生异常: {e}")