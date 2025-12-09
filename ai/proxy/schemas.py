# ai/proxy/schemas.py
from pydantic import BaseModel, Field
from typing import List
from typing import Any,Dict
from enum import Enum
class BatchLogItem(BaseModel):
    id: str = Field(..., description="Unique trace ID from C++ backend")
    text: str = Field(..., description="Raw log content")

class BatchRequest(BaseModel):
    batch: List[BatchLogItem]

class ChatRequest(BaseModel):
    history: List[Dict[str, Any]]
    new_message: str

# 定义风险等级枚举
class RiskLevel(str, Enum):
    HIGH = "high"
    MEDIUM = "medium"
    LOW = "low"
    INFO = "info" # 建议加上 info，对应 C++ 里的未知情况
# 定义结构化输出的 Schema
class LogAnalysisResult(BaseModel):
    summary: str = Field(description="A concise summary of the log error.")
    risk_level: RiskLevel = Field(description="The risk level of the error. Must be one of: 'high', 'medium', 'low','info'.")
    root_cause: str = Field(description="The root cause of the error.")
    solution: str = Field(description="Suggested solution or fix.")

class SummarizeRequest(BaseModel):
    results: List[LogAnalysisResult]
    
BATCH_PROMPT_TEMPLATE = """You are a professional log analysis expert.
I have provided a list of {count} log entries below. 

Your task is to analyze each log entry INDEPENDENTLY. 
Do not assume any correlation between these logs at this stage.

For EACH log entry, you must provide:
1. A concise summary.
2. Risk assessment ('high', 'medium', 'low').
3. The root cause based strictly on that specific log's content.
4. An actionable solution.

IMPORTANT: 
- You must return exactly {count} results.
- The 'id' in the output must match the input 'id' perfectly.
- Do not skip any entry, even if it looks simple or repetitive.

Logs to analyze:
{log_content_json}
"""

SUMMARIZE_PROMPT_TEMPLATE = """You are a CTO or Lead Architect. 
I will provide you with a list of {count} analysis reports generated from system logs.

Your task is to identify the **Global Pattern** or **Root Cause** connecting these events.
- If they are isolated errors, say so.
- If they indicate a larger failure (e.g., Database Down, Network Partition, DDoS), explicitly state it.

Format your response as a concise, high-level summary (1-2 sentences). 
Do not list every single error. Focus on the "Big Picture".

Analysis Reports:
{analysis_json}
"""