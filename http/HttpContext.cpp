#include "http/HttpContext.h"
#include <algorithm>
HttpContext::ParseResult HttpContext::parseRequest(MiniMuduo::net::Buffer *buf)
{
    while (true)
    {
        if (state_ == State::kExpectRequestLine)
        {
            const char *crlf = buf->findCRLF();
            if (crlf)
            {
                bool ans = parseRequestLine(buf->peek(), crlf-1);
                if (ans)
                {
                    state_ = State::kExpectHeaders;
                    buf->retrieve(crlf - buf->peek() + 2);
                }
                else
                {
                    state_ = State::KError;
                    return ParseResult::kError;
                }
            }
            else
            {
                return ParseResult::kNeedMoreData;
            }
        }
        else if (state_ == State::kExpectHeaders)
        {
            const char *crlf = buf->findCRLFCRLF();
            if (crlf)
            {
                bool ans = parseHeaders(buf->peek(), crlf-1);
                if (ans)
                {

                    buf->retrieve(crlf - buf->peek() + 4);
                    if (request_.method_ != "POST")
                    {
                        state_ = State::kGotAll;
                        return ParseResult::kSuccess;
                    }
                    else
                    {
                        state_ = State::kExpectBody;
                    }
                }
                else
                {
                    state_ = State::KError;
                    return ParseResult::kError;
                }
            }
            else
            {
                return ParseResult::kNeedMoreData;
            }
        }
        else if (state_ == State::kExpectBody)
        {
            if (request_.headers_.find("Content-Length") == request_.headers_.end())
            {
                state_ = State::KError;
                return ParseResult::kError;
            }

            size_t len = std::stoul(request_.headers_["Content-Length"]);
            if (buf->readableBytes() >= len)
            {
                request_.body_.assign(buf->peek(), buf->peek() + len);
                buf->retrieve(len);
                state_ = State::kGotAll;
                return ParseResult::kSuccess;
            }
            else
            {
                return ParseResult::kNeedMoreData;
            }
        }
        // 理论上不应该有这个
        return ParseResult::kNeedMoreData;
    }
}

bool HttpContext::parseRequestLine(const char *start, const char *end)
{
    const char *pos = strchr(start, ' ');
    if (pos == nullptr)
        return false;
    request_.method_.assign(start, pos - start);
    start = pos + 1;
    pos = strchr(start, ' ');
    if (pos == nullptr)
        return false;
    request_.path_.assign(start, pos - start);
    while(pos ==" ")//有可能不标准，有多余的空格
        pos++;
    start = pos + 5; // 过滤掉"Http/"
    if (end - start > 0)
        request_.version_.assign(start, end - start);
    else
        return false;
    return true;
}

bool HttpContext::parseHeaders(const char *start, const char *end)
{
    while (end-start > 0)
    {
        const char *pos1 = strchr(start, ':');
        if (pos1 == nullptr)
            return false;
        const char *pos2 = findCRLF(pos1, end);
        if (pos2 == nullptr)
        {
            std::string key = std::string(start, pos1 - start);
            std::string value = std::string(pos1 + 1, end - pos1 - 1);
            request_.headers_[key] = value;
            return true;
        }
        std::string key = std::string(start, pos1 - start);
        std::string value = std::string(pos1 + 1, pos2 - pos1 - 1);
        request_.headers_[key] = value;
        start = pos2 + 2;
    }
    return true;
}

const char *HttpContext::findCRLF(const char *start, const char *end)
{
    const char *crlf = "\r\n";
    // std::search 可以在一个序列中查找另一个子序列
    const char *result = std::search(start, end, crlf, crlf + 2);
    return result == end ? nullptr : result;
}