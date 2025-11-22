# Gemini Python SDK 极简通关教程

你好！这篇文档是专门为你准备的“备忘录”和“教学课件”。
我们不讲复杂的理论，只讲**怎么用**，以及**为什么这么用**。

---

## 第一部分：核心三要素 (The Big Three)

使用 Gemini SDK 其实就是在做一道菜，你只需要准备好三个原料：

1.  **API Key (入场券)**：告诉 Google 你是谁，你有权限用。
2.  **Model Name (厨师)**：你要找哪个厨师做饭？是快手小厨 (`gemini-1.5-flash`) 还是米其林大厨 (`gemini-1.5-pro`)？
3.  **Prompt/Contents (点菜单)**：你想吃什么？（这里就是你的日志内容和指令）。

### 你的旧代码 (Current Implementation)
看看你现在的 `ai/proxy/providers/gemini.py`，是不是正好对应这三步？

```python
# 1. 引入库
from google import genai

class GeminiProvider:
    def __init__(self, api_key: str, model_name: str):
        # 【原料1】API Key: 创建 Client
        self.client = genai.Client(api_key=api_key)
        # 【原料2】Model Name: 记住你要用的模型
        self.model_name = model_name

    def analyze(self, log_text: str, prompt: str):
        # 【原料3】Contents: 拼接提示词和日志
        full_prompt = f"{prompt}\n\nLog to analyze:\n{log_text}"
        
        # 开始做饭 (调用 API)
        response = self.client.models.generate_content(
            model=self.model_name,
            contents=full_prompt
        )
        return response.text
```

**评价**：这写得完全没问题！简单直接。但它有一个缺点：**厨师做出来的菜（返回结果）可能摆盘很乱**。
比如你让它返回 JSON，它可能心情好就返回 JSON，心情不好就给你一段 Markdown，或者在 JSON 前面加一句 "Here is your JSON:"。这就导致你的程序很难解析。

---

## 第二部分：进阶玩法 —— 结构化输出 (Structured Output)

现在我们要升级了。我们不仅要点菜，还要规定**摆盘方式**。
我们要告诉厨师：“必须用方形盘子装，左边放肉，右边放菜，不要有多余的装饰。”

在 Python 里，描述“方形盘子、左肉右菜”最好的工具就是 **Pydantic**。

### 1. 定义“盘子” (Schema)
我们先用代码定义一下我们想要的结果格式。

```python
from pydantic import BaseModel, Field

# 这就是我们的“盘子”定义
class LogAnalysisResult(BaseModel):
    summary: str = Field(description="一句话概括这个日志的问题")
    risk_level: str = Field(description="风险等级，只能是 high, medium, or low")
    root_cause: str = Field(description="导致问题的根本原因")
    solution: str = Field(description="建议的修复方案")
```

### 2. 告诉厨师用这个盘子 (Config)
在调用 `generate_content` 时，多传一个 `config` 参数。

```python
    def analyze_structured(self, log_text: str):
        # 注意：Prompt 里不用再啰嗦 "请返回 JSON" 了，SDK 会自动处理
        prompt = f"Analyze this log:\n{log_text}"
        
        response = self.client.models.generate_content(
            model=self.model_name,
            contents=prompt,
            config={
                # 【关键点 A】告诉它我们要 JSON
                "response_mime_type": "application/json", 
                # 【关键点 B】把刚才定义的“盘子”类传进去
                "response_schema": LogAnalysisResult,
            }
        )
        
        # 现在 response.text 保证是完美的 JSON
        # 甚至 SDK 可能帮你直接转回对象 (response.parsed)
        return response.text
```

---

## 总结 (Cheat Sheet)

| 步骤 | 对应代码 | 你的任务 |
| :--- | :--- | :--- |
| **1. 认证** | `client = genai.Client(api_key=...)` | 确保 `.env` 里 Key 没填错 |
| **2. 选人** | `model="gemini-1.5-flash"` | 选对模型 (Flash 便宜快，Pro 聪明慢) |
| **3. 下单** | `contents="..."` | 把日志塞进去 |
| **4. 规矩 (新)** | `config={"response_schema": MyClass}` | **定义 Pydantic 类，强制它按格式输出** |

### 为什么这么做？
1.  **省心**：不用写正则去匹配结果了。
2.  **稳定**：AI 不会乱说话，只会填空。
3.  **专业**：这是 Google 官方推荐的 Best Practice。

这就好比以前你是**口头**跟厨师说“少放盐”，现在你是直接给了他一把**定量勺**，他想多放都不行。

---
**下一步行动**：
你可以尝试修改 `ai/proxy/main.py` 和 `ai/proxy/providers/gemini.py`，引入 `pydantic`，把这个新技巧用起来！
