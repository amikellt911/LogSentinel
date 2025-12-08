// ai/GeminiApiAi.h
#pragma once

#include "ai/AiProvider.h"
#include <string>
#include <memory>
#include <nlohmann/json.hpp>

// 前向声明，避免在头文件中引入完整的cpr头文件，加快编译速度
namespace cpr {
    class Session;
}


class MockAI : public AiProvider {
public:
    MockAI();
    ~MockAI() override;

    // 使用 override 关键字明确表示我们正在重写基类的虚函数
    LogAnalysisResult analyze(const std::string& log_text) override;

    std::string chat(const std::string& history_json, const std::string& new_message) override;

    // 【新增】Map 阶段接口
    std::unordered_map<std::string, LogAnalysisResult> analyzeBatch(
        const std::vector<std::pair<std::string, std::string>>& logs
    ) override;

    // 【新增】Reduce 阶段接口
    std::string summarize(
        const std::vector<LogAnalysisResult>& results
    ) override;
private:
    std::string analyze_url_;
    std::string chat_url_;
};
