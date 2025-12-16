#pragma once
#include<sqlite3.h>
#include<string>
#include<vector>
#include<mutex>
#include <persistence/ConfigTypes.h>
class SqliteConfigRepository
{
private:
    sqlite3* db_=nullptr;
    std::mutex mutex_;
public:
    SqliteConfigRepository(const std::string & db_path);
    ~SqliteConfigRepository();
    std::vector<std::string> getActiveWebhookUrls();
    void addWebhookUrl(const std::string& url) ;
    void deleteWebhookUrl(const std::string& url) ;

    AppConfig getAppConfig();
    std::vector<PromptConfig> getAllPrompts();
    std::vector<AlertChannel> getAllChannels();
    AllSettings handleGetAll();
    void handleUpdateAppConfig(const std::map<std::string,std::string>& mp);
    void handleUpdatePrompt(const std::vector<PromptConfig>& prompts);
    void handleUpdateChannel(const std::vector<AlertChannel>& alerts);
};

