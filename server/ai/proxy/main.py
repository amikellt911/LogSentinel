# ai/proxy/main.py
import argparse
import asyncio
import inspect
import os
from fastapi import FastAPI, Request, HTTPException
from starlette.concurrency import run_in_threadpool
import anyio.to_thread
from dotenv import load_dotenv
from typing import List, Dict, Any, Optional
from pydantic import BaseModel, ValidationError
from pathlib import Path
import uvicorn
import sys
# ==========================================
# 1. 路径与环境配置 (最先执行)
# ==========================================

# 获取当前文件 (main.py) 的绝对路径
# e.g., /home/llt/.../LogSentinel/ai/proxy/main.py
current_file = Path(__file__).resolve()

# 获取项目根目录 (LogSentinel/)
# main.py -> proxy -> ai -> LogSentinel
project_root = current_file.parent.parent.parent

# 将项目根目录加入 Python 搜索路径，解决 "ImportError"
sys.path.append(str(project_root))

# 加载 .env 文件
# 假设 .env 在 ai/ 目录下 (main.py -> proxy -> ai)
dotenv_path = current_file.parent.parent / '.env'
print(f"正在尝试加载 .env 文件: {dotenv_path}")
load_dotenv(dotenv_path=dotenv_path)

# ==========================================
# 2. 导入模块 (必须在 sys.path 设置之后)
# ==========================================
# 现在可以使用绝对导入了
from ai.proxy.providers.base import AIProvider
from ai.proxy.providers.deepseek import DeepSeekProvider
from ai.proxy.providers.gemini import GeminiProvider
from ai.proxy.providers.glm import GlmProvider
from ai.proxy.providers.mock import MockProvider
from ai.proxy.schemas import (
    BatchRequestSchema,
    ChatRequest,
    SummarizeRequest,
    TraceAnalyzeRequest,
    LOG_PROMPT_TEMPLATE,
    TRACE_PROMPT_TEMPLATE,
    BATCH_PROMPT_TEMPLATE,
    SUMMARIZE_PROMPT_TEMPLATE,
)


# --- 应用设置 ---
app = FastAPI(
    title="LogSentinel AI Proxy",
    description="一个用于代理不同 AI 提供商服务的中间层。",
    version="1.0.0",
)
DEFAULT_AI_PROXY_MAX_WORKERS = 128
app.state.ai_proxy_max_workers = DEFAULT_AI_PROXY_MAX_WORKERS


def parse_proxy_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    """
    proxy 的并发上限这里故意走 CLI，而不是藏进环境变量。
    benchmark 和答辩复现实验更需要“命令一眼可见”，
    这样别人拿到脚本就知道本轮压测到底配了多少代理层并发，而不是再去翻 shell 环境。
    """
    parser = argparse.ArgumentParser(description="LogSentinel AI Proxy")
    parser.add_argument("--host", default="127.0.0.1", help="监听地址")
    parser.add_argument("--port", type=int, default=8001, help="监听端口")
    parser.add_argument(
        "--max-workers",
        type=int,
        default=DEFAULT_AI_PROXY_MAX_WORKERS,
        help="AI 代理层允许同时执行的阻塞 provider 调用上限",
    )
    return parser.parse_args(argv)


def normalize_proxy_max_workers(raw_value: int) -> int:
    """
    这里校验的是本地 AI 代理层并发上限，不是厂商 API 的真实配额。
    如果值小于等于 0，就会把任何 provider 调用都直接卡死，所以启动期必须提前挡掉。
    """
    normalized = int(raw_value)
    if normalized <= 0:
        raise ValueError("max-workers must be > 0")
    return normalized


async def configure_default_thread_limiter(max_workers: int) -> None:
    """
    Starlette 的 run_in_threadpool 底下实际走的是 AnyIO 默认线程 limiter。
    所以这里真正要改的不是某个“看得见的 ThreadPoolExecutor”，
    而是默认 limiter 的 token 数；它才决定 proxy 同时能放多少个阻塞 provider 调用进后台线程。
    """
    limiter = anyio.to_thread.current_default_thread_limiter()
    limiter.total_tokens = normalize_proxy_max_workers(max_workers)


