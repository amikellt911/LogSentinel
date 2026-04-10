#pragma once

#include <string>

// 这组 helper 只负责把 Settings 里的冷启动 Prompt 配置渲染成 Trace AI 可直接使用的模板。
// 之所以单独抽文件，而不是把字符串拼接逻辑散落到 main.cpp，
// 是为了把“结构化业务 prompt 解析”和“最终 trace prompt 渲染”收口到一个地方，
// 避免后面再接别的 Trace AI backend 时又把同样的规则复制一遍。
std::string BuildTracePromptTemplate(const std::string& ai_language,
                                     const std::string& active_prompt_content);
