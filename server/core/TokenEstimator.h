#pragma once

#include <cstddef>

struct SpanEvent;

// TokenEstimator 作为 token 估算的占位入口，后续再接入真实估算逻辑或第三方库。
class TokenEstimator
{
public:
    size_t Estimate(const SpanEvent& span) const;
};
