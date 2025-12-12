import sqlite3
import os

# 1. 配置数据库路径
# 假设你在项目根目录下运行，如果不是，请调整这个路径
DB_PATH = "persistence/data/test.db"

def inspect_db():
    if not os.path.exists(DB_PATH):
        print(f"❌ 错误: 找不到数据库文件: {DB_PATH}")
        return

    print(f"🔍 连接数据库: {DB_PATH}")
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()

    try:
        # ---------------------------------------------------------
        # 任务 1: 查看最近的 4 条分析结果 (按 ID 倒序)
        # ---------------------------------------------------------
        print("\n--- [1] 最近的 4 条分析记录 (Latest 4) ---")
        cursor.execute("""
            SELECT id, trace_id, risk_level, response_time_ms, processed_at 
            FROM analysis_results 
            ORDER BY id DESC 
            LIMIT 4
        """)
        rows = cursor.fetchall()
        
        print(f"{'ID':<6} | {'Trace ID':<20} | {'Risk':<8} | {'Time(val)':<10} | {'Processed At'}")
        print("-" * 80)
        for row in rows:
            # Time(val) 是数据库里存的原始数值
            print(f"{row[0]:<6} | {row[1][:18]}.. | {row[2]:<8} | {row[3]:<10} | {row[4]}")

        # ---------------------------------------------------------
        # 任务 2: 计算所有记录的平均耗时 (复现 Dashboard 的逻辑)
        # ---------------------------------------------------------
        print("\n--- [2] 统计数据验证 (Statistics) ---")
        cursor.execute("SELECT AVG(response_time_ms), COUNT(*) FROM analysis_results")
        avg_time, count = cursor.fetchone()
        
        print(f"📊 总记录数: {count}")
        print(f"🐢 数据库平均耗时 (AVG): {avg_time:.2f} (单位未知)")
        
        # 引导思考：如果这个值是微秒，换算成毫秒是多少？
        if avg_time:
            print(f"💡 如果假设它是微秒 (us): {avg_time / 1000:.2f} ms")

    except Exception as e:
        print(f"💥 查询出错: {e}")
    finally:
        conn.close()

if __name__ == "__main__":
    inspect_db()