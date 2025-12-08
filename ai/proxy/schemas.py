# ai/proxy/schemas.py
from pydantic import BaseModel, Field
from typing import List
from typing import Any,Dict
class BatchLogItem(BaseModel):
    id: str = Field(..., description="Unique trace ID from C++ backend")
    text: str = Field(..., description="Raw log content")

class BatchRequest(BaseModel):
    batch: List[BatchLogItem]

class ChatRequest(BaseModel):
    history: List[Dict[str, Any]]
    new_message: str

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
