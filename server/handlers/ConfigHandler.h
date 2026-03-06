#pragma once
#include <memory>
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include <MiniMuduo/net/TcpConnection.h>
class SqliteConfigRepository;
class ThreadPool;
class ConfigHandler {
public:
    explicit ConfigHandler(std::shared_ptr<SqliteConfigRepository> repo,ThreadPool* tpool);

    void handleGetSettings(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);
    void handleUpdateAppConfig(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);
    void handleUpdatePrompts(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);
    void handleUpdateChannels(const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn);
private:
    std::shared_ptr<SqliteConfigRepository> repo_;
    ThreadPool* tpool_;
};