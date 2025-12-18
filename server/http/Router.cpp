#include"http/Router.h"
#include "Router.h"
#include<MiniMuduo/net/TcpConnection.h>
void Router::add(const std::string &method, const std::string &path, HttpHandler handler)
{
    std::string key=method+":"+path;
    if(path.back()=='*'){
        key.pop_back();
        prefixRoutes_.push_back({key,handler});
    }else{
        exactRoutes_[key]=handler;
    }
}

bool Router::dispatch(const HttpRequest &request, HttpResponse *response, const std::shared_ptr<MiniMuduo::net::TcpConnection> &conn)
{
    //cors预检
    if(request.method()=="OPTIONS"){
        response->setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
        response->setHeader("Access-Control-Allow-Origin","*");
        response->setHeader("Access-Control-Allow-Methods","POST,GET,OPTIONS");
        response->setHeader("Access-Control-Allow-Headers","Content-Type");
        response->setHeader("Access-Control-Max-Age","86400");
        MiniMuduo::net::Buffer buf;
        response->appendToBuffer(&buf);
        conn->send(std::move(buf));
        return true;
    }
    std::string key=request.method()+":"+request.path();
    if(exactRoutes_.count(key)){
        exactRoutes_[key](request,response,conn);
        return true;
    }
    for(const auto& route:prefixRoutes_){
        if(key.rfind(route.first,0)==0){
            route.second(request,response,conn);
            return true;
        }
    }
    return false;
}
