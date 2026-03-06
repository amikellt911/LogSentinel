#include "http/HttpContext.h"
#include <algorithm>
//#include <MiniMuduo/base/LogMessage.h>
HttpContext::ParseResult HttpContext::parseRequest(MiniMuduo::net::Buffer *buf)
{
    bool hasMore = true;
    // 没有想到的是，可以直接请求行结束，然后下面是\r\n\r\n
    while (hasMore)
    {
        if (state_ == State::kExpectRequestLine)
        {
            // 有可能出现
            const char *crlf = buf->findCRLF();
            if (!crlf)
            {
                hasMore = false;
                break;
            }
            if (!parseRequestLine(buf->peek(), crlf))
            {
                //LOG_STREAM_ERROR<<"parseRequestLine error";
                state_ = State::KError;
                return ParseResult::kError;
            }
            buf->retrieve(crlf - buf->peek() + 2);
            state_ = State::kExpectHeaders;
        }
        else if (state_ == State::kExpectHeaders)
        {
            const char *crlf = buf->findCRLF();
            if (!crlf)
            {
                hasMore = false;
                break;
            }
            const char *start = buf->peek();

            // 关键判断：遇到空行，说明 Header 部分结束
            if (start == crlf)
            {
                buf->retrieve(2); // 消费掉这个空的 \r\n

                // Header 结束，决定下一步去哪
                if (request_.method_ == "POST")
                { // 简化判断
                    state_ = State::kExpectBody;
                }
                else
                {
                    state_ = State::kGotAll;
                    hasMore = false; // 请求已完整，退出循环
                    return ParseResult::kSuccess;
                }
            }
            else
            {
                // 不是空行，说明这是一个普通的 Header 行
                if (!parseOneHeaderLine(start, crlf))
                {
                    //LOG_STREAM_ERROR<<"parseOneHeaderLine error";
                    state_ = State::KError;
                    return ParseResult::kError;
                }
                buf->retrieve(crlf - buf->peek() + 2);
                // 状态不变，继续循环，处理下一行 Header
            }
        }
        else if (state_ == State::kExpectBody)
        {
            if (request_.headers_.find("content-length") == request_.headers_.end())
            {
                //LOG_STREAM_ERROR<<"Content-Length not found";
                state_ = State::KError;
                return ParseResult::kError;
            }

            size_t len = std::stoul(request_.headers_["content-length"]);
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
    }
    // 理论上不应该有这个
    return ParseResult::kNeedMoreData;
}

bool HttpContext::parseRequestLine(const char *start, const char *end)
{
    const char *pos = start;
    while (*pos == ' ') // 有可能不标准，有多余的空格
        pos++;
    start = pos;
    pos = strchr(start, ' ');
    if (pos == nullptr)
        return false;
    request_.method_.assign(start, pos - start);
    while (*pos == ' ') // 有可能不标准，有多余的空格
        pos++;
    start = pos;
    pos = strchr(start, ' ');
    if (pos == nullptr)
        return false;
    request_.path_.assign(start, pos - start);
    while (*pos == ' ') // 有可能不标准，有多余的空格
        pos++;
    start = pos;
    const char *http_prefix = "HTTP/";
    if (std::strncmp(start, http_prefix, 5) != 0)
    {
        return false; // 不以 HTTP/ 开头
    }
    start = pos + 5; // 过滤掉"Http/"
    if (end - start > 0)
        request_.version_.assign(start, end - start);
    else
        return false;
    return true;
}

bool HttpContext::parseOneHeaderLine(const char *start, const char *end)
{
    const char *pos1 = strchr(start, ':');
    if (!pos1 || pos1 > end) { // 冒号必须在行内
        return false;
    }
    const char *key_start = start;
    while (key_start < pos1 && *key_start == ' ')
        key_start++;

    const char *key_end = pos1 - 1;
    while (key_end > key_start && *key_end == ' ')
        key_end--;
    if (key_end < key_start)
        return false;
    const char *value_start = pos1 + 1;
    while (value_start < end && *value_start == ' ')
        value_start++;
    const char *value_end = end - 1;
    while (value_end > value_start && *value_end == ' ')
        value_end--;
    if (value_end < value_start)
        return false;
    std::string key = std::string(key_start, key_end + 1 - key_start);
    std::string value = std::string(value_start, value_end + 1 - value_start);
    std::transform(key.begin(), key.end(), key.begin(),::tolower);
    request_.headers_[key] = value;
    //LOG_STREAM_ERROR<<"parseOneHeaderLine: "<<key<<" "<<value;
    return true;
}

bool HttpContext::parseHeaders(const char *start, const char *end)
{
    // const char *ifcon=findCRLFCRLF(start, end+4);
    while (end - start > 0)
    {
        const char *pos1 = strchr(start, ':');
        if (pos1 == nullptr)
            return false;
        const char *pos2 = findCRLF(pos1, end);
        const char *key_start = start;
        while (key_start < pos1 && *key_start == ' ')
            key_start++;

        const char *key_end = pos1 - 1;
        while (key_end > key_start && *key_end == ' ')
            key_end--;
        if (key_end < key_start)
            return false;
        if (pos2 == nullptr)
        {
            const char *value_end = end - 1;
            const char *value_start = pos1 + 1;
            while (value_start < value_end && *value_start == ' ')
                value_start++;
            while (value_end > value_start && *value_end == ' ')
                value_end--;
            if (value_end < value_start)
                return false;
            std::string key = std::string(key_start, key_end + 1 - key_start);
            std::string value = std::string(value_start, value_end + 1 - value_start);
            std::transform(key.begin(), key.end(), key.begin(),::tolower);
            request_.headers_[key] = value;
            return true;
        }
        const char *value_start = pos1 + 1;
        while (value_start < pos2 && *value_start == ' ')
            value_start++;
        const char *value_end = pos2 - 1;
        while (value_end > value_start && *value_end == ' ')
            value_end--;
        if (value_end < value_start)
            return false;
        std::string key = std::string(key_start, key_end + 1 - key_start);
        std::string value = std::string(value_start, value_end + 1 - value_start);
        request_.headers_[key] = value;
        start = pos2 + 2;
        // if(ifcon==pos2)
        //     return true;
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

const char *HttpContext::findCRLFCRLF(const char *start, const char *end)
{
    const char *crlf = "\r\n\r\n";
    // std::search 可以在一个序列中查找另一个子序列
    const char *result = std::search(start, end, crlf, crlf + 4);
    return result == end ? nullptr : result;
}