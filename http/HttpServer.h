#pragma once
#include<MiniMuduo/net/TcpServer.h>
#include<MiniMuduo/net/InetAddress.h>
#include"http/HttpRequest.h"
class HttpResponse;
class HttpServer
{
    public:
        using HttpCallback = std::function<void(const HttpRequest&,HttpResponse*,std::string)>;
        void start();
        HttpServer(MiniMuduo::net::EventLoop *loop,const MiniMuduo::net::InetAddress& listenAddr,const std::string& nameArg,MiniMuduo::net::TcpServer::Option option=MiniMuduo::net::TcpServer::kNoReusePort);
        void setHttpCallback(const HttpCallback& cb){httpCallback_=std::move(cb);}
    private:
        void onConnection(const MiniMuduo::net::TcpConnectionPtr& conn);
        void onMessage(const MiniMuduo::net::TcpConnectionPtr& conn,MiniMuduo::net::Buffer *buf,MiniMuduo::base::Timestamp time);
    private:
        MiniMuduo::net::TcpServer server_;
        HttpCallback httpCallback_;

};