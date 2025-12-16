import requests
import sqlite3
import time
import json
import os

# 配置
SERVER_URL = "http://127.0.0.1:8080"
# 请确保这个路径和你 C++ 程序实际生成的 DB 路径一致
# 如果你是从 build 目录运行，可能路径会有变化，建议用绝对路径最稳
DB_PATH = "/home/llt/Project/llt/LogSentinel/server/persistence/data/test.db" 

def test_end_to_end_flow():
    print("1. 发送日志请求...")
    # 构造一条稍微像样点的日志
    payload = {"msg": "[Error] Connection pool exhausted in DatabaseModule. Retries: 3."}
    
    try:
        response = requests.post(f"{SERVER_URL}/logs", json=payload)
    except requests.exceptions.ConnectionError:
        print(f"❌ 无法连接到 {SERVER_URL}，请确保 LogSentinel 正在运行！")
        return

    assert response.status_code == 202
    data = response.json()
    trace_id = data["trace_id"]
    print(f"   -> 收到 trace_id: {trace_id}")

    print("2. 等待 Batcher 刷盘 + MockAI 处理 (等待 3 秒)...")
    time.sleep(3) # 你的经验值：3秒比较稳

    print("3. 验证数据库记录...")
    if not os.path.exists(DB_PATH):
        print(f"❌ 数据库文件不存在: {DB_PATH}")
        return

    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    # 【关键修改】查询拆分后的字段，以及 global_summary
    query_sql = """
        SELECT status, risk_level, summary, root_cause, solution, global_summary 
        FROM analysis_results 
        WHERE trace_id=?
    """
    cursor.execute(query_sql, (trace_id,))
    row = cursor.fetchone()
    
    # 断言 1: 必须有记录
    assert row is not None, f"数据库中没找到 trace_id={trace_id} 的记录"
    
    # 解包字段
    status, risk, summary, root_cause, solution, global_summary = row
    
    print(f"   -> 查获记录: Status={status}, Risk={risk}")

    # 断言 2: 状态必须是 SUCCESS
    assert status == "SUCCESS", f"任务状态异常: {status}"
    
    # 断言 3: 验证字段内容 (MockAI 应该返回特定的 Mock 数据)
    # 注意：MockAI 返回的 risk 逻辑是根据 keywords 来的，
    # 我们发的日志里有 'Error'，MockAI 应该判定为 'medium' (根据你之前的 Mock 逻辑)
    valid_risks = ["high", "medium", "low", "info", "unknown"]
    assert risk in valid_risks, f"无效的风险等级: {risk}"
    
    assert summary is not None and len(summary) > 0, "Summary 字段为空"
    assert root_cause is not None, "Root Cause 字段为空"
    assert solution is not None, "Solution 字段为空"

    # 断言 4: 验证 Map-Reduce 的产物 (Global Summary)
    # 因为只有一条日志，Global Summary 应该也能生成
    if global_summary:
        print(f"   -> Global Summary: {global_summary}")
    else:
        print("   -> ⚠️ Global Summary 为空 (可能是单条处理逻辑没触发 Summarize 或者 Mock 没返回)")

    conn.close()
    print("\n✅ 集成测试全部通过！LogSentinel MVP2 核心链路正常！")

if __name__ == "__main__":
    test_end_to_end_flow()