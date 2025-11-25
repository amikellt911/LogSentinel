// ai/GeminiApiAi.cpp
#include "ai/GeminiApiAi.h"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp> // 我们需要一个JSON库来解析单次分析的响应和构建聊天请求
#include <vector>            // 用于字段验证

// 构造函数：初始化会话和URL
GeminiApiAi::GeminiApiAi()
{
   
    // 从配置或环境变量中读取代理地址是更好的做法，这里为了简单暂时硬编码
    const std::string base_url = "http://127.0.0.1:8001";
    analyze_url_ = base_url + "/analyze/gemini";
    chat_url_ = base_url + "/chat/gemini";


}

// 析构函数，默认即可，unique_ptr会自动管理内存
GeminiApiAi::~GeminiApiAi() = default;

std::string GeminiApiAi::analyze(const std::string &log_text)
{
    cpr::Session session_;
    session_.SetHeader(cpr::Header{{"Content-Type", "text/plain"}});
    session_.SetTimeout(std::chrono::seconds(10));
    session_.SetUrl(cpr::Url{analyze_url_});
    session_.SetBody(cpr::Body{log_text});

    cpr::Response r = session_.Post();//之后能不能用协程？

    if (r.status_code != 200)
    {
        throw std::runtime_error("AI Proxy Error: HTTP " + std::to_string(r.status_code) +
                                 ", Body: " + r.text);
    }
    nlohmann::json response_json;
    try
    {
        response_json = nlohmann::json::parse(r.text);
    }
    catch (const nlohmann::json::parse_error &e)
    {
        throw std::runtime_error("Invalid JSON from Proxy: " + std::string(e.what()));
    }

    if (!response_json.contains("analysis"))
    {
        throw std::runtime_error("Protocol Error: Response missing 'analysis' field");
    }
    std::string inner_json_str = response_json["analysis"];
    nlohmann::json analysis_json;
    try
    {
        analysis_json = nlohmann::json::parse(inner_json_str);
    }
    catch (const nlohmann::json::parse_error &e)
    {
        throw std::runtime_error("AI Logic Error: Returned non-JSON string. Raw: " +
                                 inner_json_str + ". Error: " + e.what());
    }
    const std::vector<std::string> required_fields = {"summary", "risk_level", "root_cause", "solution"};
    for (const auto &field : required_fields)
    {
        if (!analysis_json.contains(field))
        {
            throw std::runtime_error("Validation Error: AI missing required field '" + field + "'");
        }
    }
    std::string risk = analysis_json["risk_level"];
    if (risk != "high" && risk != "medium" && risk != "low" && risk != "info")
    {
        // 这里可以选择抛异常，或者自动修正为 unknown，看业务宽容度
        throw std::runtime_error("Validation Error: Invalid risk_level '" + risk + "'");
    }

    // 这里做一次 get<T> 再 dump 是为了清洗数据，丢弃 AI 可能返回的多余字段
    LogAnalysisResult result = analysis_json.get<LogAnalysisResult>();
    return nlohmann::json(result).dump();
}

std::string GeminiApiAi::chat(const std::string& history_json, const std::string& new_message) {
    // 1. 准备请求头
    cpr::Session session_;
    session_.SetHeader(cpr::Header{{"Content-Type", "text/plain"}});
    session_.SetTimeout(std::chrono::seconds(10));
    session_.SetUrl(cpr::Url{chat_url_});

    // 2. 构建请求体 (数据准备阶段)
    nlohmann::json request_body;
    
    // 2.1 验证并解析输入的 history_json
    // 如果上层传来的 history 格式不对，直接在这里这就该炸，属于非法参数
    try {
        request_body["history"] = nlohmann::json::parse(history_json);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::invalid_argument("Chat Error: Invalid history_json format. " + std::string(e.what()));
    }
    
    request_body["new_message"] = new_message;
    session_.SetBody(cpr::Body{request_body.dump()});

    // 3. 发起网络请求
    cpr::Response r = session_.Post();

    // 4. 卫语句：检查 HTTP 状态
    if (r.status_code != 200) {
        throw std::runtime_error("Chat Proxy Error: HTTP " + std::to_string(r.status_code) + 
                                 ", Body: " + r.text);
    }

    // 5. 解析外层 JSON
    nlohmann::json response_json;
    try {
        response_json = nlohmann::json::parse(r.text);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Chat Protocol Error: Proxy returned invalid JSON. " + std::string(e.what()));
    }

    // 6. 检查业务字段
    if (!response_json.contains("response")) {
        throw std::runtime_error("Chat Protocol Error: Missing 'response' field in proxy output");
    }

    // 7. 成功返回 (确保它是个字符串)
    // .get<std::string>() 会自动处理类型转换，如果不是 string 会抛出 type_error，这也符合我们的异常预期
    return response_json["response"].get<std::string>();
}