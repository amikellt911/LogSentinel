// ai/MockTraceAi.cpp
#include "ai/MockTraceAi.h"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp> // 解析代理返回的 JSON 字段，避免手写解析出错
#include <chrono>
#include <stdexcept>
#include <vector>

// 构造函数：初始化代理地址，保持与日志分析相同的接入方式，减少环境差异。
MockTraceAi::MockTraceAi()
{
    const std::string base_url = "http://127.0.0.1:8001";
    analyze_trace_url_ = base_url + "/analyze/trace/mock";
}

// 析构函数默认即可，网络会话在方法内部创建与销毁，避免跨线程共享状态。
MockTraceAi::~MockTraceAi() = default;

TraceAiResponse MockTraceAi::AnalyzeTrace(const std::string& trace_payload)
{
    // 这里沿用 Trace proxy 的统一请求方式，方便 mock/gemini 两条 Trace AI 路径共用同一套调试流程。
    cpr::Session session_;
    session_.SetHeader(cpr::Header{{"Content-Type", "text/plain"}});
    session_.SetTimeout(std::chrono::seconds(10));
    session_.SetUrl(cpr::Url{analyze_trace_url_});
    session_.SetBody(cpr::Body{trace_payload});

    cpr::Response r = session_.Post();

    if (r.status_code != 200)
    {
        // 失败时直接抛异常，避免后续解析误导定位问题。
        throw std::runtime_error("Trace AI Proxy Error: HTTP " + std::to_string(r.status_code) +
                                 ", Body: " + r.text);
    }

    nlohmann::json response_json;
    try
    {
        response_json = nlohmann::json::parse(r.text);
    }
    catch (const nlohmann::json::parse_error &e)
    {
        throw std::runtime_error("Trace AI Protocol Error: Proxy returned invalid JSON. " + std::string(e.what()));
    }

    if (!response_json.contains("analysis"))
    {
        // 代理协议缺少字段时直接失败，防止下游读到半结构化数据。
        throw std::runtime_error("Trace AI Protocol Error: Response missing 'analysis' field");
    }

    std::string inner_json_str = response_json["analysis"];
    nlohmann::json analysis_json;
    try
    {
        analysis_json = nlohmann::json::parse(inner_json_str);
    }
    catch (const nlohmann::json::parse_error &e)
    {
        throw std::runtime_error("Trace AI Logic Error: Returned non-JSON string. Raw: " +
                                 inner_json_str + ". Error: " + e.what());
    }

    const std::vector<std::string> required_fields = {"summary", "risk_level", "root_cause", "solution"};
    for (const auto &field : required_fields)
    {
        if (!analysis_json.contains(field))
        {
            throw std::runtime_error("Trace AI Validation Error: Missing required field '" + field + "'");
        }
    }

    std::string risk = analysis_json["risk_level"];
    if (risk != "critical" && risk != "warning" && risk != "error" && risk != "info" && risk != "safe")
    {
        // 这里严格校验等级，避免前端出现未知枚举。
        throw std::runtime_error("Trace AI Validation Error: Invalid risk_level '" + risk + "'");
    }

    TraceAiResponse response;
    response.analysis = analysis_json.get<LogAnalysisResult>();
    // mock 路径当前不回 usage，目的是继续覆盖“provider usage 缺失 -> 本地估算回退”的主链分支。
    response.usage.reset();
    return response;
}
