from .base import AIProvider
from typing import List, Dict, Any
from google import genai
from ..schemas import LogAnalysisResult,BatchResponseSchema,SummaryResponse
import json
import os
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

    def analyze_batch(self, batch_logs: List[Dict[str, str]], prompt: str) -> List[Dict[str, Any]]:
        """
        Mvp阶段2.1：批量分析
        """
        count = len(batch_logs)
        data_json=json.dumps(batch_logs,indent=2,ensure_ascii=False)
        full_prompt=(f"{prompt}\n\n"
                     f"---Context Info---\n"
                     f"Total logs to analyze: {count}\n\n"
                     f"---Log Data(JSON)---\n"
                     f"{data_json}")
        try:
            response=self.client.models.generate_content(
                model=self.model_name,
                contents=full_prompt,
                config={
                    "response_mime_type": "application/json",
                    "response_schema": BatchResponseSchema
                }
            )
            resp_data=json.loads(response.text)
            return resp_data.get("results",[])
        except Exception as e:
            print(f"Gemini Batch Error:{e}")
            raise e
    
    def summarize(self, summary_logs: List[Dict[str, Any]], prompt: str) -> str:
        """
        Mvp 2.1 Reduce 阶段。
        """
        count=len(summary_logs)
        simplified_logs=[]
        for item in summary_logs:
            simplified_logs.append({
                "risk":item.get("risk_level")
                ,"summary":item.get("summary")
                ,"root_cause":item.get("root_cause")
            })
        
        data_json=json.dumps(simplified_logs,indent=2,ensure_ascii=False)
        full_prompt=(f"{prompt}\n\n"
                     f"---Context Info---\n"
                     f"Total Reports: {count}\n\n"
                     f"---Analysis Reports(JSON)---\n"
                     f"{data_json}")
        try:
            response=self.client.models.generate_content(
                model=self.model_name,
                contents=full_prompt,
                config={
                    "response_mime_type": "application/json",
                    "response_schema": SummaryResponse
                }
            )
            resp_data=json.loads(response.text)
            return resp_data.get("summary","No summary generated.")
        except Exception as e:
            print(f"Gemini Summarize Error: {e}")
            return f"Error generating summary: {str(e)}"