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
    // Internal loaders
    AppConfig getAppConfigInternal();
    std::vector<PromptConfig> getAllPromptsInternal();
    std::vector<AlertChannel> getAllChannelsInternal();
    void loadFromDbInternal();

private:
    // --- Snapshot State ---
    SystemConfigPtr current_snapshot_;
    mutable std::mutex snapshot_mutex_; // Protects current_snapshot_ swap

    // --- Database State ---
    sqlite3* db_=nullptr;
    std::mutex db_write_mutex_; // Protects DB writes (Transactions)

public:
    SqliteConfigRepository(const std::string & db_path);
    ~SqliteConfigRepository();

    // Core API: Get immutable snapshot
    SystemConfigPtr getSnapshot();

    // Compatibility APIs (Delegates to Snapshot)
    AppConfig getAppConfig();
    std::vector<PromptConfig> getAllPrompts();
    std::vector<AlertChannel> getAllChannels();
    AllSettings getAllSettings();

    // Write Operations
    void handleUpdateAppConfig(const std::map<std::string,std::string>& mp);
    void handleUpdatePrompt(const std::vector<PromptConfig>& prompts_input);
    void handleUpdateChannel(const std::vector<AlertChannel>& channels_input);

    // Deprecated APIs
    std::vector<std::string> getActiveWebhookUrls();
    void addWebhookUrl(const std::string& url) ;
    void deleteWebhookUrl(const std::string& url) ;
};
