#pragma once

#include <cpr/cpr.h>

#include <string>

// 这里专门收口 TraceProxyAi 的“HTTP 层没拿到有效响应”报错格式。
// 既然 status_code=0 代表的是传输层失败，而不是 provider 业务失败，
// 那错误信息里就必须把 cpr 的 error.code / error.message 一起带出来，不然前端只会看到一条空壳 HTTP 0。
inline std::string BuildTraceProxyTransportErrorMessage(const std::string& url,
                                                        const cpr::Response& response)
{
    std::string message = "Trace AI Proxy Error: url=" + url +
                          ", HTTP " + std::to_string(response.status_code);

    if (response.status_code == 0 || !response.error.message.empty()) {
        message += ", cpr_error_code=" + std::to_string(static_cast<int>(response.error.code));
        if (!response.error.message.empty()) {
            message += ", cpr_error_message=" + response.error.message;
        }
    }

    message += ", Body: " + response.text;
    return message;
}
