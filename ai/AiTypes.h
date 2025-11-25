#pragma once
#include<string>
#include<nlohmann/json.hpp>

struct LogAnalysisResult{
    std::string summary;
    std::string risk_level;
    std::string root_cause;
    std::string solution;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LogAnalysisResult,summary,risk_level,root_cause,solution);
};