# ai/proxy/providers/gemini.py
from .base import AIProvider
from typing import List, Dict, Any
from google import genai # 遵从您的指示和官方文档

class GeminiProvider(AIProvider):
    """
    使用Google GenAI Python SDK的官方推荐方式实现的AI Provider。
    核心是使用一个Client对象进行所有操作。
    """

    def __init__(self, api_key: str, model_name: str = "gemini-1.5-flash-latest"):
        """
        初始化时创建genai.Client实例。
        """
        self.client = genai.Client(api_key=api_key)
        self.model_name = model_name

    def analyze(self, log_text: str, prompt: str) -> str:
        """
        实现单次分析功能。
        使用 client.models.generate_content 方法。
        """
        full_prompt = f"{prompt}\n\nLog to analyze:\n{log_text}"
        try:
            response = self.client.models.generate_content(
                model=self.model_name,
                contents=full_prompt
            )
            return response.text
        except Exception as e:
            print(f"Error calling Gemini API for analysis: {e}")
            return f"Error: Could not get analysis from Gemini. Details: {e}"

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
