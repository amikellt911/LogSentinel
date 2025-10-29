#include"http/HttpResponse.h"

void HttpResponse::appendToBuffer(MiniMuduo::net::Buffer *output) const
{
    output->append("HTTP/1.1 ");
    output->append(std::to_string(static_cast<int>(statusCode_)));
    output->append(" ");
    output->append(statusMessage_);
    output->append("\r\n");

    int bodylen=body_.size();
    output->append("Content-Length: ");
    output->append(std::to_string(bodylen));
    output->append("\r\n");

    if(closeConnection_)
    if(closeConnection_)
    {
        output->append("Connection: close\r\n");
    }
    else
    {
        output->append("Connection: keep-alive\r\n");
    }

    for(auto &header:headers_)
    {
        output->append(header.first);
        output->append(": ");
        output->append(header.second);
        output->append("\r\n");
    }
    output->append("\r\n");
    output->append(body_);
}

void HttpResponse::setStatusCode(HttpStatusCode code)
{
    statusCode_=code;
    switch(code)
    {
        case HttpStatusCode::kUnknown:
            statusMessage_="Unknown";
            break;
        case HttpStatusCode::k200Ok:
            statusMessage_="OK";
            break;
        case HttpStatusCode::k400BadRequest:
            statusMessage_="Bad Request";
        break;
        case HttpStatusCode::k404NotFound:
            statusMessage_="Not Found";
        case HttpStatusCode::k500InternalServerError:
            statusMessage_="Internal Server Error";
    }
}

void HttpResponse::setStatusMessage(std::string message)
{
    statusMessage_=std::move(message);
}

void HttpResponse::setBody(std::string body)
{
    body_=std::move(body);
}

void HttpResponse::setHeader(std::string key,std::string value)
{
    headers_[std::move(key)]=std::move(value);
}