@app.on_event("startup")
async def configure_proxy_runtime_limits() -> None:
    """
    AnyIO 默认 limiter 必须在事件循环已经起来之后再改。
    所以这一步放到 FastAPI startup，而不是模块导入期；否则 current_default_thread_limiter() 拿不到后端上下文。
    """
    await configure_default_thread_limiter(app.state.ai_proxy_max_workers)
    print(f"[AI Proxy] max_workers={app.state.ai_proxy_max_workers}")


async def call_provider_in_threadpool(func, *args, **kwargs):
    """
    将同步 Provider 调用丢到框架线程池中执行。
    既然当前 Provider 接口还是同步 def，那么就不能在 async 路由里直接调用它，
    否则像 mock 里的 time.sleep、Gemini SDK 里的同步网络调用都会把 event loop 卡死。
    这里统一走线程池桥接，路由继续保持 async，阻塞工作交给后台线程。
    """
    return await run_in_threadpool(func, *args, **kwargs)


def build_trace_failure_result(provider_name: str,
                               error_message: str,
                               error_code: Optional[int] = None,
                               error_status: Optional[str] = None) -> Dict[str, Any]:
    """
    Trace 路由后续要给 C++ 做熔断和失败落库，所以失败结果不能再混进 HTTP 500 文本里。
    这里统一收口成稳定 JSON：上层只看 ok/code/status/message，不再关心 Python 里具体是哪家 SDK 抛的异常。
    """
    return {
        "ok": False,
        "provider": provider_name,
        "error_code": error_code,
        "error_status": error_status or "PROXY_INTERNAL_ERROR",
        "error_message": error_message,
    }


def normalize_trace_result(provider_name: str, result: Any) -> Dict[str, Any]:
    """
    Trace provider 现在允许两种输入：
    1. 新协议：已经返回 ok=true/false 的统一结构。
    2. 旧协议：直接返回 analysis 或 {analysis, usage}。
    既然当前正处于新旧切换期，这里就把所有分支收成同一外部契约，避免 C++ 跟着感知 provider 差异。
    """
    if isinstance(result, dict) and "ok" in result:
        normalized = dict(result)
        normalized["provider"] = provider_name
        if normalized.get("ok"):
            normalized.setdefault("usage", None)
        return normalized

    if isinstance(result, dict) and "analysis" in result:
        return {
            "ok": True,
            "provider": provider_name,
            "analysis": result.get("analysis"),
            "usage": result.get("usage"),
        }

    return {
        "ok": True,
        "provider": provider_name,
        "analysis": result,
        "usage": None,
    }


def render_trace_prompt(prompt_template: str, trace_text: str) -> str:
    """
    Trace 路由现在优先吃 C++ 下发的最终 prompt 模板。
    既然业务 prompt 和语言约束已经在冷启动阶段确定，那么这里真正要做的只剩最后一步：
    把本次请求的 trace_text 注入到模板里，而不是再回头猜测上层想要什么 Prompt 结构。
    """
    if "{{TRACE_CONTEXT}}" in prompt_template:
        return prompt_template.replace("{{TRACE_CONTEXT}}", trace_text)
    return f"{prompt_template}\n\n<trace_context>\n{trace_text}\n</trace_context>"


TRACE_RETRY_BACKOFF_SECONDS = (0.2, 0.5, 1.0)
TRACE_RETRYABLE_PROVIDER_STATUSES = {
    "NETWORK_ERROR",
    "RESOURCE_EXHAUSTED",
    "TOO_MANY_REQUESTS",
    "UNAVAILABLE",
    "INTERNAL",
    "INTERNAL_ERROR",
    "SERVER_ERROR",
    "SERVICE_UNAVAILABLE",
}
TRACE_RETRYABLE_STRUCTURE_STATUSES = {
    "PROVIDER_FORMAT_ERROR",
    "PROVIDER_SCHEMA_ERROR",
    "INVALID_PROVIDER_RESPONSE",
}


