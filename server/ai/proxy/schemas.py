# ai/proxy/schemas.py
from pydantic import BaseModel, Field
from typing import List, Optional, Any, Dict
from enum import Enum

class BatchLogItem(BaseModel):
    id: str = Field(..., description="来自 C++ 后端的唯一 Trace ID")
    text: str = Field(..., description="原始日志内容")

class BatchRequestSchema(BaseModel):
    batch: List[BatchLogItem]
    api_key: Optional[str] = None
    model: Optional[str] = None
    prompt: Optional[str] = None

class ChatRequest(BaseModel):
    history: List[Dict[str, Any]]
    new_message: str

# 定义风险等级枚举
class RiskLevel(str, Enum):
    HIGH = "high"
    MEDIUM = "medium"
    LOW = "low"
    INFO = "info" # 对应 C++ 里的 INFO
    UNKNOWN = "unknown"
    CRITICAL = "critical" # 处理旧版/映射后的等级
    ERROR = "error"
    WARNING = "warning"
    SAFE = "safe"

# 定义结构化输出的 Schema
class LogAnalysisResult(BaseModel):
    summary: str = Field(description="日志错误或问题的简要总结。")
    risk_level: str = Field(description="错误的风险等级。")
    root_cause: str = Field(description="错误的根本原因。")
    solution: str = Field(description="建议的解决方案或修复措施。")

class SummarizeRequest(BaseModel):
    results: List[LogAnalysisResult]
    api_key: Optional[str] = None
    model: Optional[str] = None
    prompt: Optional[str] = None
    
class BatchAnalysisResponse(BaseModel):
    results: List[Dict[str, Any]]

class BatchResultItem(BaseModel):
    id: str
    analysis: LogAnalysisResult

class BatchResponseSchema(BaseModel):
    results: List[BatchResultItem]

class SummaryResponse(BaseModel):
    summary: str

BATCH_PROMPT_TEMPLATE = """You are a professional log analysis expert.

Your task is to analyze the log entries provided below.
Analyze each log entry INDEPENDENTLY. Do not assume any correlation between these logs at this stage.

For EACH log entry, you must provide:
1. A concise summary.
2. Risk assessment ('critical', 'error', 'warning', 'info', 'safe', 'unknown').
3. The root cause based strictly on that specific log's content.
4. An actionable solution.

IMPORTANT:
- The 'id' in the output must match the input 'id' perfectly.
- Do not skip any entry, even if it looks simple or repetitive.
"""

SUMMARIZE_PROMPT_TEMPLATE = """You are a CTO or Lead Architect.

Your task is to identify the **Global Pattern** or **Root Cause** connecting the analysis reports provided below.
- If they are isolated errors, say so.
- If they indicate a larger failure (e.g., Database Down, Network Partition, DDoS), explicitly state it.

Format your response as a concise, high-level summary (1-2 sentences).
Do not list every single error. Focus on the "Big Picture".
"""
