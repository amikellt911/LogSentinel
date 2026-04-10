# ai/proxy/main.py
import os
from fastapi import FastAPI, Request, HTTPException
from starlette.concurrency import run_in_threadpool
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
from ai.proxy.providers.gemini import GeminiProvider
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
        else:
            trace_text = body.decode('utf-8')
            model = None
            api_key = None

        rendered_prompt = render_trace_prompt(prompt_template, trace_text)
        result = await call_provider_in_threadpool(
            provider.analyze_trace,
            trace_text=trace_text,
            prompt=rendered_prompt,
            api_key=api_key,
            model=model,
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
    # host="0.0.0.0" 允许局域网访问，"127.0.0.1" 仅限本机
    # workers=1 单进程模式，适合调试
    print(f"🚀 LogSentinel AI Proxy 正在端口 8001 上启动...")
    uvicorn.run(app, host="127.0.0.1", port=8001)