def normalize_retry_max_attempts(retry_max_attempts: Optional[int]) -> int:
    """
    这里把 retry_max_attempts 收口成“总尝试次数，包含第一次请求”。
    所以最小值必须是 1，避免出现 0 或负数把第一次正常调用也吞掉的怪语义。
    """
    if retry_max_attempts is None:
        return 1
    return max(1, int(retry_max_attempts))


def resolve_trace_failure_http_status(result: Dict[str, Any]) -> Optional[int]:
    """
    重试层优先看真实 HTTP 状态码，而不是厂商 body 里的业务 code。
    但是当前 Gemini SDK 失败载荷不一定显式带 http_status，所以这里允许在 error_code 明显长得像 HTTP 码时兜底复用。
    """
    raw_http_status = result.get("http_status")
    if isinstance(raw_http_status, int):
        return raw_http_status
    if isinstance(raw_http_status, str) and raw_http_status.isdigit():
        return int(raw_http_status)

    raw_error_code = result.get("error_code")
    if isinstance(raw_error_code, int) and 100 <= raw_error_code <= 599:
        return raw_error_code
    if isinstance(raw_error_code, str) and raw_error_code.isdigit():
        numeric_error_code = int(raw_error_code)
        if 100 <= numeric_error_code <= 599:
            return numeric_error_code
    return None


def get_trace_retry_backoff_seconds(completed_attempts: int) -> float:
    """
    completed_attempts 表示已经打出去并返回失败的次数。
    例如第一次失败后准备第二次发送，就拿第 0 档 200ms；后面再失败时逐步升档。
    """
    index = max(0, min(completed_attempts - 1, len(TRACE_RETRY_BACKOFF_SECONDS) - 1))
    return TRACE_RETRY_BACKOFF_SECONDS[index]


def should_retry_trace_failure(result: Dict[str, Any],
                               *,
                               completed_attempts: int,
                               retry_enabled: bool,
                               retry_max_attempts: int,
                               remaining_ms: Optional[int]) -> bool:
    """
    这里的判定顺序刻意很死：
    1. 先看开关、总次数、剩余预算这些“值不值得再花一枪”的前置条件；
    2. 再看这次失败是不是临时性抖动。
    这样可以避免后面把 provider 的零散异常到处 if/else，最后没人说得清一次失败为什么会进入重试。
    """
    if not retry_enabled:
        return False
    if completed_attempts >= retry_max_attempts:
        return False

    error_status = str(result.get("error_status", "")).upper()
    http_status = resolve_trace_failure_http_status(result)

    is_retryable_http = http_status is not None and (http_status == 429 or 500 <= http_status <= 599)
    is_retryable_provider_status = error_status in TRACE_RETRYABLE_PROVIDER_STATUSES
    is_retryable_structure_status = error_status in TRACE_RETRYABLE_STRUCTURE_STATUSES

    if is_retryable_structure_status and completed_attempts >= 2:
        # 结构错误只补一枪。
        # 因为它常常意味着输出偶发抖动；但如果连补一枪都还是结构坏掉，那更像 prompt/schema 设计问题，不该继续伪装成“系统在恢复”。
        return False

    if not (is_retryable_http or is_retryable_provider_status or is_retryable_structure_status):
        return False

    if remaining_ms is None:
        return True

    backoff_ms = int(get_trace_retry_backoff_seconds(completed_attempts) * 1000)
    return remaining_ms > backoff_ms


async def maybe_await(result: Any) -> Any:
    """
    测试里会塞同步 fake sleep，生产里会塞 asyncio.sleep。
    这里统一做一次“如果可等待就 await，否则直接返回”，避免测试代码为了配合实现被迫写成一堆假 coroutine。
    """
    if inspect.isawaitable(result):
        return await result
    return result


