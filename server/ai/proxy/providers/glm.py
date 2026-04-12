# ai/proxy/providers/glm.py

from .base import AIProvider
from ..schemas import LogAnalysisResult
from typing import List, Dict, Any, Optional
from pydantic import ValidationError
import httpx
import json


class GlmProvider(AIProvider):
    """
    智谱 GLM Provider。
    当前这一版只先把 Trace 主链打通，所以真正实现的是 analyze_trace；
    其余旧接口先明确报未实现，避免路由层把“还没接”的能力误当成可用。
    """

    def __init__(self,
                 api_key: str = "",
                 model_name: str = "glm-5.1",
                 base_url: str = "https://open.bigmodel.cn/api/paas/v4",
                 timeout_seconds: float = 30.0):
        """
        这里保留默认 Key/Model/Base URL，原因和 Gemini 一样：
        既支持启动期环境变量注入，也支持后续由 C++ 每次请求动态覆盖。
        """
        self.default_api_key = api_key
        self.default_model_name = model_name
        self.base_url = base_url.rstrip("/")
        self.timeout_seconds = timeout_seconds

    def _resolve_api_key_and_model(self,
                                   api_key: Optional[str],
                                   model: Optional[str]) -> tuple[str, str]:
        """
        既然 Settings 允许运行时切主 provider/model/api_key，那么 provider 侧就不能只吃构造时默认值。
        这里统一做一次“请求优先，默认兜底”的解析，避免每个调用方法都重复写这段分支。
        """
        target_api_key = api_key if api_key else self.default_api_key
        target_model = model if model else self.default_model_name
        if not target_api_key or target_api_key == "YOUR_API_KEY":
            raise ValueError("GLM API Key 未配置！请前往设置页面进行配置。")
        return target_api_key, target_model

    def _build_trace_error_payload(self,
                                   error_message: str,
                                   *,
                                   error_code: Optional[int] = None,
                                   error_status: str = "PROVIDER_ERROR") -> Dict[str, Any]:
        """
        Trace 主链只应该看统一失败协议，不应该再感知 GLM 原始 HTTP/JSON 细节。
        所以不管底下是超时、鉴权还是返回格式坏掉，这里都统一收口成 ok/code/status/message。
        """
        return {
            "ok": False,
            "error_code": error_code,
            "error_status": error_status,
            "error_message": error_message,
        }

    def _extract_error_payload(self, response: httpx.Response) -> Dict[str, Any]:
        """
        智谱错误体不一定只给 HTTP 状态码，通常还会带 code/msg。
        这里尽量把 HTTP 层和 body 层都提出来，给后面的熔断/日志保留更多真相。
        """
        error_code: Optional[int] = None
        error_message = response.text
        error_status = f"HTTP_{response.status_code}"

        try:
            error_json = response.json()
        except ValueError:
            return self._build_trace_error_payload(
                error_message=error_message,
                error_code=response.status_code,
                error_status=error_status,
            )

        if isinstance(error_json, dict):
            raw_code = error_json.get("code")
            if isinstance(raw_code, int):
                error_code = raw_code
            elif isinstance(raw_code, str) and raw_code.isdigit():
                error_code = int(raw_code)

            raw_message = error_json.get("msg") or error_json.get("message")
            if isinstance(raw_message, str) and raw_message:
                error_message = raw_message

            raw_status = error_json.get("error_status") or error_json.get("status")
            if isinstance(raw_status, str) and raw_status:
                error_status = raw_status

        if error_code is None:
            error_code = response.status_code

        return self._build_trace_error_payload(
            error_message=error_message,
            error_code=error_code,
            error_status=error_status,
        )

    def _build_usage_payload(self, usage_json: Any) -> Optional[Dict[str, int]]:
        """
        GLM 的 usage 命名更接近 prompt/completion/total 这一套。
        这里继续归一成 input/output/total，保持 C++ 口径稳定。
        """
        if not isinstance(usage_json, dict):
            return None

        input_tokens = int(usage_json.get("prompt_tokens", 0) or 0)
        output_tokens = int(usage_json.get("completion_tokens", 0) or 0)
        total_tokens = int(usage_json.get("total_tokens", 0) or 0)
        if total_tokens == 0:
            total_tokens = input_tokens + output_tokens

        return {
            "input_tokens": input_tokens,
            "output_tokens": output_tokens,
            "total_tokens": total_tokens,
        }

    def analyze(self, log_text: str, prompt: str, api_key: Optional[str] = None, model: Optional[str] = None) -> str:
        """
        当前主线已经不再依赖旧单日志分析链，这里先明确报未实现。
        等后续真的要恢复旧链，再决定是不是复用 Trace 这套 HTTP 路径。
        """
        raise NotImplementedError("GLM Provider 暂未实现单日志 analyze，仅支持 Trace 主链 analyze_trace。")

    def analyze_trace(self, trace_text: str, prompt: str, api_key: Optional[str] = None, model: Optional[str] = None) -> Dict[str, Any]:
        """
        Trace 分析主链：
        1. 用智谱官方 chat/completions REST 接口；
        2. 打开 response_format=json_object，让模型至少进入 JSON mode；
        3. 再由本地 json.loads + Pydantic 做字段校验，补齐“不是服务端 schema 强约束”的缺口。
        """
        try:
            target_api_key, target_model = self._resolve_api_key_and_model(api_key, model)
        except ValueError as exc:
            return self._build_trace_error_payload(str(exc), error_status="CONFIG_ERROR")

        request_payload = {
            "model": target_model,
            # Trace 路由上层已经把业务 guidance 和 trace_context 渲染进最终 prompt。
            # 所以这里直接把整段 prompt 作为 user message 发下去，避免重复拼接 trace_text。
            "messages": [
                {
                    "role": "user",
                    "content": prompt,
                }
            ],
            "stream": False,
            # 官方 chat/completions 当前正式支持的是 json_object，不是 json_schema。
            # 所以这里先开 JSON mode，再由本地做严格字段校验，不能误以为服务端已经帮我们兜底 schema。
            "response_format": {"type": "json_object"},
        }

        headers = {
            "Authorization": f"Bearer {target_api_key}",
            "Content-Type": "application/json",
        }

        try:
            with httpx.Client(timeout=self.timeout_seconds) as client:
                response = client.post(
                    f"{self.base_url}/chat/completions",
                    headers=headers,
                    json=request_payload,
                )
                if response.status_code >= 400:
                    return self._extract_error_payload(response)
                response.raise_for_status()
                response_json = response.json()
        except httpx.TimeoutException as exc:
            return self._build_trace_error_payload(str(exc), error_status="TIMEOUT")
        except httpx.HTTPError as exc:
            return self._build_trace_error_payload(str(exc), error_status="HTTP_ERROR")
        except ValueError as exc:
            return self._build_trace_error_payload(str(exc), error_status="INVALID_PROVIDER_RESPONSE")

        try:
            analysis_text = response_json["choices"][0]["message"]["content"]
        except (KeyError, IndexError, TypeError) as exc:
            return self._build_trace_error_payload(
                f"GLM trace response 缺少 choices/message/content: {exc}",
                error_status="INVALID_PROVIDER_RESPONSE",
            )

        try:
            analysis_json = json.loads(analysis_text)
        except json.JSONDecodeError as exc:
            return self._build_trace_error_payload(
                f"GLM trace response is not valid JSON: {exc}",
                error_status="PROVIDER_FORMAT_ERROR",
            )

        try:
            validated = LogAnalysisResult.model_validate(analysis_json)
        except ValidationError as exc:
            return self._build_trace_error_payload(
                f"GLM trace response failed schema validation: {exc}",
                error_status="PROVIDER_SCHEMA_ERROR",
            )

        return {
            "ok": True,
            "analysis": validated.model_dump(),
            "usage": self._build_usage_payload(response_json.get("usage")),
        }

    def chat(self, history: List[Dict[str, Any]], new_message: str) -> str:
        """
        多轮对话当前不在主线范围内，先明确报未实现。
        """
        raise NotImplementedError("GLM Provider 暂未实现 chat。")

    def analyze_batch(self, batch_logs: List[Dict[str, str]], prompt: str, api_key: Optional[str] = None, model: Optional[str] = None) -> List[Dict[str, Any]]:
        """
        旧批处理链已经脱离主线，所以这里先不补。
        """
        raise NotImplementedError("GLM Provider 暂未实现 analyze_batch。")

    def summarize(self, summary_logs: List[Dict[str, Any]], prompt: str, api_key: Optional[str] = None, model: Optional[str] = None) -> str:
        """
        Reduce 总结链路当前也不在主线范围内，先明确报未实现。
        """
        raise NotImplementedError("GLM Provider 暂未实现 summarize。")
