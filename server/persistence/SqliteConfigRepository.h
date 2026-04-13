#pragma once
#include<sqlite3.h>
#include<atomic>
#include<string>
#include<vector>
#include <memory>
#include<mutex>
#include "persistence/SystemConfig.h"

class SqliteConfigRepository
{
private:
    // 内部加载函数
    AppConfig getAppConfigInternal();
    // 获取 Prompt 列表
    std::vector<PromptConfig> getAllPromptsInternal();
    // 别名单独落表，避免热路径重复反序列化 JSON 字符串。
    std::vector<std::string> getTraceEndAliasesInternal();
    std::vector<AlertChannel> getAllChannelsInternal();
    // 从数据库重新加载全部配置并生成新的快照
    void loadFromDbInternal();

private:
    // --- 快照状态 ---
    SystemConfigPtr current_snapshot_;
    // 这条版本线只服务 Trace AI 请求体里的 model/api_key 热更新。
    // 当前主 provider 和 fallback provider 都会挂到这条版本线上，
    // 但 provider 路由本身仍然属于冷启动配置，所以这里故意不把所有 app_config 写操作都算进来。
    std::atomic<uint64_t> trace_ai_runtime_version_{1};

    // --- 数据库状态 ---
    sqlite3* db_=nullptr;
    std::mutex db_write_mutex_; // 保护数据库写操作（事务）

public:
    SqliteConfigRepository(const std::string & db_path);
    ~SqliteConfigRepository();

    // 核心 API: 获取不可变快照
    SystemConfigPtr getSnapshot();
    uint64_t getTraceAiRuntimeVersion() const;

    // 兼容 API (委托给快照)
    AppConfig getAppConfig();
    // 返回 Prompt 列表
    std::vector<PromptConfig> getAllPrompts();
    std::vector<AlertChannel> getAllChannels();
    AllSettings getAllSettings();

    // 写操作
    void handleUpdateAppConfig(const std::map<std::string,std::string>& mp);
    void handleUpdatePrompt(const std::vector<PromptConfig>& prompts_input);
    void handleUpdateChannel(const std::vector<AlertChannel>& channels_input);

    // 已弃用 API
    std::vector<std::string> getActiveWebhookUrls();
    void addWebhookUrl(const std::string& url) ;
    void deleteWebhookUrl(const std::string& url) ;
};
