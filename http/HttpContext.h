#include"http/HttpRequest.h"
#include<MiniMuduo/net/Buffer.h>
#include<cstring>
class HttpContext
{
    public:
        enum class State
        {
            kExpectRequestLine,
            kExpectHeaders,
            kExpectBody,
            kGotAll,
            KError
        };

        enum class ParseResult
        {
            kSuccess,//解析成功出一条新的request
            kError,//解析出现问题,记得重置数据
            kNeedMoreData//数据不足，需要更多数据
        };
        ParseResult parseRequest(MiniMuduo::net::Buffer *buf);
        bool gotAll() const{return state_==State::kGotAll;}
        void reset()
        {
            state_=State::kExpectRequestLine;
            request_.reset();
        }
        const HttpRequest& request() const
        {
            return request_;
        }
    private:
        State state_=State::kExpectRequestLine;
        HttpRequest request_;
        //左闭右开，cpp标准
        bool parseRequestLine(const char* start,const char* end);
        bool parseHeaders(const char* start,const char* end);
        const char* findCRLF(const char* start,const char* end);

};