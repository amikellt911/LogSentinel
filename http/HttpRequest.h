#pragma once
#include<string>
#include<unordered_map>
struct HttpRequest{
        void reset()
        {
            method_.clear();
            path_.clear();
            version_.clear();
            headers_.clear();
            body_.clear();
            trace_id.clear();
        }
        //追踪id
        std::string trace_id;
        //第一部分：请求行
        //get/post 等方法
        std::string method_;
        //路径，比如 /logs
        std::string path_;
        //http 版本
        std::string version_;
        //第二部分：请求头
        std::unordered_map<std::string,std::string> headers_;
        //第三部分：请求体
        std::string body_;

        const std::string getHeader(std::string key) const
        {
            return headers_.at(key);
        }

        void setTraceId(std::string id){
            trace_id=std::move(id);
        }
        const std::string& getTraceId() const{
            return trace_id;
        }

        const std::string path() const{
            return path_;
        }

        const std::string method() const{
            return method_;
        }

        const std::string version() const{
            return version_;
        }
};