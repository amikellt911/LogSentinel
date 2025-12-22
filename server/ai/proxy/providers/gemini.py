from .base import AIProvider
from typing import List, Dict, Any, Optional
from google import genai
from ..schemas import LogAnalysisResult,BatchResponseSchema,SummaryResponse
import json
import os

class GeminiProvider(AIProvider):
    """
    使用 Google GenAI Python SDK 的官方推荐方式实现的 AI 提供商。
    核心是使用一个 Client 对象进行所有操作。
    """

    def __init__(self, api_key: str, model_name: str = "gemini-flash-lite-latest"):
        """
        初始化时创建 genai.Client 实例。
        """
        self.default_api_key = api_key
        self.default_client = genai.Client(api_key=api_key)
        self.default_model_name = model_name

    def _get_client_and_model(self, api_key: Optional[str], model: Optional[str]):
        """
        辅助方法：确定使用哪个客户端和模型。
        如果提供了新的 API Key，且与当前持有的 Key 不同，则更新默认客户端。
        否则使用持久化的默认客户端。
        """
        target_model = model if model else self.default_model_name

        if api_key and api_key != self.default_api_key:
            # 检测到 API Key 变更，更新持久化的默认客户端
            print(f"检测到 API Key 变更，正在更新 Gemini 客户端...")
            self.default_api_key = api_key
            self.default_client = genai.Client(api_key=api_key)

        return self.default_client, target_model

    def analyze(self, log_text: str, prompt: str, api_key: Optional[str] = None, model: Optional[str] = None) -> str:
        """
        实现单次分析功能。
        使用 client.models.generate_content 方法，并配置结构化输出。
        """
        client, target_model = self._get_client_and_model(api_key, model)

        # Prompt 可以简化了，因为格式由 Schema 控制
        full_prompt = f"{prompt}\n\nLog to analyze:\n{log_text}"
        
        try:
            response = client.models.generate_content(
                model=target_model,
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
            return '{"summary": "Error calling AI", "risk_level": "critical", "root_cause": "API Error", "solution": "Check logs"}'

    def chat(self, history: List[Dict[str, Any]], new_message: str) -> str:
        """
        实现多轮对话功能。
        使用 client.chats.create 方法。
        """
        # 聊天通常依赖于持久会话，因此在不重新创建会话的情况下动态配置比较棘手。
        # 目前暂时使用默认的客户端/模型。
        try:
            # 使用 client.chats.create() 并传入历史记录来启动或恢复一个对话
            chat_session = self.default_client.chats.create(
                model=self.default_model_name,
                history=history
            )
            
            # 发送新消息
            response = chat_session.send_message(new_message)
            return response.text
        except Exception as e:
            print(f"Error calling Gemini API in chat mode: {e}")
            return f"Error: Could not get chat response from Gemini. Details: {e}"

    def analyze_batch(self, batch_logs: List[Dict[str, str]], prompt: str, api_key: Optional[str] = None, model: Optional[str] = None) -> List[Dict[str, Any]]:
        """
        Map 阶段：批量分析
        """
        client, target_model = self._get_client_and_model(api_key, model)

        count = len(batch_logs)
        data_json = json.dumps(batch_logs, indent=2, ensure_ascii=False)
        full_prompt = (f"{prompt}\n\n"
                     f"---Context Info---\n"
                     f"Total logs to analyze: {count}\n\n"
                     f"---Log Data(JSON)---\n"
                     f"{data_json}")
        try:
            response = client.models.generate_content(
                model=target_model,
                contents=full_prompt,
                config={
                    "response_mime_type": "application/json",
                    "response_schema": BatchResponseSchema
                }
            )
            resp_data = json.loads(response.text)
            return resp_data.get("results", [])
        except Exception as e:
            print(f"Gemini Batch Error:{e}")
            raise e
    
    def summarize(self, summary_logs: List[Dict[str, Any]], prompt: str, api_key: Optional[str] = None, model: Optional[str] = None) -> str:
        """
        Reduce 阶段：汇总分析
        """
        client, target_model = self._get_client_and_model(api_key, model)

        count = len(summary_logs)
        simplified_logs = []
        for item in summary_logs:
            simplified_logs.append({
                "risk": item.get("risk_level"),
                "summary": item.get("summary"),
                "root_cause": item.get("root_cause")
            })
        
        data_json = json.dumps(simplified_logs, indent=2, ensure_ascii=False)
        full_prompt = (f"{prompt}\n\n"
                     f"---Context Info---\n"
                     f"Total Reports: {count}\n\n"
                     f"---Analysis Reports(JSON)---\n"
                     f"{data_json}")
        try:
            response = client.models.generate_content(
                model=target_model,
                contents=full_prompt,
                config={
                    "response_mime_type": "application/json",
                    "response_schema": SummaryResponse
                }
            )
            resp_data = json.loads(response.text)
            return resp_data.get("summary", "No summary generated.")
        except Exception as e:
            print(f"Gemini Summarize Error: {e}")
            return f"Error generating summary: {str(e)}"