async def execute_trace_provider_with_retry(provider: AIProvider,
                                            *,
                                            trace_text: str,
                                            prompt: str,
                                            api_key: Optional[str],
                                            model: Optional[str],
                                            timeout_ms: Optional[int],
                                            retry_enabled: bool,
                                            retry_max_attempts: Optional[int],
                                            provider_call_fn=None,
                                            monotonic_fn=None,
                                            sleep_fn=asyncio.sleep) -> Any:
    """
    Trace 重试统一放在 proxy 路由层，不放进具体 provider。
    既然 ai_timeout_ms 被定义成“单个 provider 调用链的总预算”，那么每次 attempt 都必须重新看剩余时间，
    不能把 timeout 在 provider 内部一遍遍重置，否则所谓“总预算”就只是名义存在。
    """
    normalized_retry_max_attempts = normalize_retry_max_attempts(retry_max_attempts)
    if provider_call_fn is None:
        provider_call_fn = call_provider_in_threadpool
    monotonic = monotonic_fn or asyncio.get_running_loop().time
    deadline_seconds = None
    if timeout_ms is not None and timeout_ms > 0:
        deadline_seconds = monotonic() + (timeout_ms / 1000.0)

    completed_attempts = 0
    last_failure_result: Optional[Dict[str, Any]] = None

    while True:
        effective_timeout_ms = timeout_ms
        if deadline_seconds is not None and completed_attempts > 0:
            effective_timeout_ms = int((deadline_seconds - monotonic()) * 1000)
            if effective_timeout_ms <= 0:
                if last_failure_result is not None:
                    return last_failure_result
                return build_trace_failure_result(
                    provider_name="proxy",
                    error_message="Trace retry budget exhausted before provider call.",
                    error_status="TIMEOUT",
                )

        completed_attempts += 1
        result = await provider_call_fn(
            provider.analyze_trace,
            trace_text=trace_text,
            prompt=prompt,
            api_key=api_key,
            model=model,
            timeout_ms=effective_timeout_ms,
        )

        if not (isinstance(result, dict) and result.get("ok") is False):
            return result

        last_failure_result = result
        remaining_ms = None
        if deadline_seconds is not None:
            remaining_ms = max(0, int((deadline_seconds - monotonic()) * 1000))
        if not should_retry_trace_failure(
            result,
            completed_attempts=completed_attempts,
            retry_enabled=retry_enabled,
            retry_max_attempts=normalized_retry_max_attempts,
            remaining_ms=remaining_ms,
        ):
            return result

        await maybe_await(sleep_fn(get_trace_retry_backoff_seconds(completed_attempts)))

# --- Provider 实例化和注册 ---
# 在这里，我们创建所有可用的'转换插头'实例，并放入一个字典中进行管理。
# 这种方式使得添加新的 Provider 变得非常容易。
providers: Dict[str, AIProvider] = {}

# 无条件初始化 Gemini Provider (支持懒加载)
# 1. 获取环境变量（如果没有就是 None 或者 ""）
gemini_api_key = os.getenv("GEMINI_API_KEY", "")

# 2. 不管有没有 Key，都强行初始化！
#    我们把 "空白状态" 的处理逻辑下放给 Provider 内部去处理
try:
    print(f"正在初始化 Gemini Provider (Key 长度: {len(gemini_api_key)})...")
    # 哪怕传进去的是空字符串，也要让它活着
    providers["gemini"] = GeminiProvider(api_key=gemini_api_key)
    print("Gemini Provider 已初始化 (如果 Key 为空则等待配置传入)。")
except Exception as e:
    # 只有这种真正的代码报错才抓，配置问题不报错
    print(f"严重错误: 加载 Gemini 类失败: {e}")

providers["mock"] = MockProvider(delay=0.5)

# GLM 当前先只接 Trace 主链。
# 既然 provider 抽象层已经统一了 analyze_trace 协议，这里只需要注册实例；
# 旧 analyze/chat/batch/summarize 暂未实现时，会在 provider 内明确报未实现，而不是假装可用。
glm_api_key = os.getenv("GLM_API_KEY", "") or os.getenv("BIGMODEL_API_KEY", "")
glm_model_name = os.getenv("GLM_MODEL", "glm-5.1")
providers["glm"] = GlmProvider(api_key=glm_api_key, model_name=glm_model_name)

