// ai/GeminiApiAi.cpp
#include "ai/GeminiApiAi.h"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp> // 我们需要一个JSON库来解析单次分析的响应和构建聊天请求

// 构造函数：初始化会话和URL
GeminiApiAi::GeminiApiAi() {
    session_ = std::make_unique<cpr::Session>();
    
    // 从配置或环境变量中读取代理地址是更好的做法，这里为了简单暂时硬编码
    const std::string base_url = "http://127.0.0.1:8001";
    analyze_url_ = base_url + "/analyze/gemini";
    chat_url_ = base_url + "/chat/gemini";

    session_->SetHeader(cpr::Header{{"Content-Type", "text/plain"}});
}

// 析构函数，默认即可，unique_ptr会自动管理内存
GeminiApiAi::~GeminiApiAi() = default;

std::string GeminiApiAi::analyze(const std::string& log_text) {
    session_->SetUrl(cpr::Url{analyze_url_});
    session_->SetBody(cpr::Body{log_text});

    cpr::Response r = session_->Post();

    if (r.status_code == 200) {
        try {
            // Python服务返回的是一个JSON，格式为 {"provider": "...", "analysis": "..."}
            // 我们需要解析它并提取 "analysis" 字段
            nlohmann::json response_json = nlohmann::json::parse(r.text);
            if (response_json.contains("analysis")) {
                return response_json["analysis"];
            } else {
                return "Error: 'analysis' field not found in response.";
            }
        } catch (const nlohmann::json::parse_error& e) {
            return "Error: Failed to parse JSON response from proxy. Details: " + std::string(e.what());
        }
    } else {
        // 如果请求失败，返回包含状态码和错误信息的字符串
        return "Error: AI proxy service returned status " + std::to_string(r.status_code) + ". Body: " + r.text;
    }
}

std::string GeminiApiAi::chat(const std::string& history_json, const std::string& new_message) {
    // 对于聊天，我们需要发送一个JSON对象，所以要设置正确的Content-Type
    session_->SetHeader(cpr::Header{{"Content-Type", "application/json"}});
    session_->SetUrl(cpr::Url{chat_url_});

    // 构建Python服务期望的JSON结构
    nlohmann::json request_body;
    try {
        // history_json本身就是一个字符串化的JSON数组，我们需要先解析它
        request_body["history"] = nlohmann::json::parse(history_json);
    } catch (const nlohmann::json::parse_error& e) {
        return "Error: Invalid history_json format. Details: " + std::string(e.what());
    }
    request_body["new_message"] = new_message;

    session_->SetBody(cpr::Body{request_body.dump()});

    cpr::Response r = session_->Post();

    if (r.status_code == 200) {
        try {
            // Python服务返回 {"provider": "...", "response": "..."}
            nlohmann::json response_json = nlohmann::json::parse(r.text);
            if (response_json.contains("response")) {
                return response_json["response"];
            } else {
                return "Error: 'response' field not found in response.";
            }
        } catch (const nlohmann::json::parse_error& e) {
            return "Error: Failed to parse JSON response from proxy. Details: " + std::string(e.what());
        }
    } else {
        return "Error: AI proxy service returned status " + std::to_string(r.status_code) + ". Body: " + r.text;
    }
}
