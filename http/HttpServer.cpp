#include "http/HttpServer.h"
#include "http/HttpContext.h"
#include "http/HttpResponse.h"
#include <MiniMuduo/net/TcpConnection.h>
#include<MiniMuduo/base/Logger.h>
HttpServer::HttpServer(MiniMuduo::net::EventLoop *loop,
                       const MiniMuduo::net::InetAddress &listenAddr,
                       const std::string &nameArg,
                       MiniMuduo::net::TcpServer::Option option)
    : server_(loop, listenAddr, nameArg, option)
{
    server_.setConnectionCallback(std::bind(&HttpServer::onConnection, this, std::placeholders::_1));
    server_.setMessageCallback(std::bind(&HttpServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

void HttpServer::onConnection(const MiniMuduo::net::TcpConnectionPtr &conn)
{
    if (conn->connected())
    {
        HttpContext context_;
        conn->setContext(context_);
    }
}
void HttpServer::onMessage(const MiniMuduo::net::TcpConnectionPtr &conn,
                           MiniMuduo::net::Buffer *buf,
                           MiniMuduo::base::Timestamp time)
{
    HttpContext* context=std::any_cast<HttpContext> (conn->getMutableContext());
    while(context->parseRequest(buf)==HttpContext::ParseResult::kSuccess){
        HttpResponse resp;
        httpCallback_(context->request(),&resp);
        context->reset();
    }
}
