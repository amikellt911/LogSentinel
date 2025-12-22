#pragma once
#include<string>
#include<nlohmann/json.hpp>

enum class RiskLevel {
    CRITICAL,
    ERROR,
    WARNING,
    INFO,
    SAFE,
    UNKNOWN
};

NLOHMANN_JSON_SERIALIZE_ENUM(RiskLevel, {
    {RiskLevel::UNKNOWN, "unknown"},
    {RiskLevel::CRITICAL, "critical"},
    {RiskLevel::ERROR, "error"},
    {RiskLevel::WARNING, "warning"},
    {RiskLevel::INFO, "info"},
    {RiskLevel::SAFE, "safe"},
})

struct LogAnalysisResult{
    std::string summary;
    RiskLevel risk_level;
    std::string root_cause;
    std::string solution;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LogAnalysisResult,summary,risk_level,root_cause,solution);
};

struct BatchAnalysisResult{
    std::string global_summary;
    std::map<std::string,LogAnalysisResult> trace_id_to_result;
};