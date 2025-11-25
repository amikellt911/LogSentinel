# ai/proxy/main.py
import os
from fastapi import FastAPI, Request, HTTPException
from dotenv import load_dotenv
from typing import List, Dict, Any
from pydantic import BaseModel
from pathlib import Path

# 导入我们的Provider实现和基类
from .providers.base import AIProvider
from .providers.gemini import GeminiProvider

# --- 智能地加载.env文件 ---
# 获取此文件(main.py)所在的目录
current_dir = Path(__file__).parent
# .env文件位于上一级目录 (ai/)
dotenv_path = current_dir.parent / '.env'
# 从指定的路径加载环境变量
load_dotenv(dotenv_path=dotenv_path)
print(f"Attempting to load .env file from: {dotenv_path.resolve()}")


# --- 应用设置 ---
app = FastAPI(
    title="LogSentinel AI Proxy",
    description="一个用于代理不同AI提供商服务的中间层。",
    version="1.0.0",
)

# --- Provider 实例化和注册 ---
# 在这里，我们创建所有可用的'转换插头'实例，并放入一个字典中进行管理。
# 这种方式使得添加新的Provider变得非常容易。
providers: Dict[str, AIProvider] = {}

# 尝试初始化并注册Gemini Provider
gemini_api_key = os.getenv("GEMINI_API_KEY")
if gemini_api_key and gemini_api_key != "YOUR_API_KEY":
    try:
        providers["gemini"] = GeminiProvider(api_key=gemini_api_key)
        print("Gemini provider initialized successfully.")
    except Exception as e:
        print(f"Failed to initialize Gemini provider: {e}")
else:
    if not gemini_api_key:
        print("GEMINI_API_KEY not found in environment variables. Gemini provider is disabled.")
    else:
        print("Found placeholder GEMINI_API_KEY. Please replace 'YOUR_API_KEY' in ai/.env. Gemini provider is disabled.")

# 未来可以在这里添加并注册OpenAI, Claude等其他Provider
# openai_api_key = os.getenv("OPENAI_API_KEY")
# if openai_api_key:
#     providers["openai"] = OpenAIProvider(api_key=openai_api_key)


# --- API 端点定义 ---

@app.get("/")
def read_root():
    return {"status": "LogSentinel AI Proxy is running", "available_providers": list(providers.keys())}

@app.post("/analyze/{provider_name}")
async def analyze_log(provider_name: str, request: Request):
    """
    单次日志分析端点。
    接收纯文本格式的日志。
    """
    provider = providers.get(provider_name)
    if not provider:
        raise HTTPException(status_code=404, detail=f"Provider '{provider_name}' not found or not configured.")

    try:
        log_text = (await request.body()).decode('utf-8')
        # 优化的 Prompt：明确分析要求和风险等级判断标准
        default_prompt = """You are a professional software engineer and log analysis expert. Analyze the following log entry and provide:

1. A concise summary of the error or issue
2. Risk level assessment:
   - 'high': System crashes, data loss, security vulnerabilities, critical service failures
   - 'medium': Performance degradation, non-critical errors, warnings that may escalate
   - 'low': Informational messages, minor warnings, expected errors
3. Root cause analysis based on the log content
4. Actionable solution or remediation steps

Provide your analysis in a structured format."""
        
        result = provider.analyze(log_text=log_text, prompt=default_prompt)
        
        return {"provider": provider_name, "analysis": result}
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"An error occurred during analysis: {e}")


class ChatRequest(BaseModel):
    history: List[Dict[str, Any]]
    new_message: str

@app.post("/chat/{provider_name}")
async def chat_with_logs(provider_name: str, chat_request: ChatRequest):
    """
    多轮日志对话端点。
    接收一个包含历史记录和新消息的JSON对象。
    """
    provider = providers.get(provider_name)
    if not provider:
        raise HTTPException(status_code=404, detail=f"Provider '{provider_name}' not found or not configured.")

    try:
        # 在这里，我们可以插入上下文管理逻辑（如滑动窗口、摘要等）
        # 简单起见，我们暂时直接传递历史记录
        managed_history = chat_request.history

        result = provider.chat(history=managed_history, new_message=chat_request.new_message)
        
        return {"provider": provider_name, "response": result}
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"An error occurred during chat: {e}")