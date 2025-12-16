import requests
import json

# 假设你的 C++ 服务器开在本地 8080 端口
SERVER_URL = "http://127.0.0.1:8080"

def run_test():
    print("🚀 开始发送日志请求...")

    # 你的测试数据（这就就是刚才那个精彩的“缓存雪崩”剧本）
    test_payload = {
        "batch": [
            {"id": "trace-001", "text": "[WARN] [CacheService] Redis connection pool is empty. Retrying connection..."},
            {"id": "trace-002", "text": "[INFO] [UserService] Cache miss for key 'user_profile'. Fetching from DB."},
            {"id": "trace-003", "text": "[WARN] [DB_Monitor] High CPU usage detected on DB-Primary (92%)."},
            {"id": "trace-004", "text": "[ERROR] [API_Gateway] 503 Service Unavailable. Request timed out."}
        ]
    }

    batch = test_payload.get("batch", [])

    try:
        # 模拟逐条发送日志
        for i, payload in enumerate(batch):
            # 直接传字典给 json 参数，它会自动处理序列化
            response = requests.post(f"{SERVER_URL}/logs", json=payload)
            
            # 检查一下状态码，202 代表服务器已接收（Accepted）
            if response.status_code == 202:
                print(f"✅ Log {i+1} 发送成功: {payload['text'][:30]}...")
            else:
                print(f"❌ Log {i+1} 发送失败: {response.status_code} - {response.text}")

    except Exception as e:
        print(f"💥 发生错误: {e}")

if __name__ == "__main__":
    run_test()