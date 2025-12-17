#pragma once
#include<sqlite3.h>
#include<string>
#include<vector>
#include<mutex>
#include <shared_mutex>
#include <persistence/ConfigTypes.h>
class SqliteConfigRepository
{
private:
    AppConfig getAppConfigInternal();
    std::vector<PromptConfig> getAllPromptsInternal();
    std::vector<AlertChannel> getAllChannelsInternal();
    void loadFromDbInternal();
private:
    // 缓存对象
    AppConfig cached_app_config_;
    std::vector<PromptConfig> cached_prompts_;
    std::vector<AlertChannel> cached_channels_;
    
    // 读写锁 (比互斥锁更适合读多写少场景)
    // 允许多个 AI 线程同时读，但写的时候互斥
    mutable std::shared_mutex config_mutex_; 
    
    // 标记缓存是否已初始化
    bool is_initialized_ = false;

    //-------------------------
    sqlite3* db_=nullptr;
    std::mutex mutex_;
public:
    SqliteConfigRepository(const std::string & db_path);
    ~SqliteConfigRepository();


    AppConfig getAppConfig();
    std::vector<PromptConfig> getAllPrompts();
    // Helper to get the single active prompt (or empty if none)
    PromptConfig getActivePrompt();
    std::vector<AlertChannel> getAllChannels();
    AllSettings getAllSettings();
    void handleUpdateAppConfig(const std::map<std::string,std::string>& mp);
    void handleUpdatePrompt(const std::vector<PromptConfig>& prompts_input);
    void handleUpdateChannel(const std::vector<AlertChannel>& channels_input);
    //过时弃用的api
    //////////////////////////////////////////////////
    std::vector<std::string> getActiveWebhookUrls();
    void addWebhookUrl(const std::string& url) ;
    void deleteWebhookUrl(const std::string& url) ;
};

