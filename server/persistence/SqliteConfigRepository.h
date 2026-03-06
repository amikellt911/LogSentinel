#pragma once
#include<sqlite3.h>
#include<string>
#include<vector>
#include<mutex>
#include <memory>
#include "persistence/SystemConfig.h"

class SqliteConfigRepository
{
private:
    // 内部加载函数
    AppConfig getAppConfigInternal();
    // 获取 Prompt 列表
    std::vector<PromptConfig> getAllPromptsInternal();
    std::vector<AlertChannel> getAllChannelsInternal();
    // 从数据库重新加载全部配置并生成新的快照
    void loadFromDbInternal();

private:
    // --- 快照状态 ---
    SystemConfigPtr current_snapshot_;
    mutable std::mutex snapshot_mutex_; // 保护 current_snapshot_ 的交换

    // --- 数据库状态 ---
    sqlite3* db_=nullptr;
    std::mutex db_write_mutex_; // 保护数据库写操作（事务）

public:
    SqliteConfigRepository(const std::string & db_path);
    ~SqliteConfigRepository();

    // 核心 API: 获取不可变快照
    SystemConfigPtr getSnapshot();

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
