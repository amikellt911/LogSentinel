# ai/proxy/providers/mock.py

from .base import AIProvider
from typing import List, Dict, Any
import json
import time
import random

class MockProvider(AIProvider):
    """
    用于测试和开发的 Mock Provider。
    它不调用任何外部 API，而是返回预定义的结构化数据。
    可以通过 delay 参数模拟耗时，用于测试系统的背压机制。
    """

    def __init__(self, api_key: str = "mock-key", delay: float = 0.5):
        """
        初始化 Mock Provider。
        :param delay: 模拟分析的耗时（秒）。
                      设置为 0.5s 或 1s 可以让 Worker 线程处理变慢，
                      从而在压测时更容易触发 503 背压。
        """
        self.delay = delay

    def analyze(self, log_text: str, prompt: str) -> str:
        """
        模拟单次分析。
        """
        # 1. 模拟耗时 (关键：这是测试背压的核心)
        # 稍微加一点随机性，模拟真实网络波动
        actual_delay = self.delay + random.uniform(0, 0.1)
        time.sleep(actual_delay)

        # 2. 根据日志内容简单的伪造逻辑，方便前端展示效果
        log_lower = log_text.lower()
        
        risk = "low"
        summary = "Routine log entry detected."
        
        if "critical" in log_lower or "fatal" in log_lower:
            risk = "high"
            summary = "Critical failure detected in system components."
        elif "error" in log_lower or "fail" in log_lower or "exception" in log_lower:
            risk = "medium"
            summary = "Standard error detected during operation."

        # 3. 构造符合 JSON Schema 的返回结果
        # 必须严格匹配 C++ 端要求的字段: summary, risk_level, root_cause, solution
        result = {
            "summary": summary,
            "risk_level": risk,
            "root_cause": f"Mocked root cause analysis for: {log_text[:30]}...",
            "solution": "This is a mock solution. 1. Check logs. 2. Restart service. 3. Drink coffee."
        }

        return json.dumps(result)

    def chat(self, history: List[Dict[str, Any]], new_message: str) -> str:
        """
        模拟多轮对话。
        """
        time.sleep(self.delay) # 同样模拟延迟
        return f"[Mock AI]: I received your message: '{new_message}'. This is a simulated response."