from .base import AIProvider
from typing import List, Dict, Any, Optional
from google import genai
from ..schemas import LogAnalysisResult,BatchResponseSchema,SummaryResponse, BatchAnalysisOutput
import json
import os

class GeminiProvider(AIProvider):
    """
    使用 Google GenAI Python SDK 的官方推荐方式实现的 AI 提供商。
    核心是使用一个 Client 对象进行所有操作。
    """

    def __init__(self, api_key: str = "", model_name: str = "gemini-flash-lite-latest"):
        """
        初始化：支持空 Key 启动 (懒加载/Lazy Initialization)
        """
        self.default_api_key = api_key
        self.default_model_name = model_name
        self.default_client = None # 先设为 None

        # 只有当 Key 看起来合法时，才尝试初始化 Client
        # 否则就留着 None，等第一条请求过来再初始化
        if api_key and api_key != "YOUR_API_KEY" and len(api_key) > 10:
            try:
                self.default_client = genai.Client(api_key=api_key)
            except Exception as e:
                print(f"[Gemini] 初始化警告: 提供的 Key 无效，等待后续更新。错误: {e}")
        else:
            print("[Gemini] 启动时未检测到有效的 API Key。服务就绪，等待配置传入。")

    def _get_client_and_model(self, api_key: Optional[str], model: Optional[str]):
        """
        核心逻辑：懒加载 + 动态更新
        """
        target_model = model if model else self.default_model_name

        # 1. 检查是否有新 Key 传进来（来自 C++ 的请求）
        # 这里的逻辑涵盖了：
        #   a. 启动时没 Key，现在第一次传过来了 -> 初始化
        #   b. 启动时有 Key，现在换了一个 -> 更新
        if api_key and api_key != "YOUR_API_KEY" and api_key != self.default_api_key:
            print(f"[Gemini] 检测到配置更新！正在切换 Client...")
            self.default_api_key = api_key
            # 这里可能会抛出异常（如果 Key 是错的），让外层捕获
            self.default_client = genai.Client(api_key=api_key)

        # 2. 如果此时 default_client 还是 None，说明彻底没救了
        if not self.default_client:
            raise ValueError("Gemini API Key 未配置！请前往设置页面进行配置。")

        return self.default_client, target_model

    def _build_trace_error_payload(self, error: Exception, default_status: str = "PROVIDER_ERROR") -> Dict[str, Any]:
        """
        Gemini SDK 的异常字段并不保证每次都长得一模一样，所以这里按“能拿多少拿多少”的方式做最小提取。
        既然 Trace 主链后面只需要稳定的失败协议，就不要把 SDK 原始对象继续往外透传，更不能伪装成成功 analysis。
        """
        raw_code = getattr(error, "code", None)
        error_code = None
        if isinstance(raw_code, int):
            error_code = raw_code
        elif isinstance(raw_code, str) and raw_code.isdigit():
            error_code = int(raw_code)

        error_status = getattr(error, "status", None)
        if error_status is None:
            error_status = default_status
        error_message = getattr(error, "message", None) or str(error)

        return {
            "ok": False,
            "error_code": error_code,
            "error_status": error_status,
            "error_message": error_message,
        }

    def analyze(self, log_text: str, prompt: str, api_key: Optional[str] = None, model: Optional[str] = None) -> str:
        """
        实现单次分析功能。
        使用 client.models.generate_content 方法，并配置结构化输出。
        """
        try:
            client, target_model = self._get_client_and_model(api_key, model)
        except ValueError as e:
            # 如果没有 Key，直接返回错误 JSON，而不是让服务器崩溃
            print(f"[Gemini] 分析失败: {e}")
            return '{"summary": "AI 未配置 (API Key Missing)", "risk_level": "unknown", "root_cause": "Configuration Error", "solution": "Configure API Key in Settings"}'

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

    def analyze_trace(self, trace_text: str, prompt: str, api_key: Optional[str] = None, model: Optional[str] = None) -> Dict[str, Any]:
        """
        Trace 分析：当前先复用单次 analyze 的结构化输出路径。
        但是 Trace 链路后续还要拿 usage 做 token 监控，所以这里单独补一层外层元数据返回。
        """
        try:
            client, target_model = self._get_client_and_model(api_key, model)
        except ValueError as e:
            print(f"[Gemini] Trace 分析失败: {e}")
            return self._build_trace_error_payload(e, default_status="CONFIG_ERROR")

        # Trace 路由在进入 provider 之前，就已经把 trace_text 注入到了最终 prompt 模板里。
        # 所以这里不要再像普通日志 analyze 那样把 trace_text 追加一遍，
        # 否则同一份 trace 上下文会在模型输入里重复出现两次。
        full_prompt = prompt
        try:
            response = client.models.generate_content(
                model=target_model,
                contents=full_prompt,
                config={
                    "response_mime_type": "application/json",
                    "response_schema": LogAnalysisResult
                }
            )
            # Gemini 官方 usage_metadata 字段名不稳定地偏向 prompt/candidates/total 这套，
            # 这里统一归一化成 input/output/total，避免 C++ 侧感知 provider 差异。
            usage_metadata = getattr(response, "usage_metadata", None)
            usage = None
            if usage_metadata is not None:
                usage = {
                    "input_tokens": int(getattr(usage_metadata, "prompt_token_count", 0) or 0),
                    "output_tokens": int(getattr(usage_metadata, "candidates_token_count", 0) or 0),
                    "total_tokens": int(getattr(usage_metadata, "total_token_count", 0) or 0),
                    "cached_tokens": int(getattr(usage_metadata, "cached_content_token_count", 0) or 0),
                    "thoughts_tokens": int(getattr(usage_metadata, "thoughts_token_count", 0) or 0),
                }

            return {
                "ok": True,
                "analysis": response.text,
                "usage": usage,
            }
        except Exception as e:
            print(f"Error calling Gemini API for trace analysis: {e}")
            # Trace 主链后面要做失败状态落库和熔断，所以这里必须保留“调用失败”的真相。
            # 如果继续伪造一份成功 analysis，C++ 会把真正的 provider 错误误判成一条高风险业务结论。
            return self._build_trace_error_payload(e)

    def chat(self, history: List[Dict[str, Any]], new_message: str) -> str:
        """
        实现多轮对话功能。
        使用 client.chats.create 方法。
        """
        if not self.default_client:
             return "Error: Gemini API Key is not configured."

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
        Reduce 阶段：汇总分析。
        强制返回 BatchAnalysisOutput (JSON)。
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
                    "response_schema": BatchAnalysisOutput
                }
            )
            # 这里的 response.text 是一个符合 BatchAnalysisOutput 的 JSON 字符串
            # 直接返回给 C++，让 C++ 去解析
            return response.text
        except Exception as e:
            print(f"Gemini Summarize Error: {e}")
            # 返回一个符合 JSON 结构的错误信息
            return json.dumps({
                "global_summary": f"Error generating summary: {str(e)}",
                "global_risk_level": "unknown",
                "key_patterns": []
            }, ensure_ascii=False)
