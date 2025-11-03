#pragma once
#include<string>
#include<unordered_map>
#include<MiniMuduo/net/Buffer.h>

struct HttpResponse
{ 

    enum class HttpStatusCode
    {
        kUnknown,
        k200Ok = 200,
        k202Acceptd=202,
        k301MovedPermanently = 301,
        k400BadRequest = 400,
        k404NotFound = 404,
        k500InternalServerError = 500,
        k503ServiceUnavailable=503,
    };
    
    HttpStatusCode statusCode_=HttpStatusCode::kUnknown;
    std::string statusMessage_;
    std::unordered_map<std::string,std::string> headers_;
    std::string body_;
    bool closeConnection_=false;

    void appendToBuffer(MiniMuduo::net::Buffer *output) const;
    void setStatusCode(HttpStatusCode code);
    void setStatusMessage(std::string message);
    void setHeader(std::string key,std::string value);
    void setBody(std::string body);
};