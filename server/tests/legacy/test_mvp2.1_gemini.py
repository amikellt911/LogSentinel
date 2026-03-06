"""
DEPRECATED（软下线中）：
- 原因：该脚本主要用于历史 MVP2.1 手工发送示例，断言覆盖不足（只发请求、不校验完整链路结果），
  已不适合作为当前质量门禁。
- 当前状态：保留为历史示例，不纳入默认 CI。
- 替代方案：
  1) 端到端冒烟：`python server/tests/smoke_trace_spans.py --mode advanced`
  2) 单元测试门禁：`ctest -R "^TraceSessionManagerUnitTest\." --output-on-failure`
"""

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
