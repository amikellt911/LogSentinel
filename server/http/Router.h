#pragma once
#include"HttpRequest.h"
#include<functional>
#include"HttpResponse.h"
#include<memory>
#include<string>
#include<unordered_map>
namespace MiniMuduo{
    namespace net{
        class TcpConnection;
    }
}
using HttpHandler = std::function<void(const HttpRequest&,HttpResponse*,const std::shared_ptr<MiniMuduo::net::TcpConnection>&)>;

class Router{
    public:
        void add(const std::string& method,const std::string& path,HttpHandler handler);
        bool dispatch(const HttpRequest& request,HttpResponse* response,const std::shared_ptr<MiniMuduo::net::TcpConnection>& conn);
    private:
        //当路由高级实现，实现类似静态文件或通配符的时候，一般用map或Tries树
        std::unordered_map<std::string,HttpHandler> exactRoutes_;
        std::vector<std::pair<std::string,HttpHandler>> prefixRoutes_;
};