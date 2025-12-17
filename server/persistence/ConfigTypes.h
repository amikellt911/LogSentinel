#pragma once
#include<string>
#include<vector>
#include<nlohmann/json.hpp>
#include "ai/AiTypes.h"
// 【新增历史日志结构体】
struct HistoricalLogItem {
    std::string trace_id;
    std::string risk_level;
    std::string summary;
    std::string processed_at; // 日志分析完成的时间戳

    // 必须有，用于 nlohmann::json 自动序列化给前端
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(HistoricalLogItem, trace_id, risk_level, summary, processed_at);
};

// 【新增历史日志分页结果的结构体】
struct HistoryPage {
    std::vector<HistoricalLogItem> logs;
    int total_count = 0; // 总记录数，用于前端分页组件

    // 必须有，用于 nlohmann::json 自动序列化给前端
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(HistoryPage, logs, total_count);
};

// 1. 通用配置 (对应 app_config 表)
struct AppConfig {
    // AI 设置
    std::string ai_provider = "openai";
    std::string ai_model = "gpt-4-turbo";
    std::string ai_api_key = "";
    std::string ai_language = "English";

    // 内核设置
    int kernel_worker_threads = 4;
    int kernel_max_batch = 50;
    int kernel_refresh_interval = 200;
    std::string kernel_io_buffer = "256MB";
    bool kernel_adaptive_mode = true;

    // 序列化宏 (注意：字段名需与 JSON key 及 DB config_key 一致)
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AppConfig, 
        ai_provider, ai_model, ai_api_key, ai_language,
        kernel_worker_threads, kernel_max_batch, kernel_refresh_interval, kernel_io_buffer, kernel_adaptive_mode
    )
};

// 2. Prompt 配置 (对应 ai_prompts 表)
struct PromptConfig {
    int id = 0; // 0 表示新增
    std::string name;
    std::string content;
    bool is_active = true;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(PromptConfig, id, name, content, is_active)
};

// 3. 通知渠道配置 (对应 alert_channels 表)
struct AlertChannel {
    int id = 0;
    std::string name;
    std::string provider; // "DingTalk", "Slack"
    std::string webhook_url;
    std::string alert_threshold; // "Critical", "Warning"
    std::string msg_template;
    bool is_active = false;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AlertChannel, id, name, provider, webhook_url, alert_threshold, is_active)
};

// 4. 聚合大对象 (用于 GET /settings/all)
struct AllSettings {
    AppConfig config;
    std::vector<PromptConfig> prompts;
    std::vector<AlertChannel> channels;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AllSettings, config, prompts, channels)
};

struct AlertInfo{
    std::string trace_id;
    std::string summary;
    std::string time;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AlertInfo, trace_id, summary, time);
};
struct DashboardStats{
    int total_logs=0;
    int high_risk=0;
    int medium_risk=0;
    int low_risk=0;
    int info_risk=0;
    int unknown_risk=0;
    double avg_response_time=0.0;
    std::vector<AlertInfo> recent_alerts;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(DashboardStats, total_logs, high_risk, medium_risk, low_risk,info_risk,unknown_risk, avg_response_time, recent_alerts);
};
struct AnalysisResultItem{
    std::string trace_id;
    LogAnalysisResult result;
    int response_time_ms;
    std::string status;
};