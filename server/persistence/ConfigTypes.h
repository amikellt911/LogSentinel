#pragma once
#include<string>
#include<vector>
#include<utility>
#include<map>
#include<nlohmann/json.hpp>
#include "ai/AiTypes.h"
// 【新增历史日志结构体】
struct HistoricalLogItem {
    std::string trace_id;
    RiskLevel risk_level;
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
// 这里继续保留 KV 投影，而不是把 Settings 直接拆成强 schema 表。
// 原因很直接：这批字段现在仍在收口期，后面真正频繁变化的是 key 集，而不是整张表的物理结构。
// 所以这一层只负责把“当前认可的稳定 key”集中成一个类型安全的视图，便于 Repository 读写和快照发布。
struct AppConfig {
    // AI 设置
    std::string ai_provider = "mock";
    // 这个总开关表达的是“当前是否允许 trace 主链真正发起 AI 分析”。
    // 关闭后 trace 仍然会正常聚合、落 summary/spans，只是 worker 会把 ai_status 收成 skipped_manual。
    bool ai_analysis_enabled = true;
    std::string ai_language = "en";
    std::string app_language = "en";
    // 这个超时控制的是 C++ 后端等待 Python proxy 返回 Trace AI 结果的最长时间。
    // 如果这里太短，像 GLM 免费额度这类首包慢的 provider 就会在模型还没回包前被后端提前判死。
    int ai_timeout_ms = 30000;

    // 基础设置
    int http_port = 8080;
    int log_retention_days = 7;

    // AI 可靠性控制
    bool ai_retry_enabled = false;
    int ai_retry_max_attempts = 3;
    bool ai_auto_degrade = false;
    // provider 路由仍然只存主/备选择；真正的 model/api_key 已经下沉到 ai_provider_profiles。
    // 这样用户切 provider 时不会把上一家 provider 的模型名误带到下一家请求里。
    std::string ai_fallback_provider = "mock";
    bool ai_circuit_breaker = true;
    int ai_failure_threshold = 5;
    int ai_cooldown_seconds = 60;

    // Active Prompt ID（单一提示词方案）
    int active_prompt_id = 0;

    // 内核与主链运行时参数
    // 这里显式把 MiniMuduo I/O 线程和主 worker 线程拆开。
    // 否则 Settings/benchmark/论文里一说“线程数”，用户根本分不清是在调 Reactor 收包线程，
    // 还是在调真正承接 Trace 聚合、AI 调用和落库协作的工作线程池。
    int kernel_io_threads = 1;
    int kernel_worker_threads = 4;
    // dispatch_worker_threads 控制的是 dispatch queue 的消费者数量。
    // 这一层和 worker_threads 不同：dispatch 线程负责主数据准备与入写入口，
    // worker 线程才负责 AI / analysis / webhook 的第二阶段收尾。
    int dispatch_worker_threads = 1;
    std::string trace_end_field = "trace_end";
    // 别名在内存快照里直接用数组，而不是继续塞 JSON 字符串。
    // 既然 /logs/spans 是热路径，那么解析入口就应该直接拿现成的别名数组，
    // 不能每个请求再把字符串反序列化一遍；真正的序列化/反序列化成本只留在 Repository 边界。
    std::vector<std::string> trace_end_aliases = {};
    int token_limit = 0;
    int span_capacity = 100;
    int collecting_idle_timeout_ms = 5000;
    int sealed_grace_window_ms = 1000;
    int retry_base_delay_ms = 500;
    int sweep_tick_ms = 500;
    // 生命周期 profile 会改 TraceSessionManager 的状态机分支。
    // 既然它决定的是“trace 结束后到底还要不要给 grace / tombstone”，那就必须按冷启动语义收口，
    // 不能运行中随便热切，否则同一进程前后两批 trace 会吃到两套不同生命周期口径。
    std::string trace_lifecycle_profile = "protected";

    // 三类积压指标各自维护 overload / critical 百分比，避免把不同容量基数硬揉成一个总水位。
    int wm_active_sessions_overload = 75;
    int wm_active_sessions_critical = 90;
    int wm_buffered_spans_overload = 75;
    int wm_buffered_spans_critical = 90;
    int wm_pending_tasks_overload = 75;
    int wm_pending_tasks_critical = 90;

    // 序列化宏 (注意：字段名需与 JSON key 及 DB config_key 一致)
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AppConfig, 
        ai_provider, ai_analysis_enabled, ai_language, app_language, ai_timeout_ms,
        http_port, log_retention_days,
        ai_retry_enabled, ai_retry_max_attempts,
        ai_auto_degrade, ai_fallback_provider,
        ai_circuit_breaker, ai_failure_threshold, ai_cooldown_seconds,
        active_prompt_id,
        kernel_io_threads, kernel_worker_threads, dispatch_worker_threads,
        trace_end_field, trace_end_aliases, token_limit, span_capacity,
        collecting_idle_timeout_ms, sealed_grace_window_ms, retry_base_delay_ms, sweep_tick_ms,
        trace_lifecycle_profile,
        wm_active_sessions_overload, wm_active_sessions_critical,
        wm_buffered_spans_overload, wm_buffered_spans_critical,
        wm_pending_tasks_overload, wm_pending_tasks_critical
    )
};

