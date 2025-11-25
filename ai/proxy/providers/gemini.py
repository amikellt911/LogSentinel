from .base import AIProvider
from typing import List, Dict, Any
from google import genai
from pydantic import BaseModel, Field
from enum import Enum

# 定义风险等级枚举
class RiskLevel(str, Enum):
    HIGH = "high"
    MEDIUM = "medium"
    LOW = "low"

# 定义结构化输出的 Schema
class LogAnalysisResult(BaseModel):
    summary: str = Field(description="A concise summary of the log error.")
    risk_level: RiskLevel = Field(description="The risk level of the error. Must be one of: 'high', 'medium', 'low'.")
    root_cause: str = Field(description="The root cause of the error.")
    solution: str = Field(description="Suggested solution or fix.")

class GeminiProvider(AIProvider):
    """
    使用Google GenAI Python SDK的官方推荐方式实现的AI Provider。
    核心是使用一个Client对象进行所有操作。
    """

    def __init__(self, api_key: str, model_name: str = "gemini-flash-lite-latest"):
        """
        初始化时创建genai.Client实例。
        """
        self.client = genai.Client(api_key=api_key)
        self.model_name = model_name

    def analyze(self, log_text: str, prompt: str) -> str:
        """
        实现单次分析功能。
        使用 client.models.generate_content 方法，并配置结构化输出。
        """
        # Prompt 可以简化了，因为格式由 Schema 控制
        full_prompt = f"{prompt}\n\nLog to analyze:\n{log_text}"
        
        try:
            response = self.client.models.generate_content(
                model=self.model_name,
                contents=full_prompt,
                config={
                    "response_mime_type": "application/json",
                    "response_schema": LogAnalysisResult
                }
            )
            # response.text 将是一个标准的 JSON 字符串
            return response.text
        except Exception as e:
            print(f"Error calling Gemini API for analysis: {e}")
            # 返回一个符合 JSON 结构的错误信息，以免前端解析失败
            return '{"summary": "Error calling AI", "risk_level": "high", "root_cause": "API Error", "solution": "Check logs"}'

    def chat(self, history: List[Dict[str, Any]], new_message: str) -> str:
        """
        实现多轮对话功能。
        使用 client.chats.create 方法。
        """
        try:
            # 使用client.chats.create()并传入历史记录来启动或恢复一个对话
            chat_session = self.client.chats.create(
                model=self.model_name,
                history=history
            )
            
            # 发送新消息
            response = chat_session.send_message(new_message)
            return response.text
        except Exception as e:
            print(f"Error calling Gemini API in chat mode: {e}")
            return f"Error: Could not get chat response from Gemini. Details: {e}"
