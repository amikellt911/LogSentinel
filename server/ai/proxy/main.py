# ai/proxy/main.py
import os
from fastapi import FastAPI, Request, HTTPException
from dotenv import load_dotenv
from typing import List, Dict, Any, Optional
from pydantic import BaseModel
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
from ai.proxy.schemas import BatchRequestSchema,ChatRequest,SummarizeRequest,BATCH_PROMPT_TEMPLATE,SUMMARIZE_PROMPT_TEMPLATE


# --- 应用设置 ---
app = FastAPI(
    title="LogSentinel AI Proxy",
    description="一个用于代理不同 AI 提供商服务的中间层。",
    version="1.0.0",
)

# --- Provider 实例化和注册 ---
# 在这里，我们创建所有可用的'转换插头'实例，并放入一个字典中进行管理。
# 这种方式使得添加新的 Provider 变得非常容易。
providers: Dict[str, AIProvider] = {}

# 尝试初始化并注册 Gemini Provider
gemini_api_key = os.getenv("GEMINI_API_KEY")
if gemini_api_key and gemini_api_key != "YOUR_API_KEY":
    try:
        providers["gemini"] = GeminiProvider(api_key=gemini_api_key)
        print("Gemini Provider 初始化成功。")
    except Exception as e:
        print(f"初始化 Gemini Provider 失败: {e}")
else:
    if not gemini_api_key:
        print("环境变量中未找到 GEMINI_API_KEY。Gemini Provider 已禁用。")
    else:
        print("发现 GEMINI_API_KEY 占位符。请在 ai/.env 中替换 'YOUR_API_KEY'。Gemini Provider 已禁用。")
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
        # 优化的 Prompt：明确分析要求和风险等级判断标准
        default_prompt = """You are a professional software engineer and log analysis expert. Analyze the following log entry and provide:

1. A concise summary of the error or issue
2. Risk level assessment:
   - 'critical': System crashes, data loss, security vulnerabilities, critical service failures
   - 'error': Performance degradation, non-critical errors, warnings that may escalate
   - 'warning': Informational messages, minor warnings, expected errors
   - 'info': Normal operational logs, state changes, heartbeats
   - 'safe': Verified safe operations
   - 'unknown': Unintelligible logs, binary data, or insufficient context to determine risk
3. Root cause analysis based on the log content
4. Actionable solution or remediation steps

Provide your analysis in a structured format."""
        
        # 注意: analyze() 仅接收来自 C++ 的原始文本，因此无法在此提取 api_key/model。
        # 我们使用默认的 Provider 配置。
        result = provider.analyze(log_text=log_text, prompt=default_prompt)
        
        return {"provider": provider_name, "analysis": result}
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"分析过程中发生错误: {e}")

@app.post("/analyze/batch/{provider_name}")
async def analyze_log_batch(provider_name: str, request: BatchRequestSchema):
    provider=providers.get(provider_name)
    if not provider:
        raise HTTPException(status_code=404, detail=f"未找到或未配置 Provider '{provider_name}'。")
    try:
        logs_list=[item.model_dump() for item in request.batch]

        # 使用提供的 Prompt 或回退到默认模板
        prompt_to_use = request.prompt if request.prompt else BATCH_PROMPT_TEMPLATE

        results=provider.analyze_batch(
            batch_logs=logs_list,
            prompt=prompt_to_use,
            api_key=request.api_key,
            model=request.model
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
        summary_text = provider.summarize(
            summary_logs=results_list,
            prompt=prompt_to_use,
            api_key=request_data.api_key,
            model=request_data.model
        )
        # 返回格式要匹配 C++ 端的预期
        # C++ MockAI::summarize 里解析的是 response_json["summary"]
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

        result = provider.chat(history=managed_history, new_message=chat_request.new_message)
        
        return {"provider": provider_name, "response": result}
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"对话过程中发生错误: {e}")
    

if __name__ == "__main__":
    # host="0.0.0.0" 允许局域网访问，"127.0.0.1" 仅限本机
    # workers=1 单进程模式，适合调试
    print(f"🚀 LogSentinel AI Proxy 正在端口 8001 上启动...")
    uvicorn.run(app, host="127.0.0.1", port=8001)
