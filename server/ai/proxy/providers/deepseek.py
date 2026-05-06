# ai/proxy/providers/deepseek.py

from .base import AIProvider
from ..schemas import LogAnalysisResult
from typing import List, Dict, Any, Optional
from pydantic import ValidationError
import httpx
import json


class DeepSeekProvider(AIProvider):
    """
    DeepSeek Provider。
    当前只接 Trace 主链：C++ 把 trace_text/prompt/model/key 交给 Python proxy，
    这里再按 DeepSeek OpenAI-compatible chat/completions 协议发给上游。
    """

    def __init__(self,
                 api_key: str = "",
                 model_name: str = "deepseek-v4-flash",
                 base_url: str = "https://api.deepseek.com",
                 timeout_seconds: float = 30.0):
        """
        默认模型选 deepseek-v4-flash，原因是它是当前 DeepSeek v4 的低成本入口。
        model/api_key 仍允许请求级覆盖，这样 Settings 的热更新凭证可以继续透传到同一个 provider 实例。
        """
        self.default_api_key = api_key
        self.default_model_name = model_name
        self.base_url = base_url.rstrip("/")
        self.timeout_seconds = timeout_seconds

    def _resolve_api_key_and_model(self,
                                   api_key: Optional[str],
                                   model: Optional[str]) -> tuple[str, str]:
        """
        数据来源有两层：启动期环境变量是默认值，C++ 每次请求下发的是运行时值。
        这里统一按“请求优先、默认兜底”解析，避免主路和 fallback 热更新时读到旧 key/model。
        """
        target_api_key = api_key if api_key else self.default_api_key
        target_model = model if model else self.default_model_name
        if not target_api_key or target_api_key == "YOUR_API_KEY":
            raise ValueError("DeepSeek API Key 未配置！请前往设置页面进行配置。")
        return target_api_key, target_model

    def _build_trace_error_payload(self,
                                   error_message: str,
                                   *,
                                   error_code: Optional[int] = None,
                                   error_status: str = "PROVIDER_ERROR",
                                   http_status: Optional[int] = None) -> Dict[str, Any]:
        """
        TraceSessionManager 只应该消费统一失败协议。
        所以 DeepSeek 的 HTTP 状态码、OpenAI 风格 error body、网络异常都会在这里收成同一份 ok=false JSON。
        """
        return {
            "ok": False,
            "error_code": error_code,
            "error_status": error_status,
            "error_message": error_message,
            "http_status": http_status,
        }

    def _extract_error_payload(self, response: httpx.Response) -> Dict[str, Any]:
        """
        DeepSeek 兼容 OpenAI 错误体，常见结构是 {"error": {"message": "...", "type": "...", "code": "..."}}。
        这里优先拆 body 里的业务信息，同时保留真实 HTTP 状态，方便 proxy retry 判断 429/5xx。
        """
        error_code: Optional[int] = response.status_code
        error_message = response.text
        error_status = f"HTTP_{response.status_code}"

        try:
            error_json = response.json()
        except ValueError:
            return self._build_trace_error_payload(
                error_message=error_message,
                error_code=error_code,
                error_status=error_status,
                http_status=response.status_code,
            )

        if isinstance(error_json, dict):
            error_obj = error_json.get("error")
            if isinstance(error_obj, dict):
                raw_message = error_obj.get("message")
                if isinstance(raw_message, str) and raw_message:
                    error_message = raw_message

                raw_status = error_obj.get("type") or error_obj.get("status")
                if isinstance(raw_status, str) and raw_status:
                    error_status = raw_status

                raw_code = error_obj.get("code")
                if isinstance(raw_code, int):
                    error_code = raw_code
                elif isinstance(raw_code, str) and raw_code.isdigit():
                    error_code = int(raw_code)
            else:
                raw_message = error_json.get("message") or error_json.get("msg")
                if isinstance(raw_message, str) and raw_message:
                    error_message = raw_message

                raw_status = error_json.get("error_status") or error_json.get("status")
                if isinstance(raw_status, str) and raw_status:
                    error_status = raw_status

        return self._build_trace_error_payload(
            error_message=error_message,
            error_code=error_code,
            error_status=error_status,
            http_status=response.status_code,
        )

    def _build_usage_payload(self, usage_json: Any) -> Optional[Dict[str, int]]:
        """
        DeepSeek 的 usage 字段沿用 OpenAI 命名。
        C++ 侧只看 input/output/total，所以这里做一次字段归一，避免上层到处判断 prompt_tokens。
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

    def _resolve_upstream_timeout_seconds(self, timeout_ms: Optional[int]) -> float:
        """
        外层 timeout_ms 是 C++ 给整条 provider 调用链的预算。
        内层 HTTP 要早一点超时，给 proxy 留出包装 TIMEOUT JSON 的时间，否则 C++ 只能拿到传输层 HTTP 0。
        """
        if timeout_ms is None or timeout_ms <= 0:
            return self.timeout_seconds
        return max(1.0, (timeout_ms - 1000) / 1000.0)

    def analyze(self, log_text: str, prompt: str, api_key: Optional[str] = None, model: Optional[str] = None) -> str:
        """
        旧单日志分析链已经不在当前主线里。
        这里明确报未实现，避免调用方误以为 DeepSeek 已经覆盖旧 analyze/batch/summarize 全部能力。
        """
        raise NotImplementedError("DeepSeek Provider 暂未实现单日志 analyze，仅支持 Trace 主链 analyze_trace。")

    def analyze_trace(self,
                      trace_text: str,
                      prompt: str,
                      api_key: Optional[str] = None,
                      model: Optional[str] = None,
                      timeout_ms: Optional[int] = None) -> Dict[str, Any]:
        """
        Trace 分析主链：
        1. 使用 DeepSeek OpenAI-compatible chat/completions；
        2. 打开 response_format=json_object，让模型尽量输出 JSON 对象；
        3. 再由本地 json.loads + Pydantic 校验，避免把格式坏掉的模型输出落成成功分析。
        """
        try:
            target_api_key, target_model = self._resolve_api_key_and_model(api_key, model)
        except ValueError as exc:
            return self._build_trace_error_payload(str(exc), error_status="CONFIG_ERROR")

        request_payload = {
            "model": target_model,
            # 上层已经把 trace_text 注入 prompt 模板。
            # 这里不能再额外拼一次 trace_text，否则同一条 Trace 会在上下文里重复出现，既浪费 token 也容易扰乱模型判断。
            "messages": [
                {
                    "role": "user",
                    "content": prompt,
                }
            ],
            "stream": False,
            # DeepSeek JSON Output 只保证返回 JSON object，不等于服务端强 schema。
            # 所以后面仍然必须做本地 JSON 解析和 LogAnalysisResult 校验。
            "response_format": {"type": "json_object"},
        }

        headers = {
            "Authorization": f"Bearer {target_api_key}",
            "Content-Type": "application/json",
        }
        upstream_timeout_seconds = self._resolve_upstream_timeout_seconds(timeout_ms)

        try:
            with httpx.Client(timeout=upstream_timeout_seconds) as client:
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
        except httpx.RequestError as exc:
            # DNS、连接失败、TLS 失败这类问题没有有效 HTTP 响应。
            # 对 retry 层来说它们更像临时网络抖动，而不是 4xx 这种确定性配置错误。
            return self._build_trace_error_payload(str(exc), error_status="NETWORK_ERROR")
        except httpx.HTTPError as exc:
            return self._build_trace_error_payload(str(exc), error_status="HTTP_ERROR")
        except ValueError as exc:
            return self._build_trace_error_payload(str(exc), error_status="INVALID_PROVIDER_RESPONSE")

        try:
            analysis_text = response_json["choices"][0]["message"]["content"]
        except (KeyError, IndexError, TypeError) as exc:
            return self._build_trace_error_payload(
                f"DeepSeek trace response 缺少 choices/message/content: {exc}",
                error_status="INVALID_PROVIDER_RESPONSE",
            )

        try:
            analysis_json = json.loads(analysis_text)
        except json.JSONDecodeError as exc:
            return self._build_trace_error_payload(
                f"DeepSeek trace response is not valid JSON: {exc}",
                error_status="PROVIDER_FORMAT_ERROR",
            )

        try:
            validated = LogAnalysisResult.model_validate(analysis_json)
        except ValidationError as exc:
            return self._build_trace_error_payload(
                f"DeepSeek trace response failed schema validation: {exc}",
                error_status="PROVIDER_SCHEMA_ERROR",
            )

        return {
            "ok": True,
            "analysis": validated.model_dump(),
            "usage": self._build_usage_payload(response_json.get("usage")),
        }

    def chat(self, history: List[Dict[str, Any]], new_message: str) -> str:
        """
        多轮对话当前不在 Trace 主线范围内，先明确报未实现。
        """
        raise NotImplementedError("DeepSeek Provider 暂未实现 chat。")

    def analyze_batch(self, batch_logs: List[Dict[str, str]], prompt: str, api_key: Optional[str] = None, model: Optional[str] = None) -> List[Dict[str, Any]]:
        """
        旧批处理链已经从活代码主线移除，DeepSeek 这次不恢复它。
        """
        raise NotImplementedError("DeepSeek Provider 暂未实现 analyze_batch。")

    def summarize(self, summary_logs: List[Dict[str, Any]], prompt: str, api_key: Optional[str] = None, model: Optional[str] = None) -> str:
        """
        Reduce 总结链路不属于本次 DeepSeek Trace provider 接入范围。
        """
        raise NotImplementedError("DeepSeek Provider 暂未实现 summarize。")