# DeepSeek 同样只接 Trace 主链。
# C++ 侧仍然通过 /analyze/trace/deepseek 路由进入 proxy；真正的 OpenAI-compatible HTTP 细节留在 provider 内部，
# 避免把 DeepSeek 的 base_url、错误体和 usage 字段扩散到主链。
deepseek_api_key = os.getenv("DEEPSEEK_API_KEY", "")
deepseek_model_name = os.getenv("DEEPSEEK_MODEL", "deepseek-v4-flash")
providers["deepseek"] = DeepSeekProvider(api_key=deepseek_api_key, model_name=deepseek_model_name)
# 未来可以在这里添加并注册 OpenAI, Claude 等其他 Provider
# openai_api_key = os.getenv("OPENAI_API_KEY")
# if openai_api_key:
#     providers["openai"] = OpenAIProvider(api_key=openai_api_key)


# --- API 端点定义 ---

@app.get("/")
def read_root():
    return {"status": "LogSentinel AI Proxy 正在运行", "available_providers": list(providers.keys())}

@app.post("/analyze/{provider_name}")
async def analyze_log(provider_name: str, request: Request):
    """
    单次日志分析端点。
    接收纯文本格式的日志。
    """
    provider = providers.get(provider_name)
    if not provider:
        raise HTTPException(status_code=404, detail=f"未找到或未配置 Provider '{provider_name}'。")

    try:
        log_text = (await request.body()).decode('utf-8')
        # 当前接口接收纯文本，动态配置（api_key/model/prompt）由批量接口承载。
        result = await call_provider_in_threadpool(
            provider.analyze,
            log_text=log_text,
            prompt=LOG_PROMPT_TEMPLATE,
        )
        return {"provider": provider_name, "analysis": result}
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"分析过程中发生错误: {e}")

@app.post("/analyze/trace/{provider_name}")
async def analyze_trace(provider_name: str, request: Request):
    """
    Trace 聚合结果分析端点。
    当前接收 text/plain 的序列化 trace payload。
    """
    provider = providers.get(provider_name)
    if not provider:
        raise HTTPException(status_code=404, detail=f"未找到或未配置 Provider '{provider_name}'。")

    try:
        body = await request.body()
        trace_text = ""
        prompt_template = TRACE_PROMPT_TEMPLATE
        content_type = request.headers.get("content-type", "").lower()

        if "application/json" in content_type:
            try:
                payload = TraceAnalyzeRequest.model_validate_json(body)
            except ValidationError as e:
                raise HTTPException(status_code=400, detail=f"Trace 请求体格式错误: {e}") from e
            trace_text = payload.trace_text
            if payload.prompt:
                prompt_template = payload.prompt
            model = payload.model
            api_key = payload.api_key
            # 这份 timeout_ms 不是给 FastAPI 路由自己用的，而是继续往 provider 透传。
            # 只有把总等待预算往下带，provider 才能把自己的上游 HTTP 超时裁剪得略早一点。
            timeout_ms = payload.timeout_ms
            retry_enabled = bool(payload.retry_enabled) if payload.retry_enabled is not None else False
            retry_max_attempts = payload.retry_max_attempts
        else:
            trace_text = body.decode('utf-8')
            model = None
            api_key = None
            timeout_ms = None
            retry_enabled = False
            retry_max_attempts = None

        rendered_prompt = render_trace_prompt(prompt_template, trace_text)
        result = await execute_trace_provider_with_retry(
            provider,
            trace_text=trace_text,
            prompt=rendered_prompt,
            api_key=api_key,
            model=model,
            timeout_ms=timeout_ms,
            retry_enabled=retry_enabled,
            retry_max_attempts=retry_max_attempts,
        )
        return normalize_trace_result(provider_name, result)
    except HTTPException:
        raise
    except Exception as e:
        # 这里故意不再把 provider 异常变成 HTTP 500。
        # 因为对 C++ 来说，“这次 AI 调用最终失败”是业务失败，不是协议层崩溃；
        # 只有继续返回统一 body，后面的 ai_status / 熔断计数才能稳定落地。
        return build_trace_failure_result(
            provider_name,
            error_message=str(e),
        )

