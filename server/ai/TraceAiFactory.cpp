#include "ai/TraceAiFactory.h"

#include "ai/TraceProxyAi.h"

std::shared_ptr<TraceAiProvider> CreateTraceAiProvider(const TraceAiFactoryOptions& options)
{
    // 工厂这里不参与 Prompt 组装，只负责把启动期已经算好的冷启动模板交给具体 provider。
    return std::make_shared<TraceProxyAi>(options.base_url,
                                          options.backend,
                                          options.timeout_ms,
                                          options.prompt_template);
}
