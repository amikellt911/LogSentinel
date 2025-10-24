#include "http/HttpServer.h"

HttpServer::HttpServer(MiniMuduo::net::EventLoop* loop,
                       const MiniMuduo::net::InetAddress& listenAddr,
                       const std::string& nameArg,
                       MiniMuduo::net::TcpServer::Option option)
    :server_(loop, listenAddr, nameArg, option)
{
    server_.setConnectionCallback(std::bind(&HttpServer::onConnection,this,std::placeholders::_1));
    server_.setMessageCallback(std::bind(&HttpServer::onMessage,this,std::placeholders::_1,std::placeholders::_2,std::placeholders::_3));
}

void HttpServer::onConnection(const MiniMuduo::net::TcpConnectionPtr& conn)
{ 
}

void HttpServer::onMessage(const MiniMuduo::net::TcpConnectionPtr& conn,
                           MiniMuduo::net::Buffer* buf,
                           MiniMuduo::base::Timestamp time)
{
}
