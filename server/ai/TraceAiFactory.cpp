#include "ai/TraceAiFactory.h"

#include "ai/TraceProxyAi.h"

std::shared_ptr<TraceAiProvider> CreateTraceAiProvider(const TraceAiFactoryOptions& options)
{
    return std::make_shared<TraceProxyAi>(options.base_url, options.backend, options.timeout_ms);
}
