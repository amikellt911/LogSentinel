
import unittest
import requests
import sqlite3
import subprocess
import os
import time
import json

class TestServerIntegration(unittest.TestCase):
    SERVER_EXECUTABLE = "build/testServerPoolSqliteMockAI"
    DB_FILE = "../persistence/data/test.db"
    SERVER_URL = "http://localhost:8080"
    server_process = None

    @classmethod
    def setUpClass(cls):
        # 清理之前的数据库文件
        if os.path.exists(cls.DB_FILE):
            os.remove(cls.DB_FILE)

        # 检查服务器可执行文件是否存在
        if not os.path.exists(cls.SERVER_EXECUTABLE):
            raise FileNotFoundError(f"服务器可执行文件未找到: {cls.SERVER_EXECUTABLE}。请先构建项目。")

        # 启动服务器
        print(f"正在启动服务器: {cls.SERVER_EXECUTABLE}")
        # 可执行文件会在相对路径创建数据库，因此我们从项目根目录运行它
        cls.server_process = subprocess.Popen([os.path.abspath(cls.SERVER_EXECUTABLE)])
        
        # 等待服务器启动
        time.sleep(2)
        print("服务器已启动。")

    @classmethod
    def tearDownClass(cls):
        # 停止服务器
        if cls.server_process:
            print("正在停止服务器...")
            cls.server_process.terminate()
            cls.server_process.wait()
            print("服务器已停止。")

        # 可选：清理数据库文件
        if os.path.exists(cls.DB_FILE):
            print(f"数据库文件 '{cls.DB_FILE}' 已保留，供检查。")
            # os.remove(cls.DB_FILE)

    def test_01_full_workflow_normal_log(self):
        """
        测试标准日志的工作流程：POST、GET 和数据库验证。
        """
        log_content = "这是一条标准日志条目。"
        print("\n--- 正在测试标准日志工作流程 ---")
        
        # 1. POST 一条标准日志
        post_response = requests.post(f"{self.SERVER_URL}/logs", data=log_content, headers={"Content-Type": "text/plain"})
        self.assertEqual(post_response.status_code, 202)
        
        response_json = post_response.json()
        self.assertIn("trace_id", response_json)
        trace_id = response_json["trace_id"]
        print(f"POST 成功，trace_id: {trace_id}")

        # 等待异步处理（AI 分析 + 数据库写入）
        time.sleep(1)

        # 2. GET 结果
        print(f"正在获取 trace_id 为 {trace_id} 的结果")
        get_response = requests.get(f"{self.SERVER_URL}/results/{trace_id}")
        self.assertEqual(get_response.status_code, 200)
        
        get_json = get_response.json()
        expected_info_result = {"severity": "info", "suggestion": "No immediate action required."}
        self.assertEqual(get_json["trace_id"], trace_id)
        # 服务器返回的结果是一个 JSON 对象，而不是字符串
        self.assertEqual(get_json["result"], expected_info_result)
        print("GET 成功，结果匹配。")

        # 3. 在数据库中验证
        print("正在数据库中验证数据...")
        conn = sqlite3.connect(self.DB_FILE)
        cursor = conn.cursor()
        
        # 检查 raw_logs 表
        cursor.execute("SELECT log_content FROM raw_logs WHERE trace_id=?", (trace_id,))
        row_raw = cursor.fetchone()
        self.assertIsNotNone(row_raw, "在 raw_logs 表中未找到 trace ID。")
        self.assertEqual(row_raw[0], log_content)

        # 检查 analysis_results 表
        cursor.execute("SELECT analysis_content, status FROM analysis_results WHERE trace_id=?", (trace_id,))
        row_analysis = cursor.fetchone()
        conn.close()

        self.assertIsNotNone(row_analysis, "在 analysis_results 表中未找到 trace ID。")
        db_analysis_result, db_status = row_analysis
        self.assertEqual(json.loads(db_analysis_result), expected_info_result)
        self.assertEqual(db_status, "SUCCESS")
        print("数据库验证成功。")

    def test_02_full_workflow_error_log(self):
        """
        测试包含“error”关键字的日志的工作流程。
        """
        error_log_content = "这是一条error条目。"
        print("\n--- 正在测试错误日志工作流程 ---")
        
        # 1. POST 一条包含“error”关键字的日志
        post_response = requests.post(f"{self.SERVER_URL}/logs", data=error_log_content, headers={"Content-Type": "text/plain"})
        self.assertEqual(post_response.status_code, 202)
        trace_id = post_response.json()["trace_id"]
        print(f"POST 错误日志成功，trace_id: {trace_id}")

        # 等待异步处理
        time.sleep(1)

        # 2. GET 结果
        get_response = requests.get(f"{self.SERVER_URL}/results/{trace_id}")
        self.assertEqual(get_response.status_code, 200)
        get_json = get_response.json()
        expected_error_result = {"severity": "high", "suggestion": "Check application logs for stack trace."}
        self.assertEqual(get_json["trace_id"], trace_id)
        self.assertEqual(get_json["result"], expected_error_result)
        print("GET 错误日志成功，结果匹配。")

    def test_03_non_existent_trace_id(self):
        """
        测试获取不存在的 trace_id 的结果。
        """
        print("\n--- 正在测试不存在的 trace_id ---")
        non_existent_id = "this-id-does-not-exist-12345"
        get_response = requests.get(f"{self.SERVER_URL}/results/{non_existent_id}")
        self.assertEqual(get_response.status_code, 404)
        self.assertEqual(get_response.json(), {"error": "Trace ID not found"})
        print("获取不存在的 trace_id 返回 404，符合预期。")

if __name__ == "__main__":
    unittest.main(verbosity=2)