@app.post("/analyze/batch/{provider_name}")
async def analyze_log_batch(provider_name: str, request: BatchRequestSchema):
    provider=providers.get(provider_name)
    if not provider:
        raise HTTPException(status_code=404, detail=f"未找到或未配置 Provider '{provider_name}'。")
    try:
        logs_list=[item.model_dump() for item in request.batch]

        # 使用提供的 Prompt 或回退到默认模板
        prompt_to_use = request.prompt if request.prompt else BATCH_PROMPT_TEMPLATE

        results = await call_provider_in_threadpool(
            provider.analyze_batch,
            batch_logs=logs_list,
            prompt=prompt_to_use,
            api_key=request.api_key,
            model=request.model,
        )
        return {"provider": provider_name, "results": results}
    except Exception as e:
        print(f"[Error] 批量分析失败: {e}")
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/summarize/{provider_name}")
async def summarize_logs(provider_name: str, request_data: SummarizeRequest):
    """
    Reduce 阶段：接收一批 LogAnalysisResult，生成全局总结。
    """
    provider = providers.get(provider_name)
    if not provider:
        raise HTTPException(status_code=404, detail=f"未找到 Provider '{provider_name}'")
    try:
        # 将 Pydantic 对象列表转为 Dict 列表
        results_list = [item.model_dump() for item in request_data.results]

        # 使用提供的 Prompt 或回退到默认模板
        prompt_to_use = request_data.prompt if request_data.prompt else SUMMARIZE_PROMPT_TEMPLATE

        # 调用 Provider 的 summarize 接口
        summary_text = await call_provider_in_threadpool(
            provider.summarize,
            summary_logs=results_list,
            prompt=prompt_to_use,
            api_key=request_data.api_key,
            model=request_data.model,
        )
        # 返回格式要匹配 C++ 端现有的总结协议
        # C++ 侧读取的是 response_json["summary"]，这里继续保持这个字段名不变。
        return {"provider": provider_name, "summary": summary_text}
    except Exception as e:
        print(f"[Error] 总结失败: {e}")
        import traceback
        traceback.print_exc()
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/chat/{provider_name}")
async def chat_with_logs(provider_name: str, chat_request: ChatRequest):
    """
    多轮日志对话端点。
    接收一个包含历史记录和新消息的 JSON 对象。
    """
    provider = providers.get(provider_name)
    if not provider:
        raise HTTPException(status_code=404, detail=f"未找到或未配置 Provider '{provider_name}'。")

    try:
        # 在这里，我们可以插入上下文管理逻辑（如滑动窗口、摘要等）
        # 简单起见，我们暂时直接传递历史记录
        managed_history = chat_request.history

        result = await call_provider_in_threadpool(
            provider.chat,
            history=managed_history,
            new_message=chat_request.new_message,
        )
        
        return {"provider": provider_name, "response": result}
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"对话过程中发生错误: {e}")
    

if __name__ == "__main__":
    # 启动入口保持单进程。
    # 这一刀先只把“单进程里允许多少个阻塞 provider 调用并发执行”做成显式旋钮，
    # 不在这里同时引入 uvicorn 多进程，避免 benchmark 时把实验变量搅在一起。
    args = parse_proxy_args()
    app.state.ai_proxy_max_workers = normalize_proxy_max_workers(args.max_workers)
    print(
        f"🚀 LogSentinel AI Proxy 正在 {args.host}:{args.port} 上启动..."
        f" max_workers={app.state.ai_proxy_max_workers}"
    )
    uvicorn.run(app, host=args.host, port=args.port)
