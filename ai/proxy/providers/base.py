# ai/proxy/providers/base.py
from abc import ABC, abstractmethod
from typing import List, Dict, Any

class AIProvider(ABC):
    """
    AI提供商的标准接口。
    它的核心职责是定义与大模型API通信的行为，如“生成内容”。
    具体的配置（如api_key, model_name）将在子类的构造函数__init__中处理。
    """

    @abstractmethod
    def analyze(self, log_text: str, prompt: str) -> str:
        """
        执行一次性的、无上下文的分析任务。
        """
        pass

    @abstractmethod
    def chat(self, history: List[Dict[str, Any]], new_message: str) -> str:
        """
        根据提供的历史记录，进行一次多轮对话。
        注意：上下文管理（如压缩、截断）的逻辑应该在此方法被调用'之前'完成。
        此方法接收的是已经处理好的、可以直接发给API的history。
        """
        pass
