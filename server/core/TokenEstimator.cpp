#include "core/TokenEstimator.h"
#include "core/TraceSessionManager.h"

size_t TokenEstimator::Estimate(const SpanEvent& span) const
{
    // TODO: 先占位，后续接入近似估算或真实 tokenizer。
    (void)span;
    return 0;
}
