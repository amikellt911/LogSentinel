#pragma once
#include <string>
#include <thread>
#include <chrono>

inline std::string mock_ai_analysis(const std::string &raw_log)
{
    // 模拟ai处理延时
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (raw_log.find("error") != std::string::npos)
    {
        return R"({"severity": "high", "suggestion": "Check application logs for stack trace."})";
    }
    else
    {
        return R"({"severity": "info", "suggestion": "No immediate action required."})";
    }
}