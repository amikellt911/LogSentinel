#include "http/HttpServer.h"
#include "MiniMuduo/net/EventLoop.h"
#include "http/HttpResponse.h"

void onRequest(const HttpRequest& req, HttpResponse* resp)
{
    std::string method=req.method();
    if(method=="GET")
    {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
        resp->setHeader("User-Agent","llt");
        resp->setBody(std::move("hello world"));
    }
    else if(method=="POST")
    {
        resp->setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
        resp->setHeader("User-Agent","llt");
        resp->setBody(std::move("received log"));
    }else{
        resp->setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
        resp->setHeader("User-Agent","llt");
        resp->setBody(std::move("Unknown method"));
    }
}

class testServer : public HttpServer
{
public:
    testServer(MiniMuduo::net::EventLoop* loop,
               const MiniMuduo::net::InetAddress& listenAddr)
        : HttpServer(loop, listenAddr, "testServer")
    {
        setHttpCallback(onRequest);
        setThreadNum(4);
    }
};

int main(){ 
    MiniMuduo::net::EventLoop loop;
    MiniMuduo::net::InetAddress addr(8080); 
    testServer server(&loop,addr);
    server.start();   
    loop.loop();
    return 0; 
}
