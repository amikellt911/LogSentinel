#pragma once
#include<string>
#include<optional>
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

struct TraceAiUsage{
    // usage 和结构化分析结果分开保存，避免把 token 元数据塞进业务 analysis JSON，
    // 导致 prompt/debug、告警和后续系统监控都要去同一棵树里硬拆字段。
    size_t input_tokens = 0;
    size_t output_tokens = 0;
    size_t total_tokens = 0;
    size_t cached_tokens = 0;
    size_t thoughts_tokens = 0;
};

struct TraceAiResponse{
    // analysis 继续保持原来的业务语义，给 trace_summary/analysis 表和前端详情页使用。
    LogAnalysisResult analysis;
    // usage 是 provider 可选返回的计量元数据；mock 或旧 provider 缺失时允许为空。
    std::optional<TraceAiUsage> usage;
};

struct BatchAnalysisResult{
    std::string global_summary;
    std::map<std::string,LogAnalysisResult> trace_id_to_result;
};
