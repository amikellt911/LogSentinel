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
    CRITICAL = "critical" 
    ERROR = "error"
    WARNING = "warning"
    INFO = "info" 
    SAFE = "safe"
    UNKNOWN = "unknown"

# 定义结构化输出的 Schema
class LogAnalysisResult(BaseModel):
    summary: str = Field(description="日志错误或问题的简要总结。")
    # 强制使用 RiskLevel 枚举，如果 AI 返回了不在枚举中的值，Pydantic 会抛出校验错误
    risk_level: RiskLevel = Field(description="错误的风险等级。必须是 critical, error, warning, info, safe, unknown 之一。")
    root_cause: str = Field(description="错误的根本原因。")
    solution: str = Field(description="建议的解决方案或修复措施。")

class SummarizeRequest(BaseModel):
    results: List[LogAnalysisResult]
    api_key: Optional[str] = None
    model: Optional[str] = None
    prompt: Optional[str] = None

class TraceAnalyzeRequest(BaseModel):
    trace_text: str
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

class BatchAnalysisOutput(BaseModel):
    global_summary: str = Field(description="批次日志的宏观总结，概括主要问题。")
    global_risk_level: RiskLevel = Field(description="基于整批日志判断的全局风险等级。")
    key_patterns: List[str] = Field(description="识别出的关键模式或标签，例如 ['#DatabaseConnection', '#Timeout']。")

LOG_PROMPT_TEMPLATE = """You are a professional software engineer and log analysis expert.
Analyze the following single log entry and return ONLY a JSON object with:
summary, risk_level, root_cause, solution.

Risk level must be one of: critical, error, warning, info, safe, unknown.

Guidelines:
1) summary: concise and factual.
2) root_cause: infer from this log entry only.
3) solution: actionable remediation steps.
"""

# 这是 proxy 自己的兜底 Trace Prompt 模板。
# 正常情况下，C++ 会在冷启动时把“语言约束 + 业务 guidance”渲染成更完整的模板传下来；
# 这里保留一个默认版本，是为了兼容旧调用方和手工联调，而不是继续让 Python 端负责配置真值。
TRACE_PROMPT_TEMPLATE = """You are a distributed tracing analysis expert for LogSentinel.
You must analyze the input trace as a whole and return ONLY one JSON object.

Non-overridable rules:
1. The output JSON must contain exactly these fields:
   - summary
   - risk_level
   - root_cause
   - solution
2. risk_level must be one of:
   critical, error, warning, info, safe, unknown
3. If evidence is insufficient, use unknown instead of guessing.
4. Use English for all natural-language fields: summary, root_cause, solution.

Analysis priorities:
1. Focus on end-to-end failure propagation across spans.
2. Prioritize anomalies such as error status, timeout patterns, cycles, missing-parent, and abnormal latency.
3. root_cause should point to the most likely failing service/span chain.
4. solution should be specific and executable.

<business_guidance>
No additional business guidance provided.
</business_guidance>

<trace_context>
{{TRACE_CONTEXT}}
</trace_context>

Return ONLY valid JSON.
"""

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
