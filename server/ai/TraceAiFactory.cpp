#include "ai/TraceAiFactory.h"

#include "ai/TraceProxyAi.h"

std::shared_ptr<TraceAiProvider> CreateTraceAiProvider(const TraceAiFactoryOptions& options)
{
    // 工厂这里不参与 Prompt 组装，也不自己决定重试策略。
    // 它只负责把启动期已经收口好的 prompt/model/api_key/retry 配置整包交给具体 provider，
    // 避免 main.cpp 后面既要管 provider 选择，又要自己手搓 HTTP 请求体细节。
    return std::make_shared<TraceProxyAi>(options.base_url,
                                          options.backend,
                                          options.timeout_ms,
                                          options.prompt_template,
                                          options.model,
                                          options.api_key,
                                          options.retry_enabled,
                                          options.retry_max_attempts);
}
