#pragma once
#include<string>
#include <chrono>
struct AnalysisTask
{
    std::string trace_id;
    std::string raw_request_body;
    std::chrono::steady_clock::time_point start_time;
    // AI Config
    std::string ai_api_key;
    std::string ai_model;
    std::string prompt;
};