// AI Provider Profile 是 model/api_key 的唯一配置来源。
// app_config 只保留“当前选哪家 provider”的冷启动路由选择，profile 表按 provider 保存这家自己的模型和凭证。
// 这样数据流会变成：Settings 保存 profile -> Repository 发布快照 -> main.cpp 按 provider 解析 -> TraceProxyAi 请求体透传。
struct ProviderProfile {
    std::string provider;
    std::string model;
    std::string api_key;

    ProviderProfile() = default;
    ProviderProfile(std::string provider_, std::string model_, std::string api_key_)
        : provider(std::move(provider_)), model(std::move(model_)), api_key(std::move(api_key_)) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProviderProfile, provider, model, api_key)
};

// 2. Prompt 配置 (对应 prompts 表)
// 这一轮故意不继续扩字段。
// 既然前后端当前仍按“整列表覆盖”保存 prompt，那么先维持 id/name/content/is_active 这组最小契约，
// 可以避免把 scope、分类、版本号之类还没钉死的语义提前写进表结构。
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
// 这轮渠道表只保留真实外发会吃到的最小字段，消息模板不再放进 Settings 存储。
// 模板内容由系统内置维护；Settings 只负责“发不发、发到哪、按什么阈值发、签名密钥是什么”。
struct AlertChannel {
    int id = 0;
    std::string name;
    std::string provider = "feishu";
    std::string webhook_url;
    std::string secret;
    std::string alert_threshold; // "critical", "error", "warning"
    bool is_active = false;

    AlertChannel() = default;
    AlertChannel(int id, std::string name, std::string provider, std::string webhook_url, std::string secret, std::string alert_threshold, bool is_active)
        : id(id), name(std::move(name)), provider(std::move(provider)), webhook_url(std::move(webhook_url)), secret(std::move(secret)), alert_threshold(std::move(alert_threshold)), is_active(is_active) {}

    // 1. 序列化 (Backend -> JSON)
    friend void to_json(nlohmann::json& j, const AlertChannel& p) {
        j = nlohmann::json{
            {"id", p.id},
            {"name", p.name},
            {"provider", p.provider},
            {"webhook_url", p.webhook_url},
            {"secret", p.secret},
            {"alert_threshold", p.alert_threshold},
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
        if (j.contains("secret")) j.at("secret").get_to(p.secret);
        if (j.contains("alert_threshold")) j.at("alert_threshold").get_to(p.alert_threshold);

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
    // 前端 Settings 直接按 provider id 展示这张表。
    // 这里用 map 而不是 vector，是为了让 `mock/gemini/glm/deepseek` 的 model/key 回填不依赖数组顺序。
    std::map<std::string, ProviderProfile> provider_profiles;
    std::vector<PromptConfig> prompts;
    std::vector<AlertChannel> channels;
    AllSettings() = default;
    AllSettings(AppConfig config_,
                std::map<std::string, ProviderProfile> provider_profiles_,
                std::vector<PromptConfig> prompts_,
                std::vector<AlertChannel> channels_)
    : config(std::move(config_)),
      provider_profiles(std::move(provider_profiles_)),
      prompts(std::move(prompts_)),
      channels(std::move(channels_)){}
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AllSettings, config, provider_profiles, prompts, channels)
};

struct AlertInfo{
    std::string trace_id;
    std::string summary;
    std::string time;
    std::string risk; // Added risk field
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AlertInfo, trace_id, summary, time, risk);
};
struct DashboardStats{
    int total_logs=0;
    int critical_risk=0;
    int error_risk=0;
    int warning_risk=0;
    int info_risk=0;
    int safe_risk=0;
    int unknown_risk=0;
    double avg_response_time=0.0;

    // --- 新增：瞬态指标 (Transient Metrics) ---
    // 不需要序列化到数据库，但需要序列化成 JSON 给前端
    double current_qps = 0.0;
    double backpressure = 0.0;

    std::vector<AlertInfo> recent_alerts;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(DashboardStats, total_logs, critical_risk, error_risk, warning_risk, info_risk, safe_risk, unknown_risk, avg_response_time, current_qps, backpressure, recent_alerts);
};
struct AnalysisResultItem{
    std::string trace_id;
    LogAnalysisResult result;
    int response_time_ms;
    std::string status;
};
