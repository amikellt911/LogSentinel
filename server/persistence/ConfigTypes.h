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
    PromptConfig() = default;
    PromptConfig(int id, std::string name, std::string content, bool is_active)
        : id(id), name(std::move(name)), content(std::move(content)), is_active(is_active) {}

    // 【核心修改】手动实现 to_json (序列化)
    friend void to_json(nlohmann::json& j, const PromptConfig& p) {
        j = nlohmann::json{
            {"id", p.id},
            {"name", p.name},
            {"content", p.content},
            {"is_active", p.is_active} // 输出时还是 true/false，规范
        };
    }

    // 【核心修改】手动实现 from_json (反序列化，兼容 Number 和 Boolean)
    friend void from_json(const nlohmann::json& j, PromptConfig& p) {
        j.at("id").get_to(p.id);
        j.at("name").get_to(p.name);
        j.at("content").get_to(p.content);

        // 重点：宽容处理 is_active
        if (j.contains("is_active")) {
            const auto& val = j.at("is_active");
            if (val.is_boolean()) {
                p.is_active = val.get<bool>();
            } 
            else if (val.is_number()) {
                // 兼容 SQLite 或前端传来的 0/1
                p.is_active = (val.get<int>() != 0); 
            }
            else if (val.is_string()) {
                 // 甚至兼容前端传 "true"/"1" 这种字符串的防御性写法
                 std::string s = val.get<std::string>();
                 p.is_active = (s == "true" || s == "1");
            }
        }
    }
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

    AlertChannel() = default;
    AlertChannel(int id, std::string name, std::string provider, std::string webhook_url, std::string alert_threshold, std::string msg_template, bool is_active)
        : id(id), name(std::move(name)), provider(std::move(provider)), webhook_url(std::move(webhook_url)), alert_threshold(std::move(alert_threshold)), msg_template(std::move(msg_template)), is_active(is_active) {}

    // 1. 序列化 (Backend -> JSON)
    friend void to_json(nlohmann::json& j, const AlertChannel& p) {
        j = nlohmann::json{
            {"id", p.id},
            {"name", p.name},
            {"provider", p.provider},
            {"webhook_url", p.webhook_url},
            {"alert_threshold", p.alert_threshold},
            {"msg_template", p.msg_template},
            {"is_active", p.is_active} // 输出标准 bool
        };
    }

    // 2. 反序列化 (JSON -> Backend) - 增强鲁棒性
    friend void from_json(const nlohmann::json& j, AlertChannel& p) {
        // 使用 .value() 可以在字段缺失时提供默认值，避免报错
        // 或者先 check contains 再 get_to
        if (j.contains("id")) j.at("id").get_to(p.id);
        if (j.contains("name")) j.at("name").get_to(p.name);
        if (j.contains("provider")) j.at("provider").get_to(p.provider);
        if (j.contains("webhook_url")) j.at("webhook_url").get_to(p.webhook_url);
        if (j.contains("alert_threshold")) j.at("alert_threshold").get_to(p.alert_threshold);
        if (j.contains("msg_template")) j.at("msg_template").get_to(p.msg_template);

        // 特殊处理 bool 类型的兼容性
        if (j.contains("is_active")) {
            const auto& val = j.at("is_active");
            if (val.is_boolean()) {
                p.is_active = val.get<bool>();
            } else if (val.is_number()) {
                p.is_active = (val.get<int>() != 0);
            } else if (val.is_string()) {
                std::string s = val.get<std::string>();
                p.is_active = (s == "true" || s == "1");
            }
        }
    }
};

// 4. 聚合大对象 (用于 GET /settings/all)
struct AllSettings {
    AppConfig config;
    std::vector<PromptConfig> prompts;
    std::vector<AlertChannel> channels;
    AllSettings() = default;
    AllSettings(AppConfig config_,std::vector<PromptConfig> prompts_,std::vector<AlertChannel> channels_)
    : config(std::move(config_)), prompts(std::move(prompts_)), channels(std::move(channels_)){}
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