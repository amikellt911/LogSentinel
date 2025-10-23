#include<string>
#include<unordered_map>
struct HttpRequest{
        void reset()
        {
            method_="";
            path_="";
            version_="";
            headers_.clear();
            body_="";
        }
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
};