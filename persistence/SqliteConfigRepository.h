#pragma once
#include<sqlite3.h>
#include<string>
#include<vector>
#include<mutex>
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
};

