import requests
import time
import json
import sys

# 默认端口 8081 (配合 run_tests.py)，也可以通过环境变量覆盖
SERVER_URL = "http://127.0.0.1:8081"

def seed_test_data(count=25):
    """向服务器灌入 count 条测试数据"""
    print(f"🌱 正在生成 {count} 条测试日志...", end="", flush=True)
    
    success_count = 0
    for i in range(count):
        payload = {"msg": f"Test Log Entry #{i+1} for Pagination Check"}
        try:
            # 【修正1】去掉 timeout，防止服务器繁忙时客户端主动断开
            resp = requests.post(f"{SERVER_URL}/logs", json=payload)
            if resp.status_code == 202:
                success_count += 1
        except Exception as e:
            print(f"\n❌ 请求异常: {e}")
            return False
            
    print(f" 完成！(成功发送: {success_count}/{count})")
    
    # 【修正2】增加到 5秒，确保 LogBatcher 有足够时间触发 flush 并完成 SQLite 写入
    print("⏳ 等待后台异步处理 & 落库 (5s)...")
    time.sleep(5) 
    return True

def test_pagination():
    print("\n🔍 开始测试分页 API (GET /history)...")

    # --- Case 1: 基础分页 ---
    print("   [Case 1] 请求第 1 页 (Size=10)...", end="")
    try:
        resp = requests.get(f"{SERVER_URL}/history?page=1&pageSize=10")
        assert resp.status_code == 200, f"状态码错误: {resp.status_code}"
        
        data = resp.json()
        logs = data.get("logs", [])
        total = data.get("total_count", 0)

        # 强校验
        assert len(logs) == 10, f"当前页条数错误: 期望 10, 实际 {len(logs)}"
        
        # 【修正3】更友好的错误提示
        if total < 25:
            print(f"\n      ❌ 致命错误: 数据库里只有 {total} 条数据，预期至少 25 条。")
            print("      原因可能是：1. 写入被丢弃(背压) 2. 写入还没完成(太慢) 3. 连错了数据库")
            sys.exit(1)
            
        assert total >= 25
        print("✅ 通过")

    except Exception as e:
        print(f"\n❌ Case 1 失败: {e}")
        sys.exit(1)

    # --- Case 2: 翻页 ---
    print("   [Case 2] 请求第 3 页 (Size=10)...", end="")
    resp = requests.get(f"{SERVER_URL}/history?page=3&pageSize=10")
    assert resp.status_code == 200
    # 第3页应该有数据 (25条数据，每页10条 -> 第3页有5条)
    assert len(resp.json()["logs"]) > 0 
    print("✅ 通过")

    # --- Case 3: 越界 ---
    print("   [Case 3] 请求越界页码 (Page=999)...", end="")
    resp = requests.get(f"{SERVER_URL}/history?page=999&pageSize=10")
    assert resp.status_code == 200
    assert len(resp.json()["logs"]) == 0
    print("✅ 通过")

    print("\n🎉 集成测试全部通过！HistoryHandler 稳了！")

if __name__ == "__main__":
    # 简单的健康检查
    try:
        requests.get(f"{SERVER_URL}/dashboard")
    except:
        print(f"❌ 无法连接到 {SERVER_URL}，请检查 run_tests.py 是否正确启动了服务器。")
        exit(1)

    if seed_test_data(25):
        test_pagination()