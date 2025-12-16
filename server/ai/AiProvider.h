// ai/AiProvider.h
#pragma once

#include <string>
#include <vector>
#include "AiTypes.h"
#include<utility>
// 为了与Python端的JSON结构保持一致，我们可以定义一个简单的消息结构体
// 不过，为了降低接口的依赖复杂度，一个更简单的初步方案是直接传递JSON字符串
/*
struct ChatMessage {
    std::string role;
    std::string content;
};
*/

class AiProvider {
public:
    virtual ~AiProvider() = default;

    /**
     * @brief 对单条日志文本进行单次分析
     * @param log_text 需要分析的日志
     * @return AI的分析结果
     */
    virtual LogAnalysisResult analyze(const std::string& log_text) = 0;

    /**
     * @brief 进行一次多轮对话
     * @param history_json 一个包含历史记录的、已序列化为字符串的JSON。
     *                     例如: "[{"role": "user", "parts": ["Hello"]}]"
     * @param new_message 用户本轮发送的新消息
     * @return AI本轮的回复
     */
    virtual std::string chat(const std::string& history_json, const std::string& new_message) = 0;

    //Map
    virtual std::unordered_map<std::string,LogAnalysisResult> analyzeBatch(const std::vector<std::pair<std::string,std::string>>& logs)=0;
    //Reduce
    virtual std::string summarize(const std::vector<LogAnalysisResult>& results)=0;
};

