#include "http/HttpServer.h"
#include "http/HttpContext.h"
#include "http/HttpResponse.h"
#include <MiniMuduo/net/TcpConnection.h>
#include <MiniMuduo/base/LogMessage.h>
#include "util/TraceIdGenerator.h"
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
        conn->setContext(HttpContext());
    }
}
void HttpServer::onMessage(const MiniMuduo::net::TcpConnectionPtr &conn,
                           MiniMuduo::net::Buffer *buf,
                           MiniMuduo::base::Timestamp time)
{
    HttpContext* context=std::any_cast<HttpContext> (conn->getMutableContext());
    while(context->parseRequest(buf)==HttpContext::ParseResult::kSuccess){
        std::string trace_id_=generateTraceId();
        context->requestRef().setTraceId(trace_id_);
        LOG_STREAM_INFO<< "[trace: " << trace_id_ << "] New request for " << context->request().path()<<" from : "<<conn->peerAddress().toIpPort();
        HttpResponse resp;
        if(httpCallback_)
            httpCallback_(context->request(),&resp);
        MiniMuduo::net::Buffer outbuf;
        resp.appendToBuffer(&outbuf);
        conn->send(std::move(outbuf));
        LOG_STREAM_INFO<< "[trace: " << trace_id_ << "] Response for " << context->request().path()<<" to : "<<conn->peerAddress().toIpPort()<<" with answer : "<<resp.body_;
        if(resp.closeConnection_)
            conn->shutdown();
        context->reset();
    }
    if(context->states()==HttpContext::State::KError)
    {
        LOG_STREAM_ERROR<< " Error from : " << conn->peerAddress().toIpPort()<<" when parse request";
        conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
        conn->shutdown();
    }
}